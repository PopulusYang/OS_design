// user_mgmt.h —— 多用户管理：账号增删、口令哈希、/etc/passwd 持久化
//
// 本模块位于 file_sys/dir_sys 之上，通过文件系统接口读写 /etc/passwd。
// 所有用户操作须在文件系统挂载后调用。

#ifndef USER_MGMT_H
#define USER_MGMT_H

#include "vfs_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// 系统最大用户数见 vfs_core.h 中的 MAX_USERS

// 普通用户起始 UID
#define USER_UID_BASE           1000

// 口令盐值 / 哈希字节长度
#define USER_SALT_LEN           8
#define USER_HASH_LEN           32

// 十六进制字符串缓冲（含 '\0'）
#define USER_HASH_HEX_LEN       (USER_HASH_LEN * 2 + 1)
#define USER_SALT_HEX_LEN       (USER_SALT_LEN * 2 + 1)

// 内存中的用户账号记录
typedef struct UserAccount {
    char     ua_name[32];
    uint16_t ua_uid;
    uint16_t ua_gid;
    char     ua_home[64];
    char     ua_passwd_hash[USER_HASH_HEX_LEN];
    char     ua_salt[USER_SALT_HEX_LEN];
} UserAccount;

// 初始化用户子系统（加载 /etc/passwd 并创建默认目录）。
// 须在文件系统已挂载、且当前用户已绑定后调用。
// 若 /etc/passwd 不存在则自动创建空文件。
// 返回 0 成功，-1 失败。
int user_init(void);

// 从 /etc/passwd 重新加载用户数据库（通常在 mount 后调用）。
// 返回已加载用户数，-1 表示出错。
int user_db_load(void);

// 将内存中的用户数据库写回 /etc/passwd。
int user_db_save(void);

// 添加新用户（自动哈希口令、分配 uid/gid、创建家目录）。
// 返回 0 成功，-1 失败（用户已满 / 同名 / 磁盘错误）。
int user_add(const char *username, const char *password);

// 验证用户名 + 口令，返回 uid（>=0），失败返回 -1。
int user_verify(const char *username, const char *password);

// 按用户名查找账号，未找到返回 NULL。
const UserAccount *user_find(const char *username);

// 按 uid 查找账号，未找到返回 NULL。
const UserAccount *user_find_by_uid(uint16_t uid);

// 获取已加载用户总数。
int user_count(void);

// 按索引获取账号（0 .. count-1），越界返回 NULL。
const UserAccount *user_get(int index);

// 删除用户（同时删除家目录）。
int user_delete(const char *username);

// 修改用户口令。
int user_passwd(const char *username, const char *new_password);

// 在根目录下创建 /home、/root、/etc 三个 POSIX 目录。
// 供 format 阶段调用；owner 参数填入 uid/gid。
int user_create_posix_dirs(uint16_t owner_uid, uint16_t owner_gid);

// 为指定用户创建家目录（/home/username），owner 设为 uid:gid。
int user_create_home(const char *username, uint16_t uid, uint16_t gid);

// 哈希口令 + 盐值，输出十六进制字符串到 hex_out（至少 USER_HASH_HEX_LEN 字节）。
void user_hash_password(const char *password, const char *salt_hex, char *hex_out);

// 生成随机盐值十六进制字符串（至少 USER_SALT_HEX_LEN 字节）。
void user_gen_salt(char *hex_out);

#ifdef __cplusplus
}
#endif

#endif // USER_MGMT_H
