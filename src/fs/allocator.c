

#include "fs/allocator.h"
#include "fs/bg.h"
#include "fs/inomap.h"
#include "fs/disk_io.h"
#include "fs/buf.h"
#include "fs/journal.h"

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


static int           g_fs_mounted = 0;


static SuperBlock    g_super;


static int           g_super_dirty = 0;


static char          g_disk_path[512];


static IHashNode    *g_inode_hash[MINODE_HASH_SIZE];


static IHashNode     g_inode_pool[INODE_CACHE_SIZE];



static int read_disk_inode(uint16_t ino, DiskINode *out)
{
    if (out == NULL || ino == 0)
        return -1;
    return inomap_read_disk_inode((uint32_t)ino, out);
}

static int write_disk_inode(uint16_t ino, const DiskINode *inode)
{
    if (inode == NULL || ino == 0)
        return -1;
    return inomap_write_disk_inode((uint32_t)ino, inode);
}



static uint32_t inode_hash_key(uint16_t ino)
{
    return (uint32_t)(ino % MINODE_HASH_SIZE);
}

static IHashNode *inode_pool_alloc(void)
{
    int i;

    for (i = 0; i < INODE_CACHE_SIZE; i++) {
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

    for (i = 0; i < INODE_CACHE_SIZE; i++) {
        if (g_inode_pool[i].in_use && g_inode_pool[i].rwlock_inited) {
            pthread_rwlock_destroy(&g_inode_pool[i].rwlock);
        }
    }
    memset(g_inode_hash, 0, sizeof(g_inode_hash));
    memset(g_inode_pool, 0, sizeof(g_inode_pool));
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

    if (bg_init_from_super(&g_super) != 0) {
        disk_shutdown();
        return -1;
    }

    if (inomap_load(&g_super) != 0) {
        disk_shutdown();
        return -1;
    }

    inode_cache_reset();

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
    if (bg_fill_super(&g_super) != 0) {
        return -1;
    }
    if (bg_sync() != 0) {
        return -1;
    }
    if (journal_write_metadata(SUPERBLOCK_BLOCKNO, &g_super) != 0) {
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

    bflush_all();

    for (i = 0; i < INODE_CACHE_SIZE; i++) {
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

    bflush_all();

    if (fs_sync_superblock() != 0) {
        rc = -1;
    }

    bflush_all();

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

    
    for (i = 0; i < INODE_CACHE_SIZE; i++) {
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

    bg_sync();

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

int fs_reload_super(void)
{
    if (!g_fs_mounted)
        return -1;

    bcache_invalidate();

    if (read_block(SUPERBLOCK_BLOCKNO, &g_super) != 0)
        return -1;
    if (bg_init_from_super(&g_super) != 0)
        return -1;
    if (inomap_load(&g_super) != 0)
        return -1;

    inode_cache_reset();
    g_super_dirty = 0;
    return 0;
}

const SuperBlock *fs_get_superblock(void)
{
    if (!g_fs_mounted) {
        return NULL;
    }
    return &g_super;
}

// ---------- 调试输出 ----------

void fs_debug_print_super(void)
{
    if (!g_fs_mounted) {
        printf("  Filesystem not mounted.\n");
        return;
    }
    const SuperBlock *sb = &g_super;

    printf("\n");
    printf("  ── Disk SuperBlock ──────────────────────────────\n");
    printf("  Magic:              0x%08X  %s\n", sb->s_magic,
           sb->s_magic == VFS_MAGIC ? "(valid)" : "(INVALID)");
    printf("  Inodes total:       %u\n", sb->s_inode_total);
    printf("  Inodes free:        %u\n", sb->s_inode_free_count);
    printf("  Blocks total:       %u\n", sb->s_block_total);
    printf("  Blocks free:        %u\n", sb->s_block_free_count);
    printf("  Block groups:       %u\n", (unsigned)sb->s_bg_count);
    printf("  Next inode:         %u\n", (unsigned)sb->s_inode_next);
    printf("\n");
    bg_debug_print();
    inomap_debug_print();
}

void fs_debug_print_inodes(void)
{
    if (!g_fs_mounted) {
        printf("  Filesystem not mounted.\n");
        return;
    }

    int in_use = 0, dirty = 0, cached = 0;
    for (int i = 0; i < INODE_CACHE_SIZE; i++) {
        if (g_inode_pool[i].in_use) { in_use++; cached++; }
        if (g_inode_pool[i].in.m_flags & MINODE_DIRTY) dirty++;
    }
    for (int i = 0; i < MINODE_HASH_SIZE; i++) {
        for (IHashNode *p = g_inode_hash[i]; p; p = p->h_next) {
            /* already counted */
        }
    }

    printf("\n");
    printf("  ── Inode Cache ──────────────────────────────────\n");
    printf("  Pool capacity:      %d\n", INODE_CACHE_SIZE);
    printf("  Cached (in-use):    %d\n", in_use);
    printf("  Dirty (pending):    %d\n", dirty);
    printf("  Hash buckets:       %d\n", MINODE_HASH_SIZE);

    /* 列出前 32 个在用的 i 节点 */
    int shown = 0;
    printf("\n  ── Active Inodes (first 32) ─────────────────────\n");
    printf("  %-6s %-6s %-8s %-4s %s\n", "Ino", "Mode", "Size", "Ref", "Flags");
    for (int i = 0; i < INODE_CACHE_SIZE && shown < 32; i++) {
        if (!g_inode_pool[i].in_use) continue;
        MemINode *ip = &g_inode_pool[i].in;
        const char *typ = (ip->m_dinode.d_mode & IFDIR) ? "DIR" :
                          (ip->m_dinode.d_mode & IFREG) ? "REG" : "?";
        printf("  %-6u %-6s %-8u %-4u %s%s\n",
               ip->m_inode_no, typ, ip->m_dinode.d_size,
               ip->m_ref_count,
               (ip->m_flags & MINODE_DIRTY) ? "D" : "-",
               (ip->m_flags & MINODE_LOCKED) ? "L" : "-");
        shown++;
    }
    if (in_use > shown) printf("  ... (%d more)\n", in_use - shown);
    printf("\n");
}



int balloc_for(uint16_t ino_hint)
{
    int blk;

    if (!g_fs_mounted) {
        return -1;
    }

    blk = bg_balloc_for(ino_hint);
    if (blk < 0) {
        return -1;
    }

    g_super_dirty = 1;
    return blk;
}

int balloc(void)
{
    return balloc_for(0);
}

int bfree(int blk)
{
    if (!g_fs_mounted) {
        return -1;
    }
    if (bg_bfree(blk) != 0) {
        return -1;
    }
    g_super_dirty = 1;
    return 0;
}

int ialloc_for(uint16_t parent_ino)
{
    int ino;

    if (!g_fs_mounted) {
        return -1;
    }

    ino = inomap_ialloc_for((uint32_t)parent_ino);
    if (ino < 0) {
        return -1;
    }

    g_super_dirty = 1;
    return ino;
}

int ialloc(void)
{
    return ialloc_for(0);
}

int ifree(uint16_t ino)
{
    if (!g_fs_mounted) {
        return -1;
    }
    if (ino == 0 || ino == ROOT_INODE_NO) {
        return -1;
    }

    if (inomap_ifree((uint32_t)ino) != 0) {
        return -1;
    }

    g_super_dirty = 1;
    return 0;
}


/*
@brief 从磁盘读取指定i节点号的i节点数据到内存，并返回对应的内存i节点结构体指针
@param ino i节点号，必须大于0
@return 成功返回指向内存i节点的指针，失败返回NULL
*/
MemINode *iget(uint16_t ino)
{
    IHashNode *node;

    if (!g_fs_mounted) {
        return NULL;
    }
    if (ino == 0) {
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

// 释放内存i节点，如果i节点被修改过则写回磁盘，如果i节点的链接数为0则释放对应的i节点号
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
