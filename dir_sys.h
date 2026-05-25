// dir_sys.h —— 目录管理与路径解析

#ifndef DIR_SYS_H
#define DIR_SYS_H

#include "vfs_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// 绑定当前用户上下文；namei / chdir / ls 相对路径依赖 u_cdir
void dir_bind_user(User *u);

// 获取当前绑定的用户；未绑定时返回 NULL
User *dir_get_user(void);

// 路径解析：支持绝对路径（/ 开头）与相对路径，成功返回目标 MemINode（ref=1，调用者 iput）
MemINode *namei(const char *path);

// 创建目录，自动写入 . 与 ..；mode 为权限位（通常 OR IFDIR）
int mkdir(const char *path, uint16_t mode);

// 切换当前工作目录
int chdir(const char *path);

// 列出目录内容；path 为 NULL 或空串时使用当前目录
int dir_list(const char *path);

// 拆分路径为父目录路径与最终文件名（parent/name 缓冲至少 256/15 字节）
int dir_split_path(const char *path, char *parent, char *name);

// 在目录中追加目录项（文件或子目录）
int dir_link_entry(MemINode *dir_ip, const char *name, uint16_t ino);

// 从目录中删除目录项；通过 out_ino 返回被删文件的 i 节点号
int dir_unlink_entry(MemINode *dir_ip, const char *name, uint16_t *out_ino);

// ls 为 dir_list 的别名
#define ls dir_list

#ifdef __cplusplus
}
#endif

#endif // DIR_SYS_H
