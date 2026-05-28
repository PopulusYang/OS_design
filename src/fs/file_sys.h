

#ifndef FILE_SYS_H
#define FILE_SYS_H

#include "vfs_core.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


#define OF_RDLOCKED             0x0010U
#define OF_WRLOCKED             0x0020U


int vfs_create(const char *path, uint16_t mode);


int vfs_open(const char *path, uint16_t mode);


int vfs_read(int fd, void *buf, int count);


int vfs_write(int fd, const void *buf, int count);


int vfs_close(int fd);


int vfs_delete(const char *path);

#ifdef __cplusplus
}
#endif

#endif 
