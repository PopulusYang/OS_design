/*
 * dir_sys.h
 * 目录与路径：namei 路径解析、mkdir、chdir、ls 与目录项增删。
 */
#ifndef DIR_SYS_H
#define DIR_SYS_H

#include "vfs_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// 绑定当前操作用户，供路径解析取工作目录
void dir_bind_user(User *u);
// 返回当前绑定的操作用户指针
User *dir_get_user(void);

// 按绝对或相对路径解析并得到目标 inode
MemINode *namei(const char *path);

// 创建目录：分配 inode、写目录项、初始化 . 和 ..
int upfs_mkdir(const char *path, uint16_t mode);
// 切换当前用户的工作目录 inode
int chdir(const char *path);
// 列出目录下所有有效目录项
int dir_list(const char *path);

// 对外接口：拆分路径为父目录与文件名
int dir_split_path(const char *path, char *parent, char *name);
// 在目录中新增一条指向 inode 的目录项
int dir_link_entry(MemINode *dir_ip, const char *name, uint16_t ino);
// 从目录删除指定名称的目录项
int dir_unlink_entry(MemINode *dir_ip, const char *name, uint16_t *out_ino);

#define ls dir_list

#ifdef __cplusplus
}
#endif

#endif
