






#include "user/user_mgmt.h"
#include "vfs.h"
#include "fs/dir_sys.h"
#include "fs/allocator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>



static UserAccount g_users[MAX_USERS];
static int         g_user_count = 0;
static int         g_user_inited = 0;


#define PASSWD_PATH     "/etc/passwd"


#define PASSWD_LINE_MAX 256



static const char HEX_CHARS[] = "0123456789abcdef";

static void bytes_to_hex(const uint8_t *bytes, int len, char *hex_out)
{
    int i;

    for (i = 0; i < len; i++) {
        hex_out[i * 2]     = HEX_CHARS[bytes[i] >> 4];
        hex_out[i * 2 + 1] = HEX_CHARS[bytes[i] & 0x0F];
    }
    hex_out[len * 2] = '\0';
}

static int hex_to_bytes(const char *hex, uint8_t *bytes_out, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        char c_hi = hex[i * 2];
        char c_lo = hex[i * 2 + 1];

        if (c_hi == '\0' || c_lo == '\0') {
            return -1;
        }
        {
            int hi = c_hi >= '0' && c_hi <= '9' ? c_hi - '0'
                   : c_hi >= 'a' && c_hi <= 'f' ? c_hi - 'a' + 10
                   : c_hi >= 'A' && c_hi <= 'F' ? c_hi - 'A' + 10 : -1;
            int lo = c_lo >= '0' && c_lo <= '9' ? c_lo - '0'
                   : c_lo >= 'a' && c_lo <= 'f' ? c_lo - 'a' + 10
                   : c_lo >= 'A' && c_lo <= 'F' ? c_lo - 'A' + 10 : -1;

            if (hi < 0 || lo < 0) {
                return -1;
            }
            bytes_out[i] = (uint8_t)((hi << 4) | lo);
        }
    }
    return 0;
}




#define HASH_STATE_WORDS    8
#define HASH_ROUNDS         10000

static void hash_state_mix(uint32_t *s, const uint8_t *data, int data_len)
{
    int i;
    int di = 0;

    for (i = 0; i < HASH_ROUNDS; i++) {
        uint8_t b = data[di];
        int     wi = (int)(b & 0x07);

        di++;
        if (di >= data_len) {
            di = 0;
        }

        s[wi] ^= (uint32_t)b;
        s[wi] ^= s[(wi + 7) % HASH_STATE_WORDS];
        s[wi] = (s[wi] << (b & 0x1F)) | (s[wi] >> (32 - (b & 0x1F)));
        s[wi] += s[(wi + 5) % HASH_STATE_WORDS];
        s[(wi + 3) % HASH_STATE_WORDS] ^= s[wi];
        s[(wi + 2) % HASH_STATE_WORDS] += s[(wi + 1) % HASH_STATE_WORDS];
    }
}

void user_hash_password(const char *password, const char *salt_hex, char *hex_out)
{
    uint32_t state[HASH_STATE_WORDS];
    uint8_t  salt_bytes[USER_SALT_LEN];
    uint8_t  combined[128];
    int      pass_len;
    int      combined_len;
    int      i;

    if (password == NULL || salt_hex == NULL || hex_out == NULL) {
        return;
    }

    
    hex_to_bytes(salt_hex, salt_bytes, USER_SALT_LEN);

    
    for (i = 0; i < HASH_STATE_WORDS; i++) {
        state[i] = ((uint32_t)salt_bytes[(i * 4) % USER_SALT_LEN] << 24)
                 | ((uint32_t)salt_bytes[(i * 4 + 1) % USER_SALT_LEN] << 16)
                 | ((uint32_t)salt_bytes[(i * 4 + 2) % USER_SALT_LEN] << 8)
                 | ((uint32_t)salt_bytes[(i * 4 + 3) % USER_SALT_LEN]);
    }
    
    state[0] ^= 0x6A09E667U;
    state[4] ^= 0xBB67AE85U;

    
    pass_len = (int)strlen(password);
    combined_len = USER_SALT_LEN + pass_len;
    if (combined_len > (int)sizeof(combined)) {
        combined_len = (int)sizeof(combined);
    }
    memcpy(combined, salt_bytes, USER_SALT_LEN);
    memcpy(combined + USER_SALT_LEN, password, (size_t)(combined_len - USER_SALT_LEN));

    hash_state_mix(state, combined, combined_len);

    
    {
        uint8_t hash_bytes[USER_HASH_LEN];
        for (i = 0; i < HASH_STATE_WORDS; i++) {
            hash_bytes[i * 4]     = (uint8_t)(state[i] >> 24);
            hash_bytes[i * 4 + 1] = (uint8_t)(state[i] >> 16);
            hash_bytes[i * 4 + 2] = (uint8_t)(state[i] >> 8);
            hash_bytes[i * 4 + 3] = (uint8_t)(state[i]);
        }
        bytes_to_hex(hash_bytes, USER_HASH_LEN, hex_out);
    }
}

void user_gen_salt(char *hex_out)
{
    uint8_t salt[USER_SALT_LEN];
    FILE   *fp;
    int     i;

    if (hex_out == NULL) {
        return;
    }

    
    fp = fopen("/dev/urandom", "rb");
    if (fp != NULL) {
        size_t n = fread(salt, 1, sizeof(salt), fp);
        fclose(fp);
        if (n == sizeof(salt)) {
            bytes_to_hex(salt, USER_SALT_LEN, hex_out);
            return;
        }
    }

    
    {
        static uint64_t counter = 0;
        uint64_t t = (uint64_t)time(NULL);
        uint64_t c = (uint64_t)(clock()) + counter++;

        for (i = 0; i < USER_SALT_LEN; i++) {
            t = t * 1103515245U + 12345U;
            c = c * 6364136223846793005ULL + 1ULL;
            salt[i] = (uint8_t)((t ^ c) & 0xFF);
        }
    }

    bytes_to_hex(salt, USER_SALT_LEN, hex_out);
}



static int passwd_exists(void)
{
    int fd = vfs_open(PASSWD_PATH, O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    vfs_close(fd);
    return 1;
}

int user_db_load(void)
{
    int  fd;
    char buf[4096];  
    int  total;
    int  line_start;
    int  pos;

    g_user_count = 0;
    memset(g_users, 0, sizeof(g_users));

    if (!passwd_exists()) {
        g_user_inited = 1;
        return 0;
    }

    fd = vfs_open(PASSWD_PATH, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    total = vfs_read(fd, buf, (int)sizeof(buf) - 1);
    vfs_close(fd);

    if (total <= 0) {
        g_user_inited = 1;
        return 0;
    }
    buf[total] = '\0';

    line_start = 0;
    for (pos = 0; pos <= total; pos++) {
        if (buf[pos] != '\n' && buf[pos] != '\0') {
            continue;
        }

        if (g_user_count >= MAX_USERS) {
            break;
        }

        
        {
            UserAccount *ua = &g_users[g_user_count];
            char         line[PASSWD_LINE_MAX];
            char        *token;
            int           field;

            int linelen = pos - line_start;
            if (linelen <= 0) {
                line_start = pos + 1;
                continue;
            }
            if (linelen >= PASSWD_LINE_MAX) {
                linelen = PASSWD_LINE_MAX - 1;
            }
            memcpy(line, buf + line_start, (size_t)linelen);
            line[linelen] = '\0';

            field = 0;
            token = line;
            while (token != NULL && field < 5) {
                char *next = strchr(token, ':');
                if (next != NULL) {
                    *next = '\0';
                    next++;
                }

                switch (field) {
                case 0:
                    strncpy(ua->ua_name, token, sizeof(ua->ua_name) - 1);
                    break;
                case 1:
                    ua->ua_uid = (uint16_t)strtoul(token, NULL, 10);
                    break;
                case 2:
                    strncpy(ua->ua_passwd_hash, token, sizeof(ua->ua_passwd_hash) - 1);
                    break;
                case 3:
                    strncpy(ua->ua_salt, token, sizeof(ua->ua_salt) - 1);
                    break;
                case 4:
                    strncpy(ua->ua_home, token, sizeof(ua->ua_home) - 1);
                    break;
                }

                token = next;
                field++;
            }

            if (field >= 5 && ua->ua_name[0] != '\0') {
                ua->ua_gid = ua->ua_uid;
                g_user_count++;
            }
        }

        line_start = pos + 1;
    }

    g_user_inited = 1;
    return g_user_count;
}

int user_db_save(void)
{
    char line[PASSWD_LINE_MAX];
    int  fd;
    int  i;
    int  n;

    
    {
        MemINode *etc_ip = namei("/etc");
        if (etc_ip == NULL) {
            vfs_mkdir("/etc", 0755);
        } else {
            iput(etc_ip);
        }
    }

    
    if (passwd_exists()) {
        vfs_delete(PASSWD_PATH);
    }
    if (vfs_create(PASSWD_PATH, 0644) != 0) {
        return -1;
    }

    fd = vfs_open(PASSWD_PATH, O_WRONLY);
    if (fd < 0) {
        return -1;
    }

    for (i = 0; i < g_user_count; i++) {
        const UserAccount *ua = &g_users[i];
        n = snprintf(line, sizeof(line), "%s:%u:%s:%s:%s\n",
                     ua->ua_name,
                     (unsigned)ua->ua_uid,
                     ua->ua_passwd_hash,
                     ua->ua_salt,
                     ua->ua_home);
        if (n < 0 || n >= (int)sizeof(line)) {
            vfs_close(fd);
            return -1;
        }
        if (vfs_write(fd, line, n) != n) {
            vfs_close(fd);
            return -1;
        }
    }

    vfs_close(fd);
    return 0;
}



int user_init(void)
{
    if (g_user_inited) {
        return 0;
    }

    
    if (!passwd_exists()) {
        
        
        if (vfs_create(PASSWD_PATH, 0644) != 0) {
            vfs_mkdir("/etc", 0755);
            vfs_create(PASSWD_PATH, 0644);
        }
    }

    return user_db_load() >= 0 ? 0 : -1;
}

int user_add(const char *username, const char *password)
{
    char        salt_hex[USER_SALT_HEX_LEN];
    char        hash_hex[USER_HASH_HEX_LEN];
    uint16_t    uid;
    UserAccount *ua;

    if (username == NULL || password == NULL) {
        return -1;
    }
    if (strlen(username) == 0 || strlen(username) >= 31) {
        return -1;
    }
    if (g_user_count >= MAX_USERS) {
        return -1;
    }
    if (user_find(username) != NULL) {
        return -1;
    }

    
    if (strcmp(username, "root") == 0) {
        uid = 0;
    } else {
        uid = USER_UID_BASE;
        
        {
            int found;
            uint16_t try_uid;
            for (try_uid = USER_UID_BASE; try_uid < USER_UID_BASE + 100; try_uid++) {
                found = 0;
                int j;
                for (j = 0; j < g_user_count; j++) {
                    if (g_users[j].ua_uid == try_uid) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    uid = try_uid;
                    break;
                }
            }
        }
    }

    user_gen_salt(salt_hex);
    user_hash_password(password, salt_hex, hash_hex);

    ua = &g_users[g_user_count];
    strncpy(ua->ua_name, username, sizeof(ua->ua_name) - 1);
    ua->ua_name[sizeof(ua->ua_name) - 1] = '\0';
    ua->ua_uid = uid;
    ua->ua_gid = uid;
    strncpy(ua->ua_passwd_hash, hash_hex, sizeof(ua->ua_passwd_hash) - 1);
    strncpy(ua->ua_salt, salt_hex, sizeof(ua->ua_salt) - 1);

    
    {
        int n = snprintf(ua->ua_home, sizeof(ua->ua_home), "/home/%s", username);
        if (n < 0 || n >= (int)sizeof(ua->ua_home)) {
            return -1;
        }
    }

    
    if (strcmp(username, "root") == 0) {
        
        strncpy(ua->ua_home, "/root", sizeof(ua->ua_home) - 1);
        
    } else {
        if (user_create_home(username, uid, uid) != 0) {
            return -1;
        }
    }

    g_user_count++;
    if (user_db_save() != 0) return -1;
    fs_sync_disk();
    return 0;
}

int user_verify(const char *username, const char *password)
{
    const UserAccount *ua;
    char               expected[USER_HASH_HEX_LEN];

    ua = user_find(username);
    if (ua == NULL) {
        return -1;
    }

    user_hash_password(password, ua->ua_salt, expected);
    if (strcmp(expected, ua->ua_passwd_hash) == 0) {
        return (int)ua->ua_uid;
    }
    return -1;
}

const UserAccount *user_find(const char *username)
{
    int i;

    if (username == NULL) {
        return NULL;
    }
    for (i = 0; i < g_user_count; i++) {
        if (strcmp(g_users[i].ua_name, username) == 0) {
            return &g_users[i];
        }
    }
    return NULL;
}

const UserAccount *user_find_by_uid(uint16_t uid)
{
    int i;

    for (i = 0; i < g_user_count; i++) {
        if (g_users[i].ua_uid == uid) {
            return &g_users[i];
        }
    }
    return NULL;
}

int user_count(void)
{
    return g_user_count;
}

const UserAccount *user_get(int index)
{
    if (index < 0 || index >= g_user_count) {
        return NULL;
    }
    return &g_users[index];
}

int user_delete(const char *username)
{
    int idx;
    int i;

    if (username == NULL) {
        return -1;
    }

    idx = -1;
    for (i = 0; i < g_user_count; i++) {
        if (strcmp(g_users[i].ua_name, username) == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        return -1;
    }

    
    for (i = idx; i < g_user_count - 1; i++) {
        g_users[i] = g_users[i + 1];
    }
    memset(&g_users[g_user_count - 1], 0, sizeof(UserAccount));
    g_user_count--;

    return user_db_save();
}

int user_passwd(const char *username, const char *new_password)
{
    UserAccount *ua;
    char         salt_hex[USER_SALT_HEX_LEN];
    char         hash_hex[USER_HASH_HEX_LEN];

    if (username == NULL || new_password == NULL) {
        return -1;
    }
    
    {
        int i;
        ua = NULL;
        for (i = 0; i < g_user_count; i++) {
            if (strcmp(g_users[i].ua_name, username) == 0) {
                ua = &g_users[i];
                break;
            }
        }
    }
    if (ua == NULL) {
        return -1;
    }

    user_gen_salt(salt_hex);
    user_hash_password(new_password, salt_hex, hash_hex);

    strncpy(ua->ua_salt, salt_hex, sizeof(ua->ua_salt) - 1);
    strncpy(ua->ua_passwd_hash, hash_hex, sizeof(ua->ua_passwd_hash) - 1);

    return user_db_save();
}

int user_create_posix_dirs(uint16_t owner_uid, uint16_t owner_gid)
{
    (void)owner_uid;
    (void)owner_gid;

    if (vfs_mkdir("/bin",  0755) != 0) return -1;
    if (vfs_mkdir("/home", 0755) != 0) return -1;
    if (vfs_mkdir("/root", 0700) != 0) return -1;
    if (vfs_mkdir("/etc",  0755) != 0) return -1;
    return 0;
}

int user_create_home(const char *username, uint16_t uid, uint16_t gid)
{
    char path[128];
    int  n;

    (void)uid;
    (void)gid;

    if (username == NULL || username[0] == '\0') {
        return -1;
    }
    n = snprintf(path, sizeof(path), "/home/%s", username);
    if (n < 0 || n >= (int)sizeof(path)) {
        return -1;
    }
    return vfs_mkdir(path, 0700);
}
