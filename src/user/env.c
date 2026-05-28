

#include "env.h"
#include "fs/file_sys.h"
#include "fs/dir_sys.h"
#include <string.h>
#include <stdio.h>

#define SYS_ENV_PATH    "/etc/environment"

typedef struct EnvVar {
    char key[ENV_MAX_KEY];
    char val[ENV_MAX_VAL];
    int  is_system;   
} EnvVar;

static EnvVar g_sys_env[ENV_MAX_VARS];
static int    g_sys_env_count = 0;

static EnvVar g_user_env[ENV_MAX_VARS];
static int    g_user_env_count = 0;

static char   g_current_user[32] = "";

void env_set_current_user(const char *username)
{
    if (username)
        strncpy(g_current_user, username, sizeof(g_current_user) - 1);
    else
        g_current_user[0] = '\0';
}



static int env_parse_line(char *line, EnvVar *var)
{
    char *eq = strchr(line, '=');
    if (eq == NULL) return -1;
    *eq = '\0';
    char *key = line;
    char *val = eq + 1;
    
    while (*key == ' ') key++;
    char *ke = key + strlen(key) - 1;
    while (ke > key && *ke == ' ') { *ke = '\0'; ke--; }
    if (*key == '\0') return -1;

    strncpy(var->key, key, ENV_MAX_KEY - 1);
    strncpy(var->val, val, ENV_MAX_VAL - 1);
    return 0;
}

static int env_load_file(const char *path, EnvVar *arr, int *count, int is_system)
{
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) return -1;

    char buf[4096];
    int n = vfs_read(fd, buf, (int)sizeof(buf) - 1);
    vfs_close(fd);
    if (n <= 0) return 0;

    buf[n] = '\0';
    int pos = 0, line_start = 0;

    while (pos <= n) {
        if (buf[pos] == '\n' || buf[pos] == '\0') {
            buf[pos] = '\0';
            if (pos > line_start && *count < ENV_MAX_VARS) {
                if (env_parse_line(buf + line_start, &arr[*count]) == 0) {
                    arr[*count].is_system = is_system;
                    (*count)++;
                }
            }
            line_start = pos + 1;
        }
        pos++;
    }
    return 0;
}

static int env_save_file(const char *path, EnvVar *arr, int count)
{
    
    vfs_delete(path);
    if (vfs_create(path, 0644) != 0) return -1;

    int fd = vfs_open(path, O_WRONLY);
    if (fd < 0) return -1;

    char line[ENV_LINE_MAX];
    for (int i = 0; i < count; i++) {
        int n = snprintf(line, sizeof(line), "%s=%s\n", arr[i].key, arr[i].val);
        if (n > 0) vfs_write(fd, line, n);
    }
    vfs_close(fd);
    return 0;
}



int env_init(void)
{
    g_sys_env_count = 0;
    g_user_env_count = 0;
    memset(g_sys_env, 0, sizeof(g_sys_env));
    memset(g_user_env, 0, sizeof(g_user_env));
    return 0;
}

int env_system_load(void)
{
    g_sys_env_count = 0;
    return env_load_file(SYS_ENV_PATH, g_sys_env, &g_sys_env_count, 1);
}

int env_system_save(void)
{
    return env_save_file(SYS_ENV_PATH, g_sys_env, g_sys_env_count);
}

int env_user_load(const char *username)
{
    g_user_env_count = 0;
    if (username == NULL || username[0] == '\0') return 0;

    char path[256];
    snprintf(path, sizeof(path), "/home/%s/.env", username);
    return env_load_file(path, g_user_env, &g_user_env_count, 0);
}

int env_user_save(const char *username)
{
    if (username == NULL || username[0] == '\0') return -1;

    char path[256];
    snprintf(path, sizeof(path), "/home/%s/.env", username);
    return env_save_file(path, g_user_env, g_user_env_count);
}

const char *env_get(const char *name)
{
    
    for (int i = 0; i < g_user_env_count; i++)
        if (strcmp(g_user_env[i].key, name) == 0)
            return g_user_env[i].val;
    
    for (int i = 0; i < g_sys_env_count; i++)
        if (strcmp(g_sys_env[i].key, name) == 0)
            return g_sys_env[i].val;
    return NULL;
}


static int env_is_root(void)
{
    User *u = dir_get_user();
    return (u != NULL && u->u_uid == 0);
}

int env_set(const char *name, const char *value)
{
    if (name == NULL || value == NULL) return -1;

    EnvVar *arr    = env_is_root() ? g_sys_env   : g_user_env;
    int    *count  = env_is_root() ? &g_sys_env_count : &g_user_env_count;
    int     is_sys = env_is_root() ? 1 : 0;

    
    for (int i = 0; i < *count; i++) {
        if (strcmp(arr[i].key, name) == 0) {
            strncpy(arr[i].val, value, ENV_MAX_VAL - 1);
            if (is_sys) return env_system_save();
            else        return env_user_save(g_current_user);
        }
    }
    
    if (*count >= ENV_MAX_VARS) return -1;
    strncpy(arr[*count].key, name, ENV_MAX_KEY - 1);
    strncpy(arr[*count].val, value, ENV_MAX_VAL - 1);
    arr[*count].is_system = is_sys;
    (*count)++;
    if (is_sys) return env_system_save();
    else        return env_user_save(g_current_user);
}

int env_unset(const char *name)
{
    EnvVar *arr   = env_is_root() ? g_sys_env   : g_user_env;
    int    *count = env_is_root() ? &g_sys_env_count : &g_user_env_count;
    int     is_sys = env_is_root() ? 1 : 0;

    for (int i = 0; i < *count; i++) {
        if (strcmp(arr[i].key, name) == 0) {
            for (int j = i; j < *count - 1; j++)
                arr[j] = arr[j + 1];
            (*count)--;
            if (is_sys) return env_system_save();
            else        return env_user_save(g_current_user);
        }
    }
    return -1;
}

void env_foreach(void (*callback)(const char *name, const char *value, int is_system))
{
    if (callback == NULL) return;
    for (int i = 0; i < g_sys_env_count; i++)
        callback(g_sys_env[i].key, g_sys_env[i].val, 1);
    for (int i = 0; i < g_user_env_count; i++)
        callback(g_user_env[i].key, g_user_env[i].val, 0);
}

const char *env_get_path(void)
{
    const char *p = env_get("PATH");
    return p ? p : "/bin";
}
