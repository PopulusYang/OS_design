// allocator.h —— 数据块 / i 节点分配器与内存 i 节点缓存（iget / iput）

#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include "vfs_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// 挂载已格式化的虚拟盘，将超级块读入内存
int fs_mount(const char *disk_path);

// 卸载：回写超级块并释放内存 i 节点缓存，关闭虚拟盘
int fs_umount(void);

// 将内存超级块同步到磁盘 1# 块
int fs_sync_superblock(void);

// 获取内存中的超级块副本（只读访问；修改请通过分配器接口）
const SuperBlock *fs_get_superblock(void);

// 分配一个空闲数据块，成功返回块号，失败返回 -1
int balloc(void);

// 回收一个数据块；blk 必须在数据区范围内
int bfree(int blk);

// 分配一个空闲 i 节点，成功返回 i 节点号（>=2），失败返回 -1
int ialloc(void);

// 释放 i 节点号（调用前须已将磁盘 i 节点内容清零或 nlink 置 0）
int ifree(uint16_t ino);

// 内存 i 节点读写锁（基于 pthread_rwlock_t；open/close 时成对调用）
int inode_rdlock(MemINode *ip);
int inode_wrlock(MemINode *ip);
void inode_unlock(MemINode *ip);

// 获取内存 i 节点（Hash 命中则 ref++，否则从磁盘读入并挂链）
MemINode *iget(uint16_t ino);

// 释放内存 i 节点引用；ref 归零且 dirty 时回写，nlink==0 时调用 ifree 并卸链
void iput(MemINode *ip);

#ifdef __cplusplus
}
#endif

#endif // ALLOCATOR_H
