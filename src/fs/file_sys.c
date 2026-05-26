// file_sys.c —— 文件系统调用与混合索引块分配、i 节点读写锁

#include "fs/file_sys.h"
#include "fs/dir_sys.h"
#include "fs/allocator.h"
#include "fs/disk_io.h"

#include <string.h>

#define PATH_BUF_SIZE           256

// 判断是否为普通文件
static int inode_is_reg(const MemINode *ip)
{
    if (ip == NULL) {
        return 0;
    }
    return (ip->m_dinode.d_mode & IFREG) != 0;
}

// 在用户打开文件表中分配一个空闲 fd
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

// 校验 fd 并返回打开文件表项
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

// 读取索引块中某一槽位的盘块号
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

// 写入索引块中某一槽位的盘块号
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

// 分配一个新索引块并清零
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

// 混合索引映射：根据逻辑块号返回物理盘块号；create_flag 非 0 时自动分配
//
// 布局：
//   逻辑 0..7           -> d_direct[]
//   逻辑 8..8+255       -> d_sindirect 一级间址
//   逻辑 264..          -> d_dindirect 二级间址
static int file_bmap(MemINode *ip, uint32_t logical_blk, int create_flag, uint16_t *phys_out)
{
    DiskINode *d;
    uint16_t   phys;

    if (ip == NULL || phys_out == NULL) {
        return -1;
    }

    d = &ip->m_dinode;

    // 直接索引
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

    // 一次间址
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

    // 二次间址
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

// 释放索引块中登记的全部数据块及索引块自身
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

// 释放二级间址块链
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

// 截断文件：回收全部数据块并重置索引
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

    // 目标已存在则失败
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

    // 写模式（含 O_RDWR）获取互斥写锁；只读模式允许多 fd 共享读锁
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

        // 混合索引：必要时自动分配直接/一次/二次间址块
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

    // 释放 open 时获取的读/写锁
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

    // 写锁保护删除过程，避免与并发读/写冲突
    if (inode_wrlock(file_ip) != 0) {
        iput(file_ip);
        iput(parent_ip);
        return -1;
    }

    file_truncate(file_ip);
    file_ip->m_dinode.d_nlink = 0;
    file_ip->m_dinode.d_mode = 0;
    file_ip->m_flags |= MINODE_DIRTY;

    inode_unlock(file_ip);
    iput(file_ip);
    iput(parent_ip);
    return 0;
}
