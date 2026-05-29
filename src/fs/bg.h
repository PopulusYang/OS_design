
#ifndef BG_H
#define BG_H

#include "vfs_core.h"

#ifdef __cplusplus
extern "C" {
#endif

int  bg_format_init(void);
int  bg_init_from_super(const SuperBlock *sb);
int  bg_fill_super(SuperBlock *sb);
int  bg_sync(void);

int  bg_from_block(int blockno);
int  bg_balloc_for(uint16_t ino_hint);
int  bg_bfree(int blockno);

int  bg_block_in_data_zone(int blockno);
int  bg_is_inode_disk_block(int blockno);
int  bg_is_anchor_block(int blockno);

void bg_debug_print(void);
uint32_t bg_group_free(int group);

#ifdef __cplusplus
}
#endif

#endif
