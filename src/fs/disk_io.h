

#ifndef DISK_IO_H
#define DISK_IO_H

#include "vfs_core.h"

#ifdef __cplusplus
extern "C" {
#endif


#define DEFAULT_DISK_PATH       "testimg/vfs_disk.img"


int disk_create(void);


int disk_load(const char *disk_path);


int disk_save(const char *disk_path);


int disk_sync(void);


void disk_shutdown(void);


int disk_read_block(int block_no, void *buf);


int disk_write_block(int block_no, const void *buf);


void *disk_memory(void);


size_t disk_memory_size(void);

#ifdef __cplusplus
}
#endif

#endif 
