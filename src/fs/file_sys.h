

#ifndef FILE_SYS_H
#define FILE_SYS_H

#include "vfs_core.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


#define OF_RDLOCKED             0x0010U
#define OF_WRLOCKED             0x0020U


#define SEEK_SET_VFS            0
#define SEEK_CUR_VFS            1
#define SEEK_END_VFS            2

#define ACC_R                   0x04
#define ACC_W                   0x02
#define ACC_X                   0x01


void sys_open_file_init(void);
int  sys_open_file_count(void);
const SysOpenFile *sys_open_file_table(void);


int upfs_create(const char *path, uint16_t mode);
int upfs_open(const char *path, uint16_t mode);
int upfs_read(int fd, void *buf, int count);
int upfs_write(int fd, const void *buf, int count);
int upfs_close(int fd);
int upfs_unlink(const char *path);
int upfs_lseek(int fd, int offset, int whence);
int upfs_access(const char *path, int amode);
int upfs_stat(const char *path, uint16_t *out_mode, uint32_t *out_size,
              uint16_t *out_nlink, uint16_t *out_uid, uint16_t *out_gid,
              uint16_t *out_ino);
int upfs_chmod(const char *path, uint16_t new_mode);
int upfs_copy(const char *src, const char *dst);
int upfs_link(const char *existing, const char *new_path);

#ifdef __cplusplus
}
#endif

#endif 
