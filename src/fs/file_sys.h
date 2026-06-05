/*
 * file_sys.h
 * 文件读写：用户打开文件表、系统打开文件表与 create/open/read/write 等操作。
 */
#ifndef FILE_SYS_H
#define FILE_SYS_H

#include "vfs_core.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OF_RDLOCKED             0x0010U
#define OF_WRLOCKED             0x0020U

#define SEEK_SET_VFS            0
#define SEEK_CUR_VFS            1
#define SEEK_END_VFS            2

#define ACC_R                   0x04
#define ACC_W                   0x02
#define ACC_X                   0x01

// 初始化系统级打开文件表
void sys_open_file_init(void);
// 统计系统打开文件表当前占用项数
int  sys_open_file_count(void);
// 返回系统级打开文件表指针
const SysOpenFile *sys_open_file_table(void);

// 在父目录下创建新的空普通文件
int upfs_create(const char *path, uint16_t mode);
// 打开文件并分配用户 fd，关联系统打开文件表
int upfs_open(const char *path, uint16_t mode);
// 从已打开文件当前读位置读取数据
int upfs_read(int fd, void *buf, int count);
// 向已打开文件当前写位置写入数据
int upfs_write(int fd, const void *buf, int count);
// 关闭 fd，递减系统表引用并可能释放 inode
int upfs_close(int fd);
// 删除路径对应的文件并回收 inode
int upfs_unlink(const char *path);
// 调整已打开文件的读或写位置
int upfs_lseek(int fd, int offset, int whence);
// 检查当前用户对路径的读/写/执行权限
int upfs_access(const char *path, int amode);
// 获取路径对应文件的元数据
int upfs_stat(const char *path, uint16_t *out_mode, uint32_t *out_size,
              uint16_t *out_nlink, uint16_t *out_uid, uint16_t *out_gid,
              uint16_t *out_ino);
// 修改路径对应文件或目录的权限位
int upfs_chmod(const char *path, uint16_t new_mode);
// 复制源文件内容到目标路径（覆盖或新建）
int upfs_copy(const char *src, const char *dst);
// 为已有文件创建硬链接目录项
int upfs_link(const char *existing, const char *new_path);

#ifdef __cplusplus
}
#endif

#endif
