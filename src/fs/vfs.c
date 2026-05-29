
#include "vfs.h"
#include "fs/allocator.h"
#include "fs/dir_sys.h"
#include "fs/file_sys.h"
#include "fs/format.h"
#include "fs/buf.h"
#include "fs/journal.h"

static struct file_system_type *g_mounted_fs;

static struct inode_operations g_upfs_iops;
static struct file_operations  g_upfs_fops;
static struct super_operations g_upfs_sops;
static struct file_system_type g_upfs_fstype;

static int upfs_mount_wrap(const char *path)
{
    buf_init();
    if (fs_mount(path) != 0) {
        buf_shutdown();
        return -1;
    }
    if (journal_replay() != 0) {
        fs_umount();
        buf_shutdown();
        return -1;
    }
    return 0;
}

static int upfs_umount_wrap(void)
{
    int rc = fs_umount();
    bflush_all();
    buf_shutdown();
    return rc;
}

static int upfs_format_wrap(const char *path)
{
    return format(path);
}

static int upfs_sync_wrap(void)
{
    bflush_all();
    return fs_sync_disk();
}

void vfs_register_filesystem(struct file_system_type *fs)
{
    g_mounted_fs = fs;
}

struct file_system_type *vfs_current_fs(void)
{
    return g_mounted_fs;
}

int vfs_mount(const char *disk_path)
{
    if (g_mounted_fs == NULL || g_mounted_fs->sops == NULL ||
        g_mounted_fs->sops->mount == NULL)
        return -1;
    return g_mounted_fs->sops->mount(disk_path);
}

int vfs_umount(void)
{
    if (g_mounted_fs == NULL || g_mounted_fs->sops == NULL ||
        g_mounted_fs->sops->umount == NULL)
        return -1;
    return g_mounted_fs->sops->umount();
}

int vfs_format_disk(const char *disk_path)
{
    if (g_mounted_fs && g_mounted_fs->sops && g_mounted_fs->sops->format)
        return g_mounted_fs->sops->format(disk_path);
    return format(disk_path);
}

int vfs_sync_all(void)
{
    if (g_mounted_fs && g_mounted_fs->sops && g_mounted_fs->sops->sync)
        return g_mounted_fs->sops->sync();
    return fs_sync_disk();
}

int vfs_create(const char *path, uint16_t mode)
{
    if (g_mounted_fs == NULL || g_mounted_fs->iops == NULL ||
        g_mounted_fs->iops->create == NULL)
        return -1;
    return g_mounted_fs->iops->create(path, mode);
}

int vfs_mkdir(const char *path, uint16_t mode)
{
    if (g_mounted_fs == NULL || g_mounted_fs->iops == NULL ||
        g_mounted_fs->iops->mkdir == NULL)
        return -1;
    return g_mounted_fs->iops->mkdir(path, mode);
}

struct MemINode *vfs_lookup(const char *path)
{
    if (g_mounted_fs == NULL || g_mounted_fs->iops == NULL ||
        g_mounted_fs->iops->lookup == NULL)
        return NULL;
    return g_mounted_fs->iops->lookup(path);
}

int vfs_delete(const char *path)
{
    if (g_mounted_fs == NULL || g_mounted_fs->iops == NULL ||
        g_mounted_fs->iops->unlink == NULL)
        return -1;
    return g_mounted_fs->iops->unlink(path);
}

int vfs_open(const char *path, uint16_t mode)
{
    if (g_mounted_fs == NULL || g_mounted_fs->fops == NULL ||
        g_mounted_fs->fops->open == NULL)
        return -1;
    return g_mounted_fs->fops->open(path, mode);
}

int vfs_read(int fd, void *buf, int count)
{
    if (g_mounted_fs == NULL || g_mounted_fs->fops == NULL ||
        g_mounted_fs->fops->read == NULL)
        return -1;
    return g_mounted_fs->fops->read(fd, buf, count);
}

int vfs_write(int fd, const void *buf, int count)
{
    if (g_mounted_fs == NULL || g_mounted_fs->fops == NULL ||
        g_mounted_fs->fops->write == NULL)
        return -1;
    return g_mounted_fs->fops->write(fd, buf, count);
}

int vfs_close(int fd)
{
    if (g_mounted_fs == NULL || g_mounted_fs->fops == NULL ||
        g_mounted_fs->fops->close == NULL)
        return -1;
    return g_mounted_fs->fops->close(fd);
}

int vfs_lseek(int fd, int offset, int whence)
{
    if (g_mounted_fs == NULL || g_mounted_fs->fops == NULL ||
        g_mounted_fs->fops->lseek == NULL)
        return -1;
    return g_mounted_fs->fops->lseek(fd, offset, whence);
}

int vfs_access(const char *path, int amode)
{
    if (g_mounted_fs == NULL || g_mounted_fs->iops == NULL ||
        g_mounted_fs->iops->access == NULL)
        return -1;
    return g_mounted_fs->iops->access(path, amode);
}

int vfs_stat(const char *path, uint16_t *mode, uint32_t *size,
             uint16_t *nlink, uint16_t *uid, uint16_t *gid, uint16_t *ino)
{
    if (g_mounted_fs == NULL || g_mounted_fs->iops == NULL ||
        g_mounted_fs->iops->stat == NULL)
        return -1;
    return g_mounted_fs->iops->stat(path, mode, size, nlink, uid, gid, ino);
}

int vfs_chmod(const char *path, uint16_t new_mode)
{
    if (g_mounted_fs == NULL || g_mounted_fs->iops == NULL ||
        g_mounted_fs->iops->chmod == NULL)
        return -1;
    return g_mounted_fs->iops->chmod(path, new_mode);
}

int vfs_copy(const char *src, const char *dst)
{
    if (g_mounted_fs == NULL || g_mounted_fs->fops == NULL ||
        g_mounted_fs->fops->copy == NULL)
        return -1;
    return g_mounted_fs->fops->copy(src, dst);
}

int vfs_link(const char *existing, const char *new_path)
{
    if (g_mounted_fs == NULL || g_mounted_fs->iops == NULL ||
        g_mounted_fs->iops->link == NULL)
        return -1;
    return g_mounted_fs->iops->link(existing, new_path);
}

int vfs_chdir(const char *path)
{
    if (g_mounted_fs == NULL || g_mounted_fs->iops == NULL ||
        g_mounted_fs->iops->chdir == NULL)
        return -1;
    return g_mounted_fs->iops->chdir(path);
}

int vfs_listdir(const char *path)
{
    if (g_mounted_fs == NULL || g_mounted_fs->iops == NULL ||
        g_mounted_fs->iops->listdir == NULL)
        return -1;
    return g_mounted_fs->iops->listdir(path);
}

void vfs_upfs_register(void)
{
    g_upfs_iops.create  = upfs_create;
    g_upfs_iops.mkdir   = upfs_mkdir;
    g_upfs_iops.lookup  = namei;
    g_upfs_iops.unlink  = upfs_unlink;
    g_upfs_iops.link    = upfs_link;
    g_upfs_iops.chmod   = upfs_chmod;
    g_upfs_iops.stat    = upfs_stat;
    g_upfs_iops.access  = upfs_access;
    g_upfs_iops.chdir   = chdir;
    g_upfs_iops.listdir = dir_list;

    g_upfs_fops.open   = upfs_open;
    g_upfs_fops.read   = upfs_read;
    g_upfs_fops.write  = upfs_write;
    g_upfs_fops.close  = upfs_close;
    g_upfs_fops.lseek  = upfs_lseek;
    g_upfs_fops.copy   = upfs_copy;

    g_upfs_sops.mount  = upfs_mount_wrap;
    g_upfs_sops.umount = upfs_umount_wrap;
    g_upfs_sops.format = upfs_format_wrap;
    g_upfs_sops.sync   = upfs_sync_wrap;

    g_upfs_fstype.name = "upfs";
    g_upfs_fstype.iops = &g_upfs_iops;
    g_upfs_fstype.fops = &g_upfs_fops;
    g_upfs_fstype.sops = &g_upfs_sops;

    vfs_register_filesystem(&g_upfs_fstype);
}
