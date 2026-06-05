/*
 * allocator.h
 * 挂载与同步、块和 inode 分配封装、内存 inode 哈希缓存的 iget 与 iput。
 */
#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include "vfs_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// 加载磁盘镜像、恢复块组与 inode 映射并回放日志
int fs_mount(const char *disk_path);
// 刷盘、释放 inode 缓存并关闭磁盘
int fs_umount(void);
// 把内存超级块写回块 1
int fs_sync_superblock(void);
// 刷缓存、同步超级块并保存镜像
int fs_sync_disk(void);
// 返回已挂载的超级块指针，未挂载时返回 NULL
const SuperBlock *fs_get_superblock(void);

// 分配数据块，不指定 inode 提示
int balloc(void);
// 分配数据块，转发到块组并按 inode 就近
int balloc_for(uint16_t ino_hint);
// 释放数据块回块组
int bfree(int blk);
// 分配 inode 号，不指定父 inode
int ialloc(void);
// 分配 inode 号，优先在父 inode 所在块组
int ialloc_for(uint16_t parent_ino);
// 回收 inode 号（根目录 inode 不可释放）
int ifree(uint16_t ino);

// 对内存 inode 加读锁
int inode_rdlock(MemINode *ip);
// 对内存 inode 加写锁
int inode_wrlock(MemINode *ip);
// 解除内存 inode 的读写锁
void inode_unlock(MemINode *ip);

// 缓存命中则引用计数加一，否则从磁盘读入并插入哈希表
MemINode *iget(uint16_t ino);
// 释放 inode 引用，脏则写回，链接为 0 则回收
void iput(MemINode *ip);

// 多终端模式下重新从磁盘读超级块
int fs_reload_super(void);
// 打印超级块与各子系统统计信息
void fs_debug_print_super(void);
// 打印 inode 缓存池使用情况
void fs_debug_print_inodes(void);

#ifdef __cplusplus
}
#endif

#endif
