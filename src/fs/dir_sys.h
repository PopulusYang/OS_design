

#ifndef DIR_SYS_H
#define DIR_SYS_H

#include "vfs_core.h"

#ifdef __cplusplus
extern "C" {
#endif


void dir_bind_user(User *u);


User *dir_get_user(void);


MemINode *namei(const char *path);


int upfs_mkdir(const char *path, uint16_t mode);


int chdir(const char *path);


int dir_list(const char *path);


int dir_split_path(const char *path, char *parent, char *name);


int dir_link_entry(MemINode *dir_ip, const char *name, uint16_t ino);


int dir_unlink_entry(MemINode *dir_ip, const char *name, uint16_t *out_ino);


#define ls dir_list

#ifdef __cplusplus
}
#endif

#endif 
