




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





int user_init(void);



int user_db_load(void);


int user_db_save(void);



int user_add(const char *username, const char *password);


int user_verify(const char *username, const char *password);


const UserAccount *user_find(const char *username);


const UserAccount *user_find_by_uid(uint16_t uid);


int user_count(void);


const UserAccount *user_get(int index);


int user_delete(const char *username);


int user_passwd(const char *username, const char *new_password);



int user_create_posix_dirs(uint16_t owner_uid, uint16_t owner_gid);


int user_create_home(const char *username, uint16_t uid, uint16_t gid);


void user_hash_password(const char *password, const char *salt_hex, char *hex_out);


void user_gen_salt(char *hex_out);

#ifdef __cplusplus
}
#endif

#endif 
