

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

#ifdef __cplusplus
}
#endif

#endif 
