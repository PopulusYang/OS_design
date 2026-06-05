/*
 * disk_io.h
 * 虚拟磁盘块设备：按 512 字节块读写内存盘或 mmap 镜像文件。
 */
#ifndef DISK_IO_H
#define DISK_IO_H

#include "vfs_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_DISK_PATH "testimg/vfs_disk.img"

// 分配全零内存盘，供 format 使用
int disk_create(void);
// 打开镜像文件并 mmap 映射到内存
int disk_load(const char *disk_path);
// 把内存盘内容写入镜像文件
int disk_save(const char *disk_path);
// 将内存中的修改同步到磁盘文件
int disk_sync(void);
// 释放内存盘或解除 mmap 并关闭文件
void disk_shutdown(void);
// 从指定块号读取 512 字节到缓冲区
int disk_read_block(int block_no, void *buf);
// 把 512 字节写入指定块号
int disk_write_block(int block_no, const void *buf);
// 返回虚拟磁盘内存映射的起始地址
void *disk_memory(void);
// 返回虚拟磁盘总字节数
size_t disk_memory_size(void);

#ifdef __cplusplus
}
#endif

#endif
