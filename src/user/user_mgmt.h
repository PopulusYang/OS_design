/*
 * user_mgmt.h
 * 多用户账户与 /etc/passwd 管理接口。
 */
#ifndef USER_MGMT_H
#define USER_MGMT_H

#include "vfs_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USER_UID_BASE           1000

#define USER_SALT_LEN           8
#define USER_HASH_LEN           32

#define USER_HASH_HEX_LEN       (USER_HASH_LEN * 2 + 1)
#define USER_SALT_HEX_LEN       (USER_SALT_LEN * 2 + 1)

typedef struct UserAccount {
    char     ua_name[32];
    uint16_t ua_uid;
    uint16_t ua_gid;
    char     ua_home[64];
    char     ua_passwd_hash[USER_HASH_HEX_LEN];
    char     ua_salt[USER_SALT_HEX_LEN];
} UserAccount;

// 确保 passwd 文件存在并加载用户库
int user_init(void);

// 从 /etc/passwd 解析用户记录到内存表
int user_db_load(void);

// 把内存用户表序列化写入 /etc/passwd
int user_db_save(void);

// 新建用户：分配 uid、哈希口令、建主目录
int user_add(const char *username, const char *password);

// 校验用户名与口令，成功返回 uid
int user_verify(const char *username, const char *password);

// 按用户名查找账户记录
const UserAccount *user_find(const char *username);

// 按 uid 查找账户记录
const UserAccount *user_find_by_uid(uint16_t uid);

// 返回当前用户数量
int user_count(void);

// 按下标取账户记录
const UserAccount *user_get(int index);

// 从表中删除用户并保存 passwd
int user_delete(const char *username);

// 为用户生成新盐并更新口令哈希
int user_passwd(const char *username, const char *new_password);

// 创建 /bin、/home、/root、/etc 目录
int user_create_posix_dirs(uint16_t owner_uid, uint16_t owner_gid);

// 在 /home 下为用户创建主目录
int user_create_home(const char *username, uint16_t uid, uint16_t gid);

// 用盐对口令做 10000 轮哈希，输出十六进制摘要
void user_hash_password(const char *password, const char *salt_hex, char *hex_out);

// 生成随机盐，优先读 /dev/urandom
void user_gen_salt(char *hex_out);

#ifdef __cplusplus
}
#endif

#endif
