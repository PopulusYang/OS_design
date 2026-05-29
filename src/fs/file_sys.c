

#include "fs/file_sys.h"
#include "fs/dir_sys.h"
#include "fs/allocator.h"
#include "fs/disk_io.h"

#include <string.h>
#include <stdio.h>

#define PATH_BUF_SIZE           256


static SysOpenFile g_sys_ofile[SYS_OPEN_FILE_MAX];
static int         g_sys_ofile_init = 0;

void sys_open_file_init(void)
{
    memset(g_sys_ofile, 0, sizeof(g_sys_ofile));
    g_sys_ofile_init = 1;
}

int sys_open_file_count(void)
{
    int count = 0;
    for (int i = 0; i < SYS_OPEN_FILE_MAX; i++) {
        if (g_sys_ofile[i].f_inode != NULL) count++;
    }
    return count;
}

const SysOpenFile *sys_open_file_table(void)
{
    return g_sys_ofile;
}

static int sys_ofile_alloc(void)
{
    if (!g_sys_ofile_init) sys_open_file_init();
    for (int i = 0; i < SYS_OPEN_FILE_MAX; i++) {
        if (g_sys_ofile[i].f_inode == NULL) return i;
    }
    return -1;
}

static void sys_ofile_free(int idx)
{
    if (idx < 0 || idx >= SYS_OPEN_FILE_MAX) return;
    memset(&g_sys_ofile[idx], 0, sizeof(g_sys_ofile[idx]));
}


static int inode_is_reg(const MemINode *ip)
{
    if (ip == NULL) {
        return 0;
    }
    return (ip->m_dinode.d_mode & IFREG) != 0;
}


static int alloc_fd(User *u)
{
    int i;

    if (u == NULL) {
        return -1;
    }
    for (i = 0; i < MAX_OPEN_FILES; i++) {
        if (u->u_ofile[i].oft_inode == NULL) {
            return i;
        }
    }
    return -1;
}


static OpenFileTable *oft_get(User *u, int fd)
{
    if (u == NULL || fd < 0 || fd >= MAX_OPEN_FILES) {
        return NULL;
    }
    if (u->u_ofile[fd].oft_inode == NULL) {
        return NULL;
    }
    return &u->u_ofile[fd];
}


static int index_get(uint16_t idx_blk, uint32_t slot, uint16_t *val_out)
{
    uint16_t buf[BLOCK_SIZE / (int)sizeof(uint16_t)];

    if (val_out == NULL) {
        return -1;
    }
    if (read_block((int)idx_blk, buf) != 0) {
        return -1;
    }
    if (slot >= (uint32_t)ADDRS_PER_BLOCK) {
        return -1;
    }
    *val_out = buf[slot];
    return 0;
}


static int index_set(uint16_t idx_blk, uint32_t slot, uint16_t val)
{
    uint16_t buf[BLOCK_SIZE / (int)sizeof(uint16_t)];

    if (read_block((int)idx_blk, buf) != 0) {
        return -1;
    }
    if (slot >= (uint32_t)ADDRS_PER_BLOCK) {
        return -1;
    }
    buf[slot] = val;
    return write_block((int)idx_blk, buf);
}


static int alloc_index_block(uint16_t *idx_out)
{
    int blk;

    blk = balloc();
    if (blk < 0) {
        return -1;
    }
    {
        char zero[BLOCK_SIZE];
        memset(zero, 0, sizeof(zero));
        if (write_block(blk, zero) != 0) {
            bfree(blk);
            return -1;
        }
    }
    *idx_out = (uint16_t)blk;
    return 0;
}







static int file_bmap(MemINode *ip, uint32_t logical_blk, int create_flag, uint16_t *phys_out)
{
    DiskINode *d;
    uint16_t   phys;

    if (ip == NULL || phys_out == NULL) {
        return -1;
    }

    d = &ip->m_dinode;

    
    if (logical_blk < 8) {
        phys = d->d_direct[logical_blk];
        if (phys == 0) {
            if (!create_flag) {
                return -1;
            }
            {
                int blk = balloc();
                if (blk < 0) {
                    return -1;
                }
                phys = (uint16_t)blk;
            }
            d->d_direct[logical_blk] = phys;
            ip->m_flags |= MINODE_DIRTY;
        }
        *phys_out = phys;
        return 0;
    }

    
    if (logical_blk < 8U + (uint32_t)ADDRS_PER_BLOCK) {
        uint32_t slot = logical_blk - 8U;

        if (d->d_sindirect == 0) {
            if (!create_flag) {
                return -1;
            }
            if (alloc_index_block(&d->d_sindirect) != 0) {
                return -1;
            }
            ip->m_flags |= MINODE_DIRTY;
        }

        if (index_get(d->d_sindirect, slot, &phys) != 0) {
            return -1;
        }
        if (phys == 0) {
            if (!create_flag) {
                return -1;
            }
            {
                int blk = balloc();
                if (blk < 0) {
                    return -1;
                }
                phys = (uint16_t)blk;
            }
            if (index_set(d->d_sindirect, slot, phys) != 0) {
                bfree((int)phys);
                return -1;
            }
            ip->m_flags |= MINODE_DIRTY;
        }
        *phys_out = phys;
        return 0;
    }

    
    {
        uint32_t off  = logical_blk - 8U - (uint32_t)ADDRS_PER_BLOCK;
        uint32_t idx1 = off / (uint32_t)ADDRS_PER_BLOCK;
        uint32_t idx2 = off % (uint32_t)ADDRS_PER_BLOCK;
        uint16_t l1_blk;
        uint16_t l0_blk;

        if (idx1 >= (uint32_t)ADDRS_PER_BLOCK) {
            return -1;
        }

        if (d->d_dindirect == 0) {
            if (!create_flag) {
                return -1;
            }
            if (alloc_index_block(&d->d_dindirect) != 0) {
                return -1;
            }
            ip->m_flags |= MINODE_DIRTY;
        }

        if (index_get(d->d_dindirect, idx1, &l1_blk) != 0) {
            return -1;
        }
        if (l1_blk == 0) {
            if (!create_flag) {
                return -1;
            }
            if (alloc_index_block(&l1_blk) != 0) {
                return -1;
            }
            if (index_set(d->d_dindirect, idx1, l1_blk) != 0) {
                bfree((int)l1_blk);
                return -1;
            }
            ip->m_flags |= MINODE_DIRTY;
        }

        if (index_get(l1_blk, idx2, &l0_blk) != 0) {
            return -1;
        }
        if (l0_blk == 0) {
            if (!create_flag) {
                return -1;
            }
            {
                int blk = balloc();
                if (blk < 0) {
                    return -1;
                }
                l0_blk = (uint16_t)blk;
            }
            if (index_set(l1_blk, idx2, l0_blk) != 0) {
                bfree((int)l0_blk);
                return -1;
            }
            ip->m_flags |= MINODE_DIRTY;
        }

        *phys_out = l0_blk;
        return 0;
    }
}


static void free_index_block(uint16_t idx_blk, int free_self)
{
    uint16_t buf[BLOCK_SIZE / (int)sizeof(uint16_t)];
    int      i;

    if (idx_blk == 0) {
        return;
    }
    if (read_block((int)idx_blk, buf) != 0) {
        return;
    }
    for (i = 0; i < ADDRS_PER_BLOCK; i++) {
        if (buf[i] != 0) {
            bfree((int)buf[i]);
        }
    }
    if (free_self) {
        bfree((int)idx_blk);
    }
}


static void free_dindirect(uint16_t dind_blk)
{
    uint16_t l1[BLOCK_SIZE / (int)sizeof(uint16_t)];
    int      i;

    if (dind_blk == 0) {
        return;
    }
    if (read_block((int)dind_blk, l1) != 0) {
        return;
    }
    for (i = 0; i < ADDRS_PER_BLOCK; i++) {
        if (l1[i] != 0) {
            free_index_block(l1[i], 1);
        }
    }
    bfree((int)dind_blk);
}


static void file_truncate(MemINode *ip)
{
    DiskINode *d;
    int        i;

    if (ip == NULL) {
        return;
    }

    d = &ip->m_dinode;

    for (i = 0; i < 8; i++) {
        if (d->d_direct[i] != 0) {
            bfree((int)d->d_direct[i]);
            d->d_direct[i] = 0;
        }
    }

    if (d->d_sindirect != 0) {
        free_index_block(d->d_sindirect, 1);
        d->d_sindirect = 0;
    }

    if (d->d_dindirect != 0) {
        free_dindirect(d->d_dindirect);
        d->d_dindirect = 0;
    }

    d->d_size = 0;
    ip->m_flags |= MINODE_DIRTY;
}

int vfs_create(const char *path, uint16_t mode)
{
    User       *u;
    char        parent_path[PATH_BUF_SIZE];
    char        name[MAX_FILENAME_LEN + 1];
    MemINode   *parent_ip;
    MemINode   *file_ip;
    int         ino;

    u = dir_get_user();
    if (path == NULL || u == NULL || fs_get_superblock() == NULL) {
        return -1;
    }
    if (dir_split_path(path, parent_path, name) != 0) {
        return -1;
    }

    
    {
        MemINode *exist = namei(path);
        if (exist != NULL) {
            iput(exist);
            return -1;
        }
    }

    parent_ip = namei(parent_path);
    if (parent_ip == NULL) {
        return -1;
    }
    if ((parent_ip->m_dinode.d_mode & IFDIR) == 0) {
        iput(parent_ip);
        return -1;
    }

    ino = ialloc();
    if (ino < 0) {
        iput(parent_ip);
        return -1;
    }

    file_ip = iget((uint16_t)ino);
    if (file_ip == NULL) {
        ifree((uint16_t)ino);
        iput(parent_ip);
        return -1;
    }

    memset(&file_ip->m_dinode, 0, sizeof(file_ip->m_dinode));
    file_ip->m_dinode.d_mode  = (uint16_t)((mode & 0777U) | IFREG);
    file_ip->m_dinode.d_nlink = 1;
    file_ip->m_dinode.d_uid   = u->u_uid;
    file_ip->m_dinode.d_gid   = u->u_gid;
    file_ip->m_dinode.d_size  = 0;
    file_ip->m_flags |= MINODE_DIRTY;

    if (dir_link_entry(parent_ip, name, (uint16_t)ino) != 0) {
        file_ip->m_dinode.d_nlink = 0;
        iput(file_ip);
        iput(parent_ip);
        return -1;
    }

    iput(file_ip);
    iput(parent_ip);
    return 0;
}

int vfs_open(const char *path, uint16_t mode)
{
    User          *u;
    MemINode      *ip;
    OpenFileTable *oft;
    int            fd;
    int            sfd;
    uint16_t       lock_flags = 0;

    u = dir_get_user();
    if (path == NULL || u == NULL || fs_get_superblock() == NULL) {
        return -1;
    }

    ip = namei(path);
    if (ip == NULL) {
        return -1;
    }
    if (!inode_is_reg(ip)) {
        iput(ip);
        return -1;
    }

    if (mode & (O_WRONLY | O_RDWR)) {
        if (inode_wrlock(ip) != 0) {
            iput(ip);
            return -1;
        }
        lock_flags = OF_WRLOCKED;
    } else if (mode & O_RDONLY) {
        if (inode_rdlock(ip) != 0) {
            iput(ip);
            return -1;
        }
        lock_flags = OF_RDLOCKED;
    } else {
        iput(ip);
        return -1;
    }

    fd = alloc_fd(u);
    if (fd < 0) {
        inode_unlock(ip);
        iput(ip);
        return -1;
    }

    sfd = sys_ofile_alloc();
    if (sfd < 0) {
        inode_unlock(ip);
        iput(ip);
        return -1;
    }
    g_sys_ofile[sfd].f_inode  = ip;
    g_sys_ofile[sfd].f_mode   = mode;
    g_sys_ofile[sfd].f_flags  = lock_flags;
    g_sys_ofile[sfd].f_count  = 1;
    g_sys_ofile[sfd].f_offset = (mode & O_APPEND) ? ip->m_dinode.d_size : 0;

    oft = &u->u_ofile[fd];
    oft->oft_inode     = ip;
    oft->oft_mode      = mode;
    oft->oft_flags     = lock_flags;
    oft->oft_read_pos  = 0;
    oft->oft_write_pos = (mode & O_APPEND) ? ip->m_dinode.d_size : 0;
    return fd;
}

int vfs_read(int fd, void *buf, int count)
{
    User          *u;
    OpenFileTable *oft;
    MemINode      *ip;
    char          *dst;
    char           block_buf[BLOCK_SIZE];
    int            total;

    u = dir_get_user();
    if (buf == NULL || count < 0) {
        return -1;
    }
    if (count == 0) {
        return 0;
    }

    oft = oft_get(u, fd);
    if (oft == NULL) {
        return -1;
    }
    if (!(oft->oft_mode & (O_RDONLY | O_RDWR))) {
        return -1;
    }

    ip  = oft->oft_inode;
    dst = (char *)buf;
    total = 0;

    while (total < count) {
        uint32_t file_pos;
        uint32_t lblk;
        uint32_t off;
        uint16_t phys;
        int      chunk;

        file_pos = oft->oft_read_pos;
        if (file_pos >= ip->m_dinode.d_size) {
            break;
        }

        lblk = file_pos / (uint32_t)BLOCK_SIZE;
        off  = file_pos % (uint32_t)BLOCK_SIZE;

        if (file_bmap(ip, lblk, 0, &phys) != 0) {
            break;
        }
        if (read_block((int)phys, block_buf) != 0) {
            return (total > 0) ? total : -1;
        }

        chunk = (int)(BLOCK_SIZE - off);
        if (chunk > count - total) {
            chunk = count - total;
        }
        if (file_pos + (uint32_t)chunk > ip->m_dinode.d_size) {
            chunk = (int)(ip->m_dinode.d_size - file_pos);
        }

        memcpy(dst + total, block_buf + off, (size_t)chunk);
        total += chunk;
        oft->oft_read_pos += (uint32_t)chunk;
    }

    return total;
}

int vfs_write(int fd, const void *buf, int count)
{
    User          *u;
    OpenFileTable *oft;
    MemINode      *ip;
    const char    *src;
    char           block_buf[BLOCK_SIZE];
    int            total;

    u = dir_get_user();
    if (buf == NULL || count < 0) {
        return -1;
    }
    if (count == 0) {
        return 0;
    }

    oft = oft_get(u, fd);
    if (oft == NULL) {
        return -1;
    }
    if (!(oft->oft_mode & (O_WRONLY | O_RDWR))) {
        return -1;
    }

    ip  = oft->oft_inode;
    src = (const char *)buf;
    total = 0;

    while (total < count) {
        uint32_t lblk;
        uint32_t off;
        uint16_t phys;
        int      chunk;

        lblk = oft->oft_write_pos / (uint32_t)BLOCK_SIZE;
        off  = oft->oft_write_pos % (uint32_t)BLOCK_SIZE;

        
        if (file_bmap(ip, lblk, 1, &phys) != 0) {
            return (total > 0) ? total : -1;
        }

        if (read_block((int)phys, block_buf) != 0) {
            return (total > 0) ? total : -1;
        }

        chunk = (int)(BLOCK_SIZE - off);
        if (chunk > count - total) {
            chunk = count - total;
        }

        memcpy(block_buf + off, src + total, (size_t)chunk);
        if (write_block((int)phys, block_buf) != 0) {
            return (total > 0) ? total : -1;
        }

        total += chunk;
        oft->oft_write_pos += (uint32_t)chunk;

        if (oft->oft_write_pos > ip->m_dinode.d_size) {
            ip->m_dinode.d_size = oft->oft_write_pos;
            ip->m_flags |= MINODE_DIRTY;
        }
    }

    return total;
}

int vfs_close(int fd)
{
    User          *u;
    OpenFileTable *oft;
    MemINode      *ip;

    u = dir_get_user();
    oft = oft_get(u, fd);
    if (oft == NULL) {
        return -1;
    }

    ip = oft->oft_inode;

    for (int i = 0; i < SYS_OPEN_FILE_MAX; i++) {
        if (g_sys_ofile[i].f_inode == ip) {
            g_sys_ofile[i].f_count--;
            if (g_sys_ofile[i].f_count == 0)
                sys_ofile_free(i);
            break;
        }
    }

    if (oft->oft_flags & (OF_RDLOCKED | OF_WRLOCKED)) {
        inode_unlock(ip);
    }

    iput(ip);

    oft->oft_inode     = NULL;
    oft->oft_read_pos  = 0;
    oft->oft_write_pos = 0;
    oft->oft_mode      = 0;
    oft->oft_flags     = 0;
    return 0;
}

int vfs_delete(const char *path)
{
    char        parent_path[PATH_BUF_SIZE];
    char        name[MAX_FILENAME_LEN + 1];
    MemINode   *parent_ip;
    MemINode   *file_ip;
    uint16_t    ino;

    if (path == NULL || dir_get_user() == NULL || fs_get_superblock() == NULL) {
        return -1;
    }
    if (dir_split_path(path, parent_path, name) != 0) {
        return -1;
    }

    parent_ip = namei(parent_path);
    if (parent_ip == NULL) {
        return -1;
    }
    if ((parent_ip->m_dinode.d_mode & IFDIR) == 0) {
        iput(parent_ip);
        return -1;
    }

    if (dir_unlink_entry(parent_ip, name, &ino) != 0) {
        iput(parent_ip);
        return -1;
    }

    file_ip = iget(ino);
    if (file_ip == NULL) {
        iput(parent_ip);
        return -1;
    }

    if (!inode_is_reg(file_ip)) {
        iput(file_ip);
        iput(parent_ip);
        return -1;
    }

    if (inode_wrlock(file_ip) != 0) {
        iput(file_ip);
        iput(parent_ip);
        return -1;
    }

    file_ip->m_dinode.d_nlink--;
    if (file_ip->m_dinode.d_nlink == 0) {
        file_truncate(file_ip);
        file_ip->m_dinode.d_mode = 0;
    }
    file_ip->m_flags |= MINODE_DIRTY;

    inode_unlock(file_ip);
    iput(file_ip);
    iput(parent_ip);
    return 0;
}


int vfs_lseek(int fd, int offset, int whence)
{
    User          *u;
    OpenFileTable *oft;
    MemINode      *ip;
    int32_t        new_pos;

    u = dir_get_user();
    oft = oft_get(u, fd);
    if (oft == NULL) return -1;

    ip = oft->oft_inode;

    switch (whence) {
    case SEEK_SET_VFS:
        new_pos = offset;
        break;
    case SEEK_CUR_VFS:
        new_pos = (int32_t)oft->oft_read_pos + offset;
        break;
    case SEEK_END_VFS:
        new_pos = (int32_t)ip->m_dinode.d_size + offset;
        break;
    default:
        return -1;
    }

    if (new_pos < 0) return -1;
    oft->oft_read_pos  = (uint32_t)new_pos;
    oft->oft_write_pos = (uint32_t)new_pos;
    return (int)new_pos;
}


int vfs_access(const char *path, int amode)
{
    User     *u;
    MemINode *ip;
    uint16_t  m;

    u = dir_get_user();
    if (path == NULL || u == NULL) return -1;

    ip = namei(path);
    if (ip == NULL) return -1;

    m = ip->m_dinode.d_mode;

    if (u->u_uid == 0) {
        iput(ip);
        return 0;
    }

    if (ip->m_dinode.d_uid == u->u_uid) {
        if ((amode & ACC_R) && !(m & 0400)) { iput(ip); return -1; }
        if ((amode & ACC_W) && !(m & 0200)) { iput(ip); return -1; }
        if ((amode & ACC_X) && !(m & 0100)) { iput(ip); return -1; }
    } else if (ip->m_dinode.d_gid == u->u_gid) {
        if ((amode & ACC_R) && !(m & 0040)) { iput(ip); return -1; }
        if ((amode & ACC_W) && !(m & 0020)) { iput(ip); return -1; }
        if ((amode & ACC_X) && !(m & 0010)) { iput(ip); return -1; }
    } else {
        if ((amode & ACC_R) && !(m & 0004)) { iput(ip); return -1; }
        if ((amode & ACC_W) && !(m & 0002)) { iput(ip); return -1; }
        if ((amode & ACC_X) && !(m & 0001)) { iput(ip); return -1; }
    }

    iput(ip);
    return 0;
}


int vfs_stat(const char *path, uint16_t *out_mode, uint32_t *out_size,
             uint16_t *out_nlink, uint16_t *out_uid, uint16_t *out_gid,
             uint16_t *out_ino)
{
    MemINode *ip;

    if (path == NULL) return -1;
    ip = namei(path);
    if (ip == NULL) return -1;

    if (out_mode)  *out_mode  = ip->m_dinode.d_mode;
    if (out_size)  *out_size  = ip->m_dinode.d_size;
    if (out_nlink) *out_nlink = ip->m_dinode.d_nlink;
    if (out_uid)   *out_uid   = ip->m_dinode.d_uid;
    if (out_gid)   *out_gid   = ip->m_dinode.d_gid;
    if (out_ino)   *out_ino   = ip->m_inode_no;

    iput(ip);
    return 0;
}


int vfs_chmod(const char *path, uint16_t new_mode)
{
    User     *u;
    MemINode *ip;

    u = dir_get_user();
    if (path == NULL || u == NULL) return -1;

    ip = namei(path);
    if (ip == NULL) return -1;

    if (u->u_uid != 0 && ip->m_dinode.d_uid != u->u_uid) {
        iput(ip);
        return -1;
    }

    ip->m_dinode.d_mode = (ip->m_dinode.d_mode & 0xF000U) | (new_mode & 0777U);
    ip->m_flags |= MINODE_DIRTY;
    iput(ip);
    return 0;
}


int vfs_copy(const char *src, const char *dst)
{
    int src_fd, dst_fd;
    char buf[BLOCK_SIZE];
    int n;

    if (src == NULL || dst == NULL) return -1;

    src_fd = vfs_open(src, O_RDONLY);
    if (src_fd < 0) return -1;

    if (vfs_create(dst, 0644) != 0) {
        MemINode *exist = namei(dst);
        if (exist == NULL) { vfs_close(src_fd); return -1; }
        iput(exist);
    }

    dst_fd = vfs_open(dst, O_WRONLY);
    if (dst_fd < 0) { vfs_close(src_fd); return -1; }

    while ((n = vfs_read(src_fd, buf, BLOCK_SIZE)) > 0) {
        if (vfs_write(dst_fd, buf, n) != n) {
            vfs_close(src_fd);
            vfs_close(dst_fd);
            return -1;
        }
    }

    vfs_close(src_fd);
    vfs_close(dst_fd);
    return 0;
}


int vfs_link(const char *existing, const char *new_path)
{
    char      parent_path[PATH_BUF_SIZE];
    char      name[MAX_FILENAME_LEN + 1];
    MemINode *ip;
    MemINode *parent_ip;

    if (existing == NULL || new_path == NULL) return -1;

    ip = namei(existing);
    if (ip == NULL) return -1;

    if (!inode_is_reg(ip)) {
        iput(ip);
        return -1;
    }

    if (dir_split_path(new_path, parent_path, name) != 0) {
        iput(ip);
        return -1;
    }

    parent_ip = namei(parent_path);
    if (parent_ip == NULL) {
        iput(ip);
        return -1;
    }

    if (dir_link_entry(parent_ip, name, ip->m_inode_no) != 0) {
        iput(parent_ip);
        iput(ip);
        return -1;
    }

    ip->m_dinode.d_nlink++;
    ip->m_flags |= MINODE_DIRTY;

    iput(parent_ip);
    iput(ip);
    return 0;
}
