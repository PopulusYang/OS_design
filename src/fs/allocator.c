

#include "fs/allocator.h"
#include "fs/disk_io.h"

#include <stdio.h>
#include <string.h>
#include <pthread.h>


#define MINODE_HASH_SIZE        128


typedef struct IHashNode {
    MemINode           in;
    struct IHashNode  *h_next;
    uint8_t            in_use;
    uint8_t            rwlock_inited;
    uint8_t            pad[2];
    pthread_rwlock_t   rwlock;
} IHashNode;


#define INODE_STACK_EMPTY       ((uint16_t)0xFFFFU)


static int           g_fs_mounted = 0;


static SuperBlock    g_super;


static int           g_super_dirty = 0;


static char          g_disk_path[512];


static IHashNode    *g_inode_hash[MINODE_HASH_SIZE];


static IHashNode     g_inode_pool[TOTAL_INODES];



static int inode_block_no(uint16_t ino)
{
    return INODE_ZONE_START + (int)(ino / INODES_PER_BLOCK);
}

static int read_disk_inode(uint16_t ino, DiskINode *out)
{
    char  block_buf[BLOCK_SIZE];
    int   blk;
    int   slot;
    int   off;

    if (out == NULL || ino >= TOTAL_INODES) {
        return -1;
    }

    blk  = inode_block_no(ino);
    slot = (int)(ino % INODES_PER_BLOCK);
    off  = slot * DISK_INODE_SIZE;

    if (read_block(blk, block_buf) != 0) {
        return -1;
    }

    memcpy(out, block_buf + off, (size_t)DISK_INODE_SIZE);
    return 0;
}

static int write_disk_inode(uint16_t ino, const DiskINode *inode)
{
    char block_buf[BLOCK_SIZE];
    int  blk;
    int  slot;
    int  off;

    if (inode == NULL || ino >= TOTAL_INODES) {
        return -1;
    }

    blk  = inode_block_no(ino);
    slot = (int)(ino % INODES_PER_BLOCK);
    off  = slot * DISK_INODE_SIZE;

    if (read_block(blk, block_buf) != 0) {
        return -1;
    }

    memcpy(block_buf + off, inode, (size_t)DISK_INODE_SIZE);
    return write_block(blk, block_buf);
}




static int reg_header_valid(uint16_t m)
{
    if (m == 0) {
        return 0;
    }
    if (m > MAX_FREE_BLOCKS) {
        return 0;
    }
    return 1;
}









static int load_reg_group(uint16_t reg_blk)
{
    uint16_t reg_buf[BLOCK_SIZE / (int)sizeof(uint16_t)];
    int      m;
    int      j;

    if (read_block((int)reg_blk, reg_buf) != 0) {
        return -1;
    }

    m = (int)reg_buf[0];
    if (!reg_header_valid((uint16_t)m)) {
        return -1;
    }

    for (j = 0; j < m; j++) {
        g_super.s_free_block_stack[j] = reg_buf[j + 1];
    }

    if (m < MAX_FREE_BLOCKS) {
        
        g_super.s_free_block_stack[m] = reg_blk;
        g_super.s_free_block_count = (uint16_t)(m + 1);
    } else {
        g_super.s_free_block_count = (uint16_t)m;
    }

    g_super.s_free_block_chain = reg_buf[m + 1];
    return 0;
}


static int reload_free_block_stack(void)
{
    if (g_super.s_free_block_count > 0) {
        return 0;
    }
    if (g_super.s_free_block_chain == 0) {
        return -1;
    }
    return load_reg_group(g_super.s_free_block_chain);
}


static int maybe_reload_last_group_leader(void)
{
    uint16_t leader;
    uint16_t reg_buf[BLOCK_SIZE / (int)sizeof(uint16_t)];

    if (g_super.s_free_block_count != 1) {
        return 0;
    }

    leader = g_super.s_free_block_stack[0];

    
    if (g_super.s_free_block_chain == 0) {
        return 0;
    }

    
    if (read_block((int)leader, reg_buf) != 0) {
        return -1;
    }

    if (reg_header_valid(reg_buf[0])) {
        return load_reg_group(leader);
    }

    
    return 0;
}



static uint32_t inode_hash_key(uint16_t ino)
{
    return (uint32_t)(ino % MINODE_HASH_SIZE);
}

static IHashNode *inode_pool_alloc(void)
{
    int i;

    for (i = 0; i < TOTAL_INODES; i++) {
        if (!g_inode_pool[i].in_use) {
            memset(&g_inode_pool[i], 0, sizeof(g_inode_pool[i]));
            g_inode_pool[i].in_use = 1;
            if (pthread_rwlock_init(&g_inode_pool[i].rwlock, NULL) != 0) {
                g_inode_pool[i].in_use = 0;
                return NULL;
            }
            g_inode_pool[i].rwlock_inited = 1;
            return &g_inode_pool[i];
        }
    }
    return NULL;
}

static void inode_hash_insert(IHashNode *node)
{
    uint32_t key;

    if (node == NULL) {
        return;
    }

    key = inode_hash_key(node->in.m_inode_no);
    node->h_next = g_inode_hash[key];
    g_inode_hash[key] = node;
}

static IHashNode *inode_hash_find(uint16_t ino)
{
    uint32_t    key;
    IHashNode  *p;

    key = inode_hash_key(ino);
    for (p = g_inode_hash[key]; p != NULL; p = p->h_next) {
        if (p->in.m_inode_no == ino) {
            return p;
        }
    }
    return NULL;
}

static void inode_hash_remove(IHashNode *node)
{
    uint32_t    key;
    IHashNode  *p;
    IHashNode **pp;

    if (node == NULL) {
        return;
    }

    key = inode_hash_key(node->in.m_inode_no);
    pp  = &g_inode_hash[key];
    for (p = *pp; p != NULL; pp = &p->h_next, p = *pp) {
        if (p == node) {
            *pp = p->h_next;
            p->h_next = NULL;
            return;
        }
    }
}

static void inode_pool_free(IHashNode *node)
{
    if (node == NULL) {
        return;
    }
    inode_hash_remove(node);
    if (node->rwlock_inited) {
        pthread_rwlock_destroy(&node->rwlock);
    }
    memset(node, 0, sizeof(*node));
}

static void inode_cache_reset(void)
{
    int i;

    for (i = 0; i < TOTAL_INODES; i++) {
        if (g_inode_pool[i].in_use && g_inode_pool[i].rwlock_inited) {
            pthread_rwlock_destroy(&g_inode_pool[i].rwlock);
        }
    }
    memset(g_inode_hash, 0, sizeof(g_inode_hash));
    memset(g_inode_pool, 0, sizeof(g_inode_pool));
}


static int scan_free_inode(uint16_t *out_ino)
{
    uint16_t  ino;
    DiskINode dinode;

    if (out_ino == NULL) {
        return -1;
    }

    for (ino = 2; ino < TOTAL_INODES; ino++) {
        if (read_disk_inode(ino, &dinode) != 0) {
            return -1;
        }
        if (dinode.d_mode == 0 && dinode.d_nlink == 0) {
            *out_ino = ino;
            return 0;
        }
    }
    return -1;
}


static int pop_inode_stack(uint16_t *out_ino)
{
    int top;

    if (out_ino == NULL) {
        return -1;
    }
    if (g_super.s_inode_stack_top == INODE_STACK_EMPTY) {
        return -1;
    }

    top = (int)g_super.s_inode_stack_top;
    *out_ino = g_super.s_inode_free_stack[top];
    if (top == 0) {
        g_super.s_inode_stack_top = INODE_STACK_EMPTY;
    } else {
        g_super.s_inode_stack_top = (uint16_t)(top - 1);
    }
    return 0;
}


static void push_inode_stack(uint16_t ino)
{
    int top;

    if (g_super.s_inode_stack_top == INODE_STACK_EMPTY) {
        g_super.s_inode_free_stack[0] = ino;
        g_super.s_inode_stack_top = 0;
        return;
    }

    top = (int)g_super.s_inode_stack_top;
    if (top >= INODE_FREE_STACK_SIZE - 1) {
        return;
    }

    top++;
    g_super.s_inode_free_stack[top] = ino;
    g_super.s_inode_stack_top = (uint16_t)top;
}


static int rebuild_inode_stack_from_disk(void)
{
    uint16_t  ino;
    DiskINode dinode;

    if (g_super.s_inode_free_count == 0) {
        return 0;
    }
    if (g_super.s_inode_stack_top != INODE_STACK_EMPTY) {
        return 0;
    }

    for (ino = 2; ino < TOTAL_INODES; ino++) {
        if (read_disk_inode(ino, &dinode) != 0) {
            return -1;
        }
        if (dinode.d_mode != 0 || dinode.d_nlink != 0) {
            continue;
        }
        if (g_super.s_inode_stack_top == INODE_STACK_EMPTY) {
            g_super.s_inode_free_stack[0] = ino;
            g_super.s_inode_stack_top = 0;
        } else if (g_super.s_inode_stack_top < INODE_FREE_STACK_SIZE - 1) {
            push_inode_stack(ino);
        } else {
            break;
        }
    }

    return 0;
}



int fs_mount(const char *disk_path)
{
    if (disk_path == NULL || disk_path[0] == '\0') {
        return -1;
    }
    if (g_fs_mounted) {
        return -1;
    }

    if (disk_load(disk_path) != 0) {
        return -1;
    }

    if (read_block(SUPERBLOCK_BLOCKNO, &g_super) != 0) {
        disk_shutdown();
        return -1;
    }

    if (g_super.s_magic != VFS_MAGIC) {
        disk_shutdown();
        return -1;
    }

    inode_cache_reset();
    rebuild_inode_stack_from_disk();

    strncpy(g_disk_path, disk_path, sizeof(g_disk_path) - 1);
    g_disk_path[sizeof(g_disk_path) - 1] = '\0';
    g_super_dirty = 0;
    g_fs_mounted  = 1;
    return 0;
}

int fs_sync_superblock(void)
{
    if (!g_fs_mounted) {
        return -1;
    }
    if (!g_super_dirty) {
        return 0;
    }
    if (write_block(SUPERBLOCK_BLOCKNO, &g_super) != 0) {
        return -1;
    }
    g_super_dirty = 0;
    return 0;
}

int fs_sync_disk(void)
{
    int i;
    int rc = 0;

    if (!g_fs_mounted) {
        return -1;
    }

    
    for (i = 0; i < TOTAL_INODES; i++) {
        if (!g_inode_pool[i].in_use) {
            continue;
        }
        if (g_inode_pool[i].in.m_flags & MINODE_DIRTY) {
            if (write_disk_inode(g_inode_pool[i].in.m_inode_no,
                                 &g_inode_pool[i].in.m_dinode) != 0) {
                rc = -1;
            }
        }
    }

    
    if (fs_sync_superblock() != 0) {
        rc = -1;
    }

    
    if (disk_sync() != 0) {
        rc = -1;
    }

    return rc;
}

int fs_umount(void)
{
    int i;
    int rc = 0;

    if (!g_fs_mounted) {
        return -1;
    }

    
    for (i = 0; i < TOTAL_INODES; i++) {
        if (!g_inode_pool[i].in_use) {
            continue;
        }
        if (g_inode_pool[i].in.m_flags & MINODE_DIRTY) {
            if (write_disk_inode(g_inode_pool[i].in.m_inode_no,
                                 &g_inode_pool[i].in.m_dinode) != 0) {
                rc = -1;
            }
        }
    }

    if (fs_sync_superblock() != 0) {
        rc = -1;
    }

    if (g_disk_path[0] != '\0') {
        if (disk_save(g_disk_path) != 0) {
            rc = -1;
        }
    }

    inode_cache_reset();
    disk_shutdown();
    g_fs_mounted  = 0;
    g_super_dirty = 0;
    g_disk_path[0] = '\0';
    memset(&g_super, 0, sizeof(g_super));
    return rc;
}

const SuperBlock *fs_get_superblock(void)
{
    if (!g_fs_mounted) {
        return NULL;
    }
    return &g_super;
}



int balloc(void)
{
    uint16_t blk;

    if (!g_fs_mounted) {
        return -1;
    }
    if (g_super.s_block_free_count == 0) {
        return -1;
    }

    
    if (g_super.s_free_block_count == 0) {
        if (reload_free_block_stack() != 0) {
            return -1;
        }
    }

    
    if (maybe_reload_last_group_leader() != 0) {
        return -1;
    }

    if (g_super.s_free_block_count == 0) {
        return -1;
    }

    
    g_super.s_free_block_count--;
    blk = g_super.s_free_block_stack[g_super.s_free_block_count];
    g_super.s_block_free_count--;
    g_super_dirty = 1;
    return (int)blk;
}

int bfree(int blk)
{
    uint16_t reg_buf[BLOCK_SIZE / (int)sizeof(uint16_t)];
    uint16_t b;
    int      i;

    if (!g_fs_mounted) {
        return -1;
    }
    if (blk < DATA_ZONE_START || blk >= DATA_ZONE_START + DATA_ZONE_BLOCKS) {
        return -1;
    }

    b = (uint16_t)blk;

    if (g_super.s_free_block_count == MAX_FREE_BLOCKS) {
        
        memset(reg_buf, 0, sizeof(reg_buf));

        if (MAX_FREE_BLOCKS == 50) {
            
            reg_buf[0] = MAX_FREE_BLOCKS;
            for (i = 0; i < MAX_FREE_BLOCKS; i++) {
                reg_buf[i + 1] = g_super.s_free_block_stack[i];
            }
            reg_buf[MAX_FREE_BLOCKS + 1] = g_super.s_free_block_chain;
        }

        if (write_block((int)b, reg_buf) != 0) {
            return -1;
        }

        
        g_super.s_free_block_chain = b;
        g_super.s_free_block_count = 1;
        g_super.s_free_block_stack[0] = b;
    } else {
        
        g_super.s_free_block_stack[g_super.s_free_block_count] = b;
        g_super.s_free_block_count++;
    }

    g_super.s_block_free_count++;
    g_super_dirty = 1;
    return 0;
}



int ialloc(void)
{
    uint16_t  ino;
    DiskINode dinode;

    if (!g_fs_mounted) {
        return -1;
    }
    if (g_super.s_inode_free_count == 0) {
        return -1;
    }

    
    if (pop_inode_stack(&ino) != 0) {
        if (scan_free_inode(&ino) != 0) {
            return -1;
        }
    }

    if (read_disk_inode(ino, &dinode) != 0) {
        return -1;
    }
    if (dinode.d_mode != 0 || dinode.d_nlink != 0) {
        return -1;
    }

    memset(&dinode, 0, sizeof(dinode));
    if (write_disk_inode(ino, &dinode) != 0) {
        return -1;
    }

    g_super.s_inode_free_count--;
    g_super_dirty = 1;
    return (int)ino;
}

int ifree(uint16_t ino)
{
    DiskINode dinode;

    if (!g_fs_mounted) {
        return -1;
    }
    if (ino == 0 || ino == ROOT_INODE_NO || ino >= TOTAL_INODES) {
        return -1;
    }

    memset(&dinode, 0, sizeof(dinode));
    if (write_disk_inode(ino, &dinode) != 0) {
        return -1;
    }

    push_inode_stack(ino);
    g_super.s_inode_free_count++;
    g_super_dirty = 1;
    return 0;
}



MemINode *iget(uint16_t ino)
{
    IHashNode *node;

    if (!g_fs_mounted) {
        return NULL;
    }
    if (ino == 0 || ino >= TOTAL_INODES) {
        return NULL;
    }

    node = inode_hash_find(ino);
    if (node != NULL) {
        node->in.m_ref_count++;
        return &node->in;
    }

    node = inode_pool_alloc();
    if (node == NULL) {
        return NULL;
    }

    if (read_disk_inode(ino, &node->in.m_dinode) != 0) {
        node->in_use = 0;
        return NULL;
    }

    node->in.m_inode_no  = ino;
    node->in.m_ref_count = 1;
    node->in.m_flags     = 0;
    inode_hash_insert(node);
    return &node->in;
}

void iput(MemINode *ip)
{
    IHashNode *node;

    if (ip == NULL) {
        return;
    }

    node = inode_hash_find(ip->m_inode_no);
    if (node == NULL || &node->in != ip) {
        return;
    }

    if (ip->m_ref_count == 0) {
        return;
    }

    ip->m_ref_count--;

    if (ip->m_ref_count > 0) {
        return;
    }

    
    if (ip->m_flags & MINODE_DIRTY) {
        if (write_disk_inode(ip->m_inode_no, &ip->m_dinode) != 0) {
            return;
        }
        ip->m_flags &= (uint8_t)~MINODE_DIRTY;
    }

    
    if (ip->m_dinode.d_nlink == 0) {
        ifree(ip->m_inode_no);
    }

    inode_pool_free(node);
}

static IHashNode *inode_node_from_mem(MemINode *ip)
{
    IHashNode *node;

    if (ip == NULL) {
        return NULL;
    }
    node = inode_hash_find(ip->m_inode_no);
    if (node == NULL || &node->in != ip) {
        return NULL;
    }
    return node;
}

int inode_rdlock(MemINode *ip)
{
    IHashNode *node;

    node = inode_node_from_mem(ip);
    if (node == NULL || !node->rwlock_inited) {
        return -1;
    }
    return pthread_rwlock_rdlock(&node->rwlock);
}

int inode_wrlock(MemINode *ip)
{
    IHashNode *node;

    node = inode_node_from_mem(ip);
    if (node == NULL || !node->rwlock_inited) {
        return -1;
    }
    return pthread_rwlock_wrlock(&node->rwlock);
}

void inode_unlock(MemINode *ip)
{
    IHashNode *node;

    node = inode_node_from_mem(ip);
    if (node == NULL || !node->rwlock_inited) {
        return;
    }
    pthread_rwlock_unlock(&node->rwlock);
}
