/*
 * bg.h
 * 块组管理：8 个块组的布局、成组链接法分配与回收数据块。
 */
#ifndef BG_H
#define BG_H

#include "vfs_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// 格式化时初始化全部块组的空闲链表
int  bg_format_init(void);
// 挂载时从超级块和锚块恢复块组状态
int  bg_init_from_super(const SuperBlock *sb);
// 汇总各块组空闲块数写入超级块
int  bg_fill_super(SuperBlock *sb);
// 把所有块组锚块写回磁盘
int  bg_sync(void);

// 根据物理块号计算所属块组编号
int  bg_from_block(int blockno);
// 按 inode 提示优先从同块组分配一个空闲数据块
int  bg_balloc_for(uint16_t ino_hint);
// 把数据块回收到所属块组的空闲栈
int  bg_bfree(int blockno);

// 判断块号是否落在某块组的数据区内
int  bg_block_in_data_zone(int blockno);
// 判断块号是否为 inode 块
int  bg_is_inode_disk_block(int blockno);
// 判断块号是否为块组锚块或引导/超级块
int  bg_is_anchor_block(int blockno);

// 打印各块组锚块、数据区与空闲块统计
void bg_debug_print(void);
// 返回指定块组当前空闲块数
uint32_t bg_group_free(int group);

#ifdef __cplusplus
}
#endif

#endif
