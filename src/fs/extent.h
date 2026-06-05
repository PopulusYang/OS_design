/*
 * extent.h
 * 文件逻辑块到物理块的 Extent 映射：查找、分配与自动合并。
 */
#ifndef EXTENT_H
#define EXTENT_H

#include "vfs_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// 查磁盘 inode 中逻辑块到物理块的映射
int  extent_lookup(const DiskINode *d, uint32_t lblk, uint16_t *phys_out);
// 查或建逻辑块映射，create 为真时分配新物理块
int  extent_bmap(MemINode *ip, uint32_t lblk, int create, uint16_t *phys_out);
// 清空文件的 Extent 映射并释放树块
void extent_clear(MemINode *ip);
// 把 inode 设为仅含一条内联 Extent
int  extent_set_single(MemINode *ip, uint32_t lblk, uint16_t pblk, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif
