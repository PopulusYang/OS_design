/*
 * inomap.h
 * 动态 inode：位置树与空闲树定位 inode 块与槽位，分配与回收 inode 号。
 */
#ifndef INOMAP_H
#define INOMAP_H

#include "vfs_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// 格式化时建根 inode、位置树与空闲树
int  inomap_format_init(uint16_t root_dir_block);
// 挂载时从超级块恢复 inode 映射状态
int  inomap_load(const SuperBlock *sb);
// 把 inode 统计与 B+ 树根写回超级块
int  inomap_sync(SuperBlock *sb);

// 查 inode 号在磁盘上的块号与槽位偏移
int  inomap_lookup(uint32_t ino, int *out_blk, int *out_off);
// 分配 inode：先弹栈，再找空槽，最后新建块
int  inomap_ialloc_for(uint32_t parent_ino);
// 回收 inode 号并更新位置树与空闲树
int  inomap_ifree(uint32_t ino);

// 判断块号是否为存放 inode 的数据块
int  inomap_is_chunk_block(int blockno);
// 按 inode 号写回 32 字节磁盘 inode
int  inomap_write_disk_inode(uint32_t ino, const DiskINode *inode);
// 按 inode 号读取 32 字节磁盘 inode
int  inomap_read_disk_inode(uint32_t ino, DiskINode *out);

// 打印 inode 映射与回收栈状态
void inomap_debug_print(void);

#ifdef __cplusplus
}
#endif

#endif
