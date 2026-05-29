
#ifndef VFS_H
#define VFS_H

#include "vfs_core.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct MemINode;

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

struct file_operations {
    int (*open)(const char *path, uint16_t mode);
    int (*read)(int fd, void *buf, int count);
    int (*write)(int fd, const void *buf, int count);
    int (*close)(int fd);
    int (*lseek)(int fd, int offset, int whence);
    int (*copy)(const char *src, const char *dst);
};

struct super_operations {
    int (*mount)(const char *disk_path);
    int (*umount)(void);
    int (*format)(const char *disk_path);
    int (*sync)(void);
};

struct file_system_type {
    const char              *name;
    struct inode_operations *iops;
    struct file_operations  *fops;
    struct super_operations *sops;
};

void vfs_register_filesystem(struct file_system_type *fs);
struct file_system_type *vfs_current_fs(void);

int vfs_mount(const char *disk_path);
int vfs_umount(void);
int vfs_format_disk(const char *disk_path);
int vfs_sync_all(void);

int vfs_create(const char *path, uint16_t mode);
int vfs_mkdir(const char *path, uint16_t mode);
struct MemINode *vfs_lookup(const char *path);
int vfs_delete(const char *path);
int vfs_open(const char *path, uint16_t mode);
int vfs_read(int fd, void *buf, int count);
int vfs_write(int fd, const void *buf, int count);
int vfs_close(int fd);
int vfs_lseek(int fd, int offset, int whence);
int vfs_access(const char *path, int amode);
int vfs_stat(const char *path, uint16_t *mode, uint32_t *size,
             uint16_t *nlink, uint16_t *uid, uint16_t *gid, uint16_t *ino);
int vfs_chmod(const char *path, uint16_t new_mode);
int vfs_copy(const char *src, const char *dst);
int vfs_link(const char *existing, const char *new_path);
int vfs_chdir(const char *path);
int vfs_listdir(const char *path);

void vfs_upfs_register(void);

#ifdef __cplusplus
}
#endif

#endif
