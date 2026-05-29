
#ifndef INOMAP_H
#define INOMAP_H

#include "vfs_core.h"

#ifdef __cplusplus
extern "C" {
#endif

int  inomap_format_init(uint16_t root_dir_block);
int  inomap_load(const SuperBlock *sb);
int  inomap_sync(SuperBlock *sb);

int  inomap_lookup(uint32_t ino, int *out_blk, int *out_off);
int  inomap_ialloc_for(uint32_t parent_ino);
int  inomap_ifree(uint32_t ino);

int  inomap_is_chunk_block(int blockno);
int  inomap_write_disk_inode(uint32_t ino, const DiskINode *inode);
int  inomap_read_disk_inode(uint32_t ino, DiskINode *out);

void inomap_debug_print(void);

#ifdef __cplusplus
}
#endif

#endif
