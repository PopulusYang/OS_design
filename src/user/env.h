





#ifndef ENV_H
#define ENV_H

#ifdef __cplusplus
extern "C" {
#endif

#define ENV_MAX_VARS     64
#define ENV_MAX_KEY      64
#define ENV_MAX_VAL      256
#define ENV_LINE_MAX     (ENV_MAX_KEY + ENV_MAX_VAL + 2)


int  env_init(void);


int  env_system_load(void);


int  env_system_save(void);


int  env_user_load(const char *username);


int  env_user_save(const char *username);


const char *env_get(const char *name);


int  env_set(const char *name, const char *value);


int  env_unset(const char *name);



void env_foreach(void (*callback)(const char *name, const char *value, int is_system));


const char *env_get_path(void);


void env_set_current_user(const char *username);

#ifdef __cplusplus
}
#endif

#endif 
