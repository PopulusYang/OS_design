
#ifndef EXTENT_H
#define EXTENT_H

#include "vfs_core.h"

#ifdef __cplusplus
extern "C" {
#endif

int  extent_lookup(const DiskINode *d, uint32_t lblk, uint16_t *phys_out);
int  extent_bmap(MemINode *ip, uint32_t lblk, int create, uint16_t *phys_out);
void extent_clear(MemINode *ip);
int  extent_set_single(MemINode *ip, uint32_t lblk, uint16_t pblk, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif
