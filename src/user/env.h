/*
 * env.h
 * 环境变量的内存存储与持久化接口。
 */
#ifndef ENV_H
#define ENV_H

#ifdef __cplusplus
extern "C" {
#endif

#define ENV_MAX_VARS     64
#define ENV_MAX_KEY      64
#define ENV_MAX_VAL      256
#define ENV_LINE_MAX     (ENV_MAX_KEY + ENV_MAX_VAL + 2)

// 清空系统与用户环境变量表
int  env_init(void);

// 从 /etc/environment 加载系统环境变量
int  env_system_load(void);

// 把系统环境变量写回 /etc/environment
int  env_system_save(void);

// 从用户主目录 .env 加载个人环境变量
int  env_user_load(const char *username);

// 把个人环境变量写回用户 .env 文件
int  env_user_save(const char *username);

// 按名查找变量，用户表优先于系统表
const char *env_get(const char *name);

// 设置或新增环境变量并持久化到对应文件
int  env_set(const char *name, const char *value);

// 删除环境变量并写回文件
int  env_unset(const char *name);

// 遍历系统与用户变量并回调
void env_foreach(void (*callback)(const char *name, const char *value, int is_system));

// 取 PATH 变量，缺省为 /bin
const char *env_get_path(void);

// 记录当前登录用户名，供用户级环境变量保存
void env_set_current_user(const char *username);

#ifdef __cplusplus
}
#endif

#endif
