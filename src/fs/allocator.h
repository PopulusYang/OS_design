

#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include "vfs_core.h"

#ifdef __cplusplus
extern "C" {
#endif


int fs_mount(const char *disk_path);


int fs_umount(void);


int fs_sync_superblock(void);


int fs_sync_disk(void);


const SuperBlock *fs_get_superblock(void);


int balloc(void);


int bfree(int blk);


int ialloc(void);


int ifree(uint16_t ino);


int inode_rdlock(MemINode *ip);
int inode_wrlock(MemINode *ip);
void inode_unlock(MemINode *ip);


MemINode *iget(uint16_t ino);


void iput(MemINode *ip);

/* 调试：输出超级块详细信息（磁盘布局、空闲块链、i节点栈） */
void fs_debug_print_super(void);

/* 调试：输出 i节点缓存/池使用情况 */
void fs_debug_print_inodes(void);

#ifdef __cplusplus
}
#endif

#endif 
