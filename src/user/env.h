// env.h —— 环境变量系统：系统级 + 用户级，文件持久化
//
// 系统 env: /etc/environment  — 所有用户共享
// 用户 env: ~/.env           — 每个用户独立
// 格式: KEY=VALUE，每行一对

#ifndef ENV_H
#define ENV_H

#ifdef __cplusplus
extern "C" {
#endif

#define ENV_MAX_VARS     64
#define ENV_MAX_KEY      64
#define ENV_MAX_VAL      256
#define ENV_LINE_MAX     (ENV_MAX_KEY + ENV_MAX_VAL + 2)

// 初始化环境变量系统（挂载后调用）
int  env_init(void);

// 加载系统环境变量文件 (/etc/environment)
int  env_system_load(void);

// 保存系统环境变量
int  env_system_save(void);

// 加载指定用户的环境变量 (~/.env)
int  env_user_load(const char *username);

// 保存指定用户的环境变量
int  env_user_save(const char *username);

// 查询环境变量（先查用户 env，再查系统 env）
const char *env_get(const char *name);

// 设置当前用户的环境变量（立即写回磁盘）
int  env_set(const char *name, const char *value);

// 删除当前用户的环境变量
int  env_unset(const char *name);

// 列出所有环境变量（系统 + 用户，通过回调）
// callback 参数：name, value, is_system(0=user,1=system)
void env_foreach(void (*callback)(const char *name, const char *value, int is_system));

// 获取系统 PATH 变量值
const char *env_get_path(void);

// 当前用户名（用于选择用户 env）
void env_set_current_user(const char *username);

#ifdef __cplusplus
}
#endif

#endif // ENV_H
