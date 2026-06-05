/*
 * vfs.h
 * 虚拟文件系统抽象层：定义 inode/文件/超级块三类操作表，
 * 以及挂载、读写、目录等统一入口 vfs_*。
 */
#ifndef VFS_H
#define VFS_H

#include "vfs_core.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct MemINode;

// 路径与目录相关：创建、查找、删除、权限、切换目录等
struct inode_operations {
    int (*create)(const char *path, uint16_t mode);
    int (*mkdir)(const char *path, uint16_t mode);
    struct MemINode *(*lookup)(const char *path);
    int (*unlink)(const char *path);
    int (*link)(const char *existing, const char *new_path);
    int (*chmod)(const char *path, uint16_t mode);
    int (*stat)(const char *path, uint16_t *mode, uint32_t *size,
                uint16_t *nlink, uint16_t *uid, uint16_t *gid, uint16_t *ino);
    int (*access)(const char *path, int amode);
    int (*chdir)(const char *path);
    int (*listdir)(const char *path);
};

// 已打开文件的读写、定位、复制
struct file_operations {
    int (*open)(const char *path, uint16_t mode);
    int (*read)(int fd, void *buf, int count);
    int (*write)(int fd, const void *buf, int count);
    int (*close)(int fd);
    int (*lseek)(int fd, int offset, int whence);
    int (*copy)(const char *src, const char *dst);
};

// 整卷操作：挂载、卸载、格式化、刷盘
struct super_operations {
    int (*mount)(const char *disk_path);
    int (*umount)(void);
    int (*format)(const char *disk_path);
    int (*sync)(void);
};

// 一种具体文件系统的注册信息（本项目实现为 UPFS）
struct file_system_type {
    const char              *name;
    struct inode_operations *iops;
    struct file_operations  *fops;
    struct super_operations *sops;
};

// 注册一种文件系统到 VFS
void vfs_register_filesystem(struct file_system_type *fs);
// 返回当前注册到 VFS 的文件系统类型
struct file_system_type *vfs_current_fs(void);

// 挂载指定路径的磁盘到 VFS
int vfs_mount(const char *disk_path);
// 卸载当前 VFS 文件系统
int vfs_umount(void);
// 通过 VFS 格式化磁盘
int vfs_format_disk(const char *disk_path);
// 通过 VFS 刷写全部脏数据
int vfs_sync_all(void);

// VFS 层创建文件
int vfs_create(const char *path, uint16_t mode);
// VFS 层创建目录
int vfs_mkdir(const char *path, uint16_t mode);
// VFS 层按路径解析并返回内存 inode
struct MemINode *vfs_lookup(const char *path);
// VFS 层删除文件或空目录
int vfs_delete(const char *path);
// VFS 层打开文件
int vfs_open(const char *path, uint16_t mode);
// VFS 层读文件
int vfs_read(int fd, void *buf, int count);
// VFS 层写文件
int vfs_write(int fd, const void *buf, int count);
// VFS 层关闭文件描述符
int vfs_close(int fd);
// VFS 层调整文件偏移
int vfs_lseek(int fd, int offset, int whence);
// VFS 层检查访问权限
int vfs_access(const char *path, int amode);
// VFS 层获取路径对应文件的元数据
int vfs_stat(const char *path, uint16_t *mode, uint32_t *size,
             uint16_t *nlink, uint16_t *uid, uint16_t *gid, uint16_t *ino);
// VFS 层修改权限
int vfs_chmod(const char *path, uint16_t new_mode);
// VFS 层复制文件
int vfs_copy(const char *src, const char *dst);
// VFS 层创建硬链接
int vfs_link(const char *existing, const char *new_path);
// VFS 层切换工作目录
int vfs_chdir(const char *path);
// VFS 层列出目录
int vfs_listdir(const char *path);

// 注册 UPFS 为当前文件系统实现
void vfs_upfs_register(void);

#ifdef __cplusplus
}
#endif

#endif
