// file_sys.h —— 文件创建、打开、读写、关闭与删除

#ifndef FILE_SYS_H
#define FILE_SYS_H

#include "vfs_core.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 打开文件表项：标记本 fd 持有的 i 节点锁类型
#define OF_RDLOCKED             0x0010U
#define OF_WRLOCKED             0x0020U

// 创建普通文件（若已存在则失败）
int create(const char *path, uint16_t mode);

// 打开文件，返回 fd（0 .. MAX_OPEN_FILES-1），失败返回 -1
int open(const char *path, uint16_t mode);

// 从 fd 读取最多 count 字节，返回实际读取字节数，错误返回 -1
int read(int fd, void *buf, int count);

// 向 fd 写入 count 字节，返回实际写入字节数，错误返回 -1
int write(int fd, const void *buf, int count);

// 关闭 fd，释放 i 节点读写锁与引用
int close(int fd);

// 删除文件（删除目录项并回收数据块与 i 节点）
int delete(const char *path);

#ifdef __cplusplus
}
#endif

#endif // FILE_SYS_H
