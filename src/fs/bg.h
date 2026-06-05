
// 块组管理模块（ext2风格）：
// - 磁盘划分为等大的块组区域，每块组包含1个锚点块+N个数据块
// - 每块组独立维护成组链接法空闲块栈（锚点块为栈顶）
// - 支持按inode所在块组优先分配（局部性优化）
#ifndef BG_H
#define BG_H

#include "vfs_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// 格式化阶段：构建块组布局，初始化空闲块栈，写入锚点块
int  bg_format_init(void);

// 挂载阶段：从超级块加载块组描述符，读取各块组锚点块
int  bg_init_from_super(const SuperBlock *sb);

// 同步：将运行时空闲块信息写回超级块
int  bg_fill_super(SuperBlock *sb);

// 将所有块组锚点块写回磁盘
int  bg_sync(void);

// 根据块号反查所属块组编号
int  bg_from_block(int blockno);

// 从指定inode所在块组优先分配一个空闲数据块
int  bg_balloc_for(uint16_t ino_hint);

// 释放一个数据块，归还到其所属块组的空闲栈
int  bg_bfree(int blockno);

// 判断块号是否在数据区（非锚点、非启动块）
int  bg_block_in_data_zone(int blockno);

// 判断块号是否为inode chunk块（委托inomap判断）
int  bg_is_inode_disk_block(int blockno);

// 判断块号是否为锚点/启动/超级块（不可分配的特殊块）
int  bg_is_anchor_block(int blockno);

// 调试输出：打印所有块组信息
void bg_debug_print(void);

// 查询指定块组的当前空闲块数
uint32_t bg_group_free(int group);

#ifdef __cplusplus
}
#endif

#endif
