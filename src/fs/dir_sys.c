

#include "fs/dir_sys.h"
#include "fs/allocator.h"
#include "fs/extent.h"
#include "fs/buf.h"
#include "fs/journal.h"

#include <stdio.h>
#include <string.h>


static User *g_cur_user = NULL;

void dir_bind_user(User *u)
{
    g_cur_user = u;
}

User *dir_get_user(void)
{
    return g_cur_user;
}


static int inode_is_dir(const MemINode *ip)
{
    if (ip == NULL) {
        return 0;
    }
    return (ip->m_dinode.d_mode & IFDIR) != 0;
}


static int dir_name_equal(const char *name, const DirEntry *de)
{
    int i;

    if (name == NULL || de == NULL) {
        return 0;
    }

    for (i = 0; i < MAX_FILENAME_LEN; i++) {
        char c = de->de_name[i];
        if (c == '\0') {
            return name[i] == '\0';
        }
        if (name[i] == '\0') {
            return 0;
        }
        if (name[i] != c) {
            return 0;
        }
    }
    return name[MAX_FILENAME_LEN] == '\0';
}


static void dir_name_copy(char *dst, size_t dst_size, const DirEntry *de)
{
    int i;

    if (dst == NULL || dst_size == 0 || de == NULL) {
        return;
    }

    for (i = 0; i < MAX_FILENAME_LEN && (size_t)i < dst_size - 1; i++) {
        dst[i] = de->de_name[i];
        if (de->de_name[i] == '\0') {
            return;
        }
    }
    dst[dst_size - 1] = '\0';
}


static int dir_name_valid(const char *name)
{
    size_t len;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    if (strchr(name, '/') != NULL) {
        return 0;
    }
    len = strlen(name);
    if (len > (size_t)MAX_FILENAME_LEN) {
        return 0;
    }
    return 1;
}


static int dir_lookup(const MemINode *dir_ip, const char *name, uint16_t *out_ino)
{
    uint32_t size;
    uint32_t pos;
    char     block_buf[BLOCK_SIZE];

    if (dir_ip == NULL || name == NULL || out_ino == NULL) {
        return -1;
    }
    if (!inode_is_dir(dir_ip)) {
        return -1;
    }

    size = dir_ip->m_dinode.d_size;
    for (pos = 0; pos + DIR_ENTRY_SIZE <= size; pos += DIR_ENTRY_SIZE) {
        uint32_t lblk = pos / (uint32_t)BLOCK_SIZE;
        uint32_t off  = pos % (uint32_t)BLOCK_SIZE;
        uint16_t phys;
        DirEntry *de;

        if (extent_lookup(&dir_ip->m_dinode, lblk, &phys) != 0) {
            return -1;
        }
        if (read_block((int)phys, block_buf) != 0) {
            return -1;
        }

        de = (DirEntry *)(block_buf + off);
        if (de->de_inode == 0) {
            continue;
        }
        if (dir_name_equal(name, de)) {
            *out_ino = de->de_inode;
            return 0;
        }
    }

    return -1;
}


static const char *path_skip_slash(const char *p)
{
    while (p != NULL && *p == '/') {
        p++;
    }
    return p;
}


static const char *path_next_component(const char *p, char *name)
{
    int i;

    p = path_skip_slash(p);
    if (p == NULL || *p == '\0') {
        if (name != NULL) {
            name[0] = '\0';
        }
        return p;
    }

    i = 0;
    while (p[i] != '\0' && p[i] != '/') {
        if (i >= MAX_FILENAME_LEN) {
            return NULL;
        }
        name[i] = p[i];
        i++;
    }
    name[i] = '\0';
    return p + i;
}


static int path_is_last_component(const char *rest)
{
    rest = path_skip_slash(rest);
    if (rest == NULL || *rest == '\0') {
        return 1;
    }
    return 0;
}


#define PATH_BUF_SIZE           256

static int path_split_parent(const char *path, char *parent, char *name)
{
    const char *last;
    size_t      plen;

    if (path == NULL || parent == NULL || name == NULL) {
        return -1;
    }

    last = strrchr(path, '/');
    if (last == NULL) {
        strncpy(parent, ".", PATH_BUF_SIZE - 1);
        parent[PATH_BUF_SIZE - 1] = '\0';
        strncpy(name, path, MAX_FILENAME_LEN);
        name[MAX_FILENAME_LEN] = '\0';
        return 0;
    }

    if (last == path) {
        
        strncpy(parent, "/", PATH_BUF_SIZE - 1);
        parent[PATH_BUF_SIZE - 1] = '\0';
        strncpy(name, last + 1, MAX_FILENAME_LEN);
        name[MAX_FILENAME_LEN] = '\0';
        return dir_name_valid(name) ? 0 : -1;
    }

    plen = (size_t)(last - path);
    if (plen >= PATH_BUF_SIZE) {
        return -1;
    }
    memcpy(parent, path, plen);
    parent[plen] = '\0';
    strncpy(name, last + 1, MAX_FILENAME_LEN);
    name[MAX_FILENAME_LEN] = '\0';
    return dir_name_valid(name) ? 0 : -1;
}

MemINode *namei(const char *path)
{
    MemINode   *ip;
    const char *p;
    char        comp[MAX_FILENAME_LEN + 1];
    uint16_t    start_ino;

    if (path == NULL || g_cur_user == NULL || fs_get_superblock() == NULL) {
        return NULL;
    }

    start_ino = (path[0] == '/') ? ROOT_INODE_NO : g_cur_user->u_cdir;
    ip = iget(start_ino);
    if (ip == NULL) {
        return NULL;
    }

    
    if (path[0] == '/') {
        p = path_skip_slash(path + 1);
        if (*p == '\0') {
            return ip;
        }
        p = path + 1;
    } else {
        p = path;
    }

    while (*p != '\0') {
        uint16_t next_ino;

        p = path_next_component(p, comp);
        if (p == NULL) {
            iput(ip);
            return NULL;
        }
        if (comp[0] == '\0') {
            break;
        }

        if (!inode_is_dir(ip)) {
            iput(ip);
            return NULL;
        }

        if (strcmp(comp, ".") == 0) {
            if (path_is_last_component(p)) {
                return ip;
            }
            p = path_skip_slash(p);
            continue;
        }

        if (strcmp(comp, "..") == 0) {
            MemINode *parent_ip;

            if (dir_lookup(ip, "..", &next_ino) != 0) {
                iput(ip);
                return NULL;
            }
            parent_ip = iget(next_ino);
            iput(ip);
            ip = parent_ip;
            if (ip == NULL) {
                return NULL;
            }
            if (path_is_last_component(p)) {
                return ip;
            }
            p = path_skip_slash(p);
            continue;
        }

        if (dir_lookup(ip, comp, &next_ino) != 0) {
            iput(ip);
            return NULL;
        }

        if (path_is_last_component(p)) {
            MemINode *target = iget(next_ino);
            iput(ip);
            return target;
        }

        {
            MemINode *next_ip = iget(next_ino);
            iput(ip);
            ip = next_ip;
            if (ip == NULL) {
                return NULL;
            }
        }

        p = path_skip_slash(p);
    }

    return ip;
}


static int dir_add_entry(MemINode *dir_ip, const char *name, uint16_t ino)
{
    uint32_t size;
    uint32_t pos;
    char     block_buf[BLOCK_SIZE];
    int      new_block = 0;

    if (dir_ip == NULL || !dir_name_valid(name)) {
        return -1;
    }
    if (!inode_is_dir(dir_ip)) {
        return -1;
    }

    size = dir_ip->m_dinode.d_size;

    
    for (pos = 0; pos + DIR_ENTRY_SIZE <= size; pos += DIR_ENTRY_SIZE) {
        uint32_t lblk = pos / (uint32_t)BLOCK_SIZE;
        uint32_t off  = pos % (uint32_t)BLOCK_SIZE;
        uint16_t phys;
        DirEntry *de;

        if (extent_lookup(&dir_ip->m_dinode, lblk, &phys) != 0) {
            return -1;
        }
        if (read_block((int)phys, block_buf) != 0) {
            return -1;
        }

        de = (DirEntry *)(block_buf + off);
        if (de->de_inode != 0) {
            if (dir_name_equal(name, de)) {
                return -1;
            }
            continue;
        }

        de->de_inode = ino;
        memset(de->de_name, 0, MAX_FILENAME_LEN);
        strncpy(de->de_name, name, MAX_FILENAME_LEN);
        if (journal_write_dir_block((int)phys, block_buf) != 0) {
            return -1;
        }
        dir_ip->m_flags |= MINODE_DIRTY;
        return 0;
    }

    
    {
        uint32_t lblk = size / (uint32_t)BLOCK_SIZE;
        uint32_t off  = size % (uint32_t)BLOCK_SIZE;
        uint16_t phys;
        DirEntry *de;

        if (off == 0 && size > 0) {
            if (extent_bmap(dir_ip, lblk, 1, &phys) != 0) {
                return -1;
            }
            new_block = 1;
            memset(block_buf, 0, sizeof(block_buf));
        } else {
            if (extent_lookup(&dir_ip->m_dinode, lblk, &phys) != 0) {
                return -1;
            }
            if (read_block((int)phys, block_buf) != 0) {
                return -1;
            }
        }

        de = (DirEntry *)(block_buf + off);
        de->de_inode = ino;
        memset(de->de_name, 0, MAX_FILENAME_LEN);
        strncpy(de->de_name, name, MAX_FILENAME_LEN);

        if (journal_write_dir_block((int)phys, block_buf) != 0) {
            return -1;
        }

        dir_ip->m_dinode.d_size += DIR_ENTRY_SIZE;
        dir_ip->m_flags |= MINODE_DIRTY;
    }

    return 0;
}

int dir_split_path(const char *path, char *parent, char *name)
{
    return path_split_parent(path, parent, name);
}

int dir_link_entry(MemINode *dir_ip, const char *name, uint16_t ino)
{
    return dir_add_entry(dir_ip, name, ino);
}


int dir_unlink_entry(MemINode *dir_ip, const char *name, uint16_t *out_ino)
{
    uint32_t size;
    uint32_t pos;
    char     block_buf[BLOCK_SIZE];

    if (dir_ip == NULL || name == NULL || out_ino == NULL) {
        return -1;
    }
    if (!dir_name_valid(name)) {
        return -1;
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return -1;
    }
    if (!inode_is_dir(dir_ip)) {
        return -1;
    }

    size = dir_ip->m_dinode.d_size;
    for (pos = 0; pos + DIR_ENTRY_SIZE <= size; pos += DIR_ENTRY_SIZE) {
        uint32_t lblk = pos / (uint32_t)BLOCK_SIZE;
        uint32_t off  = pos % (uint32_t)BLOCK_SIZE;
        uint16_t phys;
        DirEntry *de;

        if (extent_lookup(&dir_ip->m_dinode, lblk, &phys) != 0) {
            return -1;
        }
        if (read_block((int)phys, block_buf) != 0) {
            return -1;
        }

        de = (DirEntry *)(block_buf + off);
        if (de->de_inode == 0) {
            continue;
        }
        if (!dir_name_equal(name, de)) {
            continue;
        }

        *out_ino = de->de_inode;
        de->de_inode = 0;
        memset(de->de_name, 0, MAX_FILENAME_LEN);
        if (journal_write_dir_block((int)phys, block_buf) != 0) {
            return -1;
        }
        dir_ip->m_flags |= MINODE_DIRTY;
        return 0;
    }

    return -1;
}


static int dir_init_dots(MemINode *dir_ip, uint16_t parent_ino, int dir_blk)
{
    char     block_buf[BLOCK_SIZE];
    DirEntry *de;

    if (dir_ip == NULL || dir_blk < 0) {
        return -1;
    }

    memset(block_buf, 0, sizeof(block_buf));
    de = (DirEntry *)block_buf;

    de[0].de_inode = dir_ip->m_inode_no;
    strncpy(de[0].de_name, ".", MAX_FILENAME_LEN);

    de[1].de_inode = parent_ino;
    strncpy(de[1].de_name, "..", MAX_FILENAME_LEN);

    if (journal_write_dir_block(dir_blk, block_buf) != 0) {
        return -1;
    }

    if (extent_set_single(dir_ip, 0, (uint16_t)dir_blk, 1) != 0) {
        return -1;
    }
    dir_ip->m_dinode.d_size = 2 * DIR_ENTRY_SIZE;
    dir_ip->m_dinode.d_nlink = 2;
    dir_ip->m_flags |= MINODE_DIRTY;
    return 0;
}

int upfs_mkdir(const char *path, uint16_t mode)
{
    char        parent_path[PATH_BUF_SIZE];
    char        name[MAX_FILENAME_LEN + 1];
    MemINode   *parent;
    MemINode   *child;
    int         ino;
    int         blk;
    uint16_t    existing;

    if (path == NULL || g_cur_user == NULL || fs_get_superblock() == NULL) {
        return -1;
    }
    if (path_split_parent(path, parent_path, name) != 0) {
        return -1;
    }

    parent = namei(parent_path);
    if (parent == NULL) {
        return -1;
    }
    if (!inode_is_dir(parent)) {
        iput(parent);
        return -1;
    }

    if (dir_lookup(parent, name, &existing) == 0) {
        iput(parent);
        return -1;
    }

    ino = ialloc_for(parent->m_inode_no);
    if (ino < 0) {
        iput(parent);
        return -1;
    }

    blk = balloc_for((uint16_t)ino);
    if (blk < 0) {
        ifree((uint16_t)ino);
        iput(parent);
        return -1;
    }

    child = iget((uint16_t)ino);
    if (child == NULL) {
        bfree(blk);
        ifree((uint16_t)ino);
        iput(parent);
        return -1;
    }

    memset(&child->m_dinode, 0, sizeof(child->m_dinode));
    child->m_dinode.d_mode = (uint16_t)((mode & 0777) | IFDIR);
    child->m_dinode.d_uid  = g_cur_user->u_uid;
    child->m_dinode.d_gid  = g_cur_user->u_gid;
    child->m_dinode.d_nlink = 2;
    child->m_flags |= MINODE_DIRTY;

    if (dir_init_dots(child, parent->m_inode_no, blk) != 0) {
        iput(child);
        bfree(blk);
        ifree((uint16_t)ino);
        iput(parent);
        return -1;
    }

    if (dir_add_entry(parent, name, (uint16_t)ino) != 0) {
        iput(child);
        bfree(blk);
        ifree((uint16_t)ino);
        iput(parent);
        return -1;
    }

    
    parent->m_dinode.d_nlink++;
    parent->m_flags |= MINODE_DIRTY;

    iput(child);
    iput(parent);
    return 0;
}

int chdir(const char *path)
{
    MemINode *ip;

    if (path == NULL || g_cur_user == NULL) {
        return -1;
    }

    ip = namei(path);
    if (ip == NULL) {
        return -1;
    }
    if (!inode_is_dir(ip)) {
        iput(ip);
        return -1;
    }

    g_cur_user->u_cdir = ip->m_inode_no;
    iput(ip);
    return 0;
}


static char mode_type_char(uint16_t mode)
{
    if (mode & IFDIR) {
        return 'd';
    }
    if (mode & IFREG) {
        return '-';
    }
    return '?';
}

int dir_list(const char *path)
{
    MemINode   *dir_ip;
    MemINode   *entry_ip;
    uint32_t    size;
    uint32_t    pos;
    char        block_buf[BLOCK_SIZE];
    char        name_buf[MAX_FILENAME_LEN + 1];

    if (g_cur_user == NULL || fs_get_superblock() == NULL) {
        return -1;
    }

    if (path == NULL || path[0] == '\0') {
        dir_ip = iget(g_cur_user->u_cdir);
    } else {
        dir_ip = namei(path);
    }

    if (dir_ip == NULL) {
        return -1;
    }
    if (!inode_is_dir(dir_ip)) {
        iput(dir_ip);
        return -1;
    }

    printf("inode  type mode     size  name\n");
    printf("-----  ---- ------ --------  ----\n");

    size = dir_ip->m_dinode.d_size;
    for (pos = 0; pos + DIR_ENTRY_SIZE <= size; pos += DIR_ENTRY_SIZE) {
        uint32_t lblk = pos / (uint32_t)BLOCK_SIZE;
        uint32_t off  = pos % (uint32_t)BLOCK_SIZE;
        uint16_t phys;
        DirEntry *de;
        uint16_t  ino;

        if (extent_lookup(&dir_ip->m_dinode, lblk, &phys) != 0) {
            break;
        }
        if (read_block((int)phys, block_buf) != 0) {
            iput(dir_ip);
            return -1;
        }

        de = (DirEntry *)(block_buf + off);
        ino = de->de_inode;
        if (ino == 0) {
            continue;
        }

        dir_name_copy(name_buf, sizeof(name_buf), de);

        entry_ip = iget(ino);
        if (entry_ip == NULL) {
            printf("%5u  %c    %06o %8u  %s (iget failed)\n",
                   (unsigned)ino, '?', 0U, 0U, name_buf);
            continue;
        }

        printf("%5u  %c    %06o %8u  %s\n",
               (unsigned)entry_ip->m_inode_no,
               mode_type_char(entry_ip->m_dinode.d_mode),
               (unsigned)(entry_ip->m_dinode.d_mode & 0777),
               (unsigned)entry_ip->m_dinode.d_size,
               name_buf);

        iput(entry_ip);
    }

    iput(dir_ip);
    return 0;
}
