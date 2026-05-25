// disk_io.h —— 虚拟磁盘块级读写与持久化接口

#ifndef DISK_IO_H
#define DISK_IO_H

#include "vfs_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// 默认虚拟盘镜像文件名
#define DEFAULT_DISK_PATH       "vfs_disk.img"

// 在内存中创建空白虚拟盘（calloc），若已有旧镜像则先释放
int disk_create(void);

// 从宿主机文件加载完整虚拟盘镜像到内存；文件不存在或大小不对则失败
int disk_load(const char *disk_path);

// 将内存中完整虚拟盘镜像写入宿主机文件（覆盖写）
int disk_save(const char *disk_path);

// 关闭虚拟盘，释放内存；不会自动 save
void disk_shutdown(void);

// 读取指定逻辑块到 buf（buf 至少 BLOCK_SIZE 字节）
int read_block(int block_no, void *buf);

// 将 buf 写入指定逻辑块（buf 至少 BLOCK_SIZE 字节）
int write_block(int block_no, const void *buf);

// 返回内存模拟区首地址；未创建时返回 NULL
void *disk_memory(void);

// 返回内存模拟区总字节数
size_t disk_memory_size(void);

#ifdef __cplusplus
}
#endif

#endif // DISK_IO_H
