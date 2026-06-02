
#include "vfs.h"
#include "fs/allocator.h"
#include "fs/dir_sys.h"
#include "fs/file_sys.h"
#include "fs/format.h"
#include "fs/buf.h"
#include "fs/journal.h"

static struct file_system_type *g_mounted_fs; //当前挂载的文件系统

static struct inode_operations g_upfs_iops; //UPFS的i节点操作函数集合
static struct file_operations  g_upfs_fops; //UPFS的文件操作函数集合
static struct super_operations g_upfs_sops; //UPFS的超级块操作函数集合
static struct file_system_type g_upfs_fstype; //UPFS的文件系统类型结构体实例

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
    bflush_all();
    int rc = fs_umount();
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

/*
@brief VFS层的文件和目录操作接口，调用当前挂载文件系统的对应函数实现具体功能
@param path 文件或目录路径
@param mode 权限模式（仅create和mkdir使用）
@return 操作结果，通常0表示成功，非0表示失败
*/
int vfs_create(const char *path, uint16_t mode)
{
    if (g_mounted_fs == NULL || 
        g_mounted_fs->iops == NULL ||
        g_mounted_fs->iops->create == NULL) //检测当前挂载文件系统和i节点操作函数是否存在
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

//初始化文件系统
void vfs_upfs_register(void)
{
    //初始化UPFS的操作函数集合
    g_upfs_iops.create  = upfs_create; //创建文件
    g_upfs_iops.mkdir   = upfs_mkdir; //创建目录
    g_upfs_iops.lookup  = namei; //路径解析和i节点查找
    g_upfs_iops.unlink  = upfs_unlink; //删除文件或目录
    g_upfs_iops.link    = upfs_link; //创建硬链接
    g_upfs_iops.chmod   = upfs_chmod; //修改权限
    g_upfs_iops.stat    = upfs_stat; //获取文件或目录的状态信息
    g_upfs_iops.access  = upfs_access; //检查访问权限
    g_upfs_iops.chdir   = chdir; //改变当前工作目录
    g_upfs_iops.listdir = dir_list; //列出目录内容

    g_upfs_fops.open   = upfs_open; //打开文件
    g_upfs_fops.read   = upfs_read; //读取文件内容
    g_upfs_fops.write  = upfs_write; //写入文件内容
    g_upfs_fops.close  = upfs_close; //关闭文件
    g_upfs_fops.lseek  = upfs_lseek; //调整文件读写位置
    g_upfs_fops.copy   = upfs_copy; //复制文件

    g_upfs_sops.mount  = upfs_mount_wrap; //挂载文件系统
    g_upfs_sops.umount = upfs_umount_wrap; //卸载文件系统
    g_upfs_sops.format = upfs_format_wrap; //格式化磁盘
    g_upfs_sops.sync   = upfs_sync_wrap; //同步数据到磁盘

    g_upfs_fstype.name = "upfs"; //文件系统名称
    g_upfs_fstype.iops = &g_upfs_iops; //关联i节点操作函数集合
    g_upfs_fstype.fops = &g_upfs_fops; //关联文件操作函数集合
    g_upfs_fstype.sops = &g_upfs_sops; //关联超级块操作函数集合

    vfs_register_filesystem(&g_upfs_fstype);
}
