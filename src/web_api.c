#include "serve.h"
#include "vfs.h"
#include "fs/allocator.h"
#include "fs/bg.h"
#include "fs/inomap.h"
#include "fs/dir_sys.h"
#include "fs/extent.h"
#include "fs/buf.h"
#include "fs/disk_io.h"
#include "user/user_mgmt.h"
#include "kernel/memory.h"
#include "kernel/process.h"
#include "kernel/scheduler.h"
#include "kernel_shared.h"
#include "vfs_core.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

/* ── minimal JSON parser ─────────────────────────────────────────── */

#define JMAX_FIELDS 8
#define JMAX_VLEN   65536

typedef struct {
    int  count;
    char keys[JMAX_FIELDS][32];
    char vals[JMAX_FIELDS][JMAX_VLEN];
} JsonObj;

static int json_parse(const char *s, JsonObj *j)
{
    j->count = 0;
    while (*s && *s != '{') s++;
    if (*s != '{') return -1;
    s++;

    for (;;) {
        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
        if (*s == '}' || *s == '\0') break;
        if (*s == ',') { s++; continue; }
        if (*s != '"') return -1;
        s++;

        int ki = j->count;
        if (ki >= JMAX_FIELDS) return -1;
        int k = 0;
        while (*s && *s != '"' && k < 31) j->keys[ki][k++] = *s++;
        j->keys[ki][k] = '\0';
        if (*s == '"') s++;

        while (*s == ' ' || *s == '\t') s++;
        if (*s != ':') return -1;
        s++;
        while (*s == ' ' || *s == '\t') s++;

        int v = 0;
        if (*s == '"') {
            /* string value */
            s++;
            while (*s && v < JMAX_VLEN - 1) {
                if (*s == '\\' && *(s + 1)) {
                    s++;
                    switch (*s) {
                    case '"':  j->vals[ki][v++] = '"';  break;
                    case '\\': j->vals[ki][v++] = '\\'; break;
                    case 'n':  j->vals[ki][v++] = '\n'; break;
                    case 't':  j->vals[ki][v++] = '\t'; break;
                    case '/':  j->vals[ki][v++] = '/';  break;
                    default:   j->vals[ki][v++] = *s;   break;
                    }
                    s++;
                } else if (*s == '"') {
                    break;
                } else {
                    j->vals[ki][v++] = *s++;
                }
            }
            if (*s == '"') s++;
        } else if (*s == '-' || (*s >= '0' && *s <= '9')) {
            /* number / boolean / null → store as-is */
            while (*s && v < JMAX_VLEN - 1 &&
                   (*s == '-' || (*s >= '0' && *s <= '9') ||
                    (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z'))) {
                j->vals[ki][v++] = *s++;
            }
        } else {
            return -1;
        }
        j->vals[ki][v] = '\0';
        j->count++;
    }
    return 0;
}

static const char *json_get(const JsonObj *j, const char *key)
{
    for (int i = 0; i < j->count; i++)
        if (strcmp(j->keys[i], key) == 0)
            return j->vals[i];
    return NULL;
}

/* ── JSON string escaping ────────────────────────────────────────── */

static void json_print_str(const char *s, int len)
{
    putchar('"');
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"':  fputs("\\\"", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '\n': fputs("\\n",  stdout); break;
        case '\r': fputs("\\r",  stdout); break;
        case '\t': fputs("\\t",  stdout); break;
        default:
            if (c < 0x20)
                printf("\\u%04x", c);
            else
                putchar(c);
            break;
        }
    }
    putchar('"');
}

/* ── response helpers ────────────────────────────────────────────── */

static int g_req_cb = -1;

static void api_start_response(void)
{
    if (g_req_cb >= 0)
        printf("{\"_cb\":%d", g_req_cb);
    else
        fputs("{\"_cb\":1", stdout);
}

static void api_ok(void)  { api_start_response(); puts(",\"ok\":true}"); }
static int  api_mount_fs(void);
static void api_err(const char *msg)
{
    api_start_response();
    fputs(",\"ok\":false,\"error\":", stdout);
    json_print_str(msg, (int)strlen(msg));
    puts("}");
}

/* ── state name helper ───────────────────────────────────────────── */

static const char *proc_state_str(int s)
{
    switch (s) {
    case PROC_FREE:    return "free";
    case PROC_READY:   return "ready";
    case PROC_RUNNING: return "running";
    case PROC_BLOCKED: return "blocked";
    case PROC_ZOMBIE:  return "zombie";
    default:           return "unknown";
    }
}

/* ── cmd: ls ─────────────────────────────────────────────────────── */

static void cmd_ls(const char *path)
{
    MemINode *dir_ip = namei(path);
    if (!dir_ip) { api_err("path not found"); return; }
    if (!(dir_ip->m_dinode.d_mode & IFDIR)) {
        iput(dir_ip);
        api_err("not a directory");
        return;
    }

    api_start_response(); fputs(",\"ok\":true,\"entries\":[", stdout);
    uint32_t size = dir_ip->m_dinode.d_size;
    char block_buf[BLOCK_SIZE];
    int first = 1;

    for (uint32_t pos = 0; pos + DIR_ENTRY_SIZE <= size; pos += DIR_ENTRY_SIZE) {
        uint32_t lblk = pos / BLOCK_SIZE;
        uint32_t off  = pos % BLOCK_SIZE;
        uint16_t phys;
        if (extent_lookup(&dir_ip->m_dinode, lblk, &phys) < 0) continue;
        read_block((int)phys, block_buf);
        DirEntry *de = (DirEntry *)(block_buf + off);
        if (de->de_inode == 0) continue;

        char name[MAX_FILENAME_LEN + 1];
        memcpy(name, de->de_name, MAX_FILENAME_LEN);
        name[MAX_FILENAME_LEN] = '\0';

        MemINode *eip = iget(de->de_inode);
        if (!eip) continue;

        if (!first) putchar(',');
        first = 0;
        printf("{\"name\":");
        json_print_str(name, (int)strlen(name));
        printf(",\"ino\":%u,\"mode\":%u,\"size\":%u,\"type\":\"%s\","
               "\"nlink\":%u,\"uid\":%u,\"gid\":%u}",
               (unsigned)de->de_inode,
               (unsigned)eip->m_dinode.d_mode,
               (unsigned)eip->m_dinode.d_size,
               (eip->m_dinode.d_mode & IFDIR) ? "d" : "f",
               (unsigned)eip->m_dinode.d_nlink,
               (unsigned)eip->m_dinode.d_uid,
               (unsigned)eip->m_dinode.d_gid);
        iput(eip);
    }

    iput(dir_ip);
    puts("]}");
}

/* ── cmd: stat ───────────────────────────────────────────────────── */

static void cmd_stat(const char *path)
{
    uint16_t mode, nlink, uid, gid, ino;
    uint32_t size;
    if (vfs_stat(path, &mode, &size, &nlink, &uid, &gid, &ino) < 0) {
        api_err("stat failed");
        return;
    }
    api_start_response(); printf(",\"ok\":true,\"ino\":%u,\"mode\":%u,\"size\":%u,"
           "\"type\":\"%s\",\"nlink\":%u,\"uid\":%u,\"gid\":%u}\n",
           (unsigned)ino, (unsigned)mode, (unsigned)size,
           (mode & IFDIR) ? "dir" : "file",
           (unsigned)nlink, (unsigned)uid, (unsigned)gid);
}

/* ── cmd: cat ────────────────────────────────────────────────────── */

static int api_file_is_binary(const char *path)
{
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) return 0;
    unsigned char hdr[16];
    int n = vfs_read(fd, (char *)hdr, (int)sizeof(hdr));
    vfs_close(fd);
    if (n >= 4 && hdr[0] == 'U' && hdr[1] == 'P' && hdr[2] == 'X' && hdr[3] == '\0')
        return 1;
    for (int i = 0; i < n; i++) {
        if (hdr[i] == '\0')
            return 1;
    }
    return 0;
}

static void cmd_cat(const char *path)
{
    if (api_file_is_binary(path)) {
        api_err("binary file (cannot open)");
        return;
    }

    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) { api_err("open failed"); return; }

    api_start_response(); fputs(",\"ok\":true,\"data\":\"", stdout);

    char buf[BLOCK_SIZE];
    int n;
    while ((n = vfs_read(fd, buf, (int)sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            unsigned char c = (unsigned char)buf[i];
            switch (c) {
            case '"':  fputs("\\\"", stdout); break;
            case '\\': fputs("\\\\", stdout); break;
            case '\n': fputs("\\n",  stdout); break;
            case '\r': fputs("\\r",  stdout); break;
            case '\t': fputs("\\t",  stdout); break;
            default:
                if (c < 0x20)
                    printf("\\u%04x", c);
                else
                    putchar(c);
                break;
            }
        }
    }

    puts("\"}");
    vfs_close(fd);
}

/* ── cmd: mkdir ──────────────────────────────────────────────────── */

static void cmd_mkdir(const char *path)
{
    if (vfs_mkdir(path, IFDIR | 0755) < 0)
        api_err("mkdir failed");
    else
        api_ok();
}

/* ── cmd: create ─────────────────────────────────────────────────── */

static void cmd_create(const char *path)
{
    if (vfs_create(path, IFREG | 0644) < 0)
        api_err("create failed");
    else
        api_ok();
}

/* ── cmd: rm ─────────────────────────────────────────────────────── */

static void cmd_rm(const char *path)
{
    if (vfs_delete(path) < 0)
        api_err("rm failed");
    else
        api_ok();
}

/* ── cmd: write ──────────────────────────────────────────────────── */

static int api_truncate_path(const char *path)
{
    MemINode *ip;

    ip = namei(path);
    if (ip == NULL)
        return -1;
    if (!(ip->m_dinode.d_mode & IFREG)) {
        iput(ip);
        return -1;
    }
    if (inode_wrlock(ip) != 0) {
        iput(ip);
        return -1;
    }
    extent_clear(ip);
    inode_unlock(ip);
    iput(ip);
    return 0;
}

static void cmd_write(const char *path, const char *data)
{
    int fd;
    int len;
    int written;
    uint16_t mode, nlink, uid, gid, ino;
    uint32_t old_size;

    if (!data)
        data = "";

    len = (int)strlen(data);

    if (vfs_stat(path, &mode, &old_size, &nlink, &uid, &gid, &ino) == 0) {
        if (api_truncate_path(path) != 0) {
            api_err("truncate failed");
            return;
        }
    } else {
        if (vfs_create(path, IFREG | 0644) < 0) {
            api_err("create failed");
            return;
        }
    }

    if (len == 0) {
        if (vfs_sync_all() != 0) {
            api_err("sync failed");
            return;
        }
        api_ok();
        return;
    }

    fd = vfs_open(path, O_WRONLY);
    if (fd < 0) {
        api_err("open failed");
        return;
    }

    written = vfs_write(fd, data, len);
    vfs_close(fd);

    if (written < len) {
        char msg[128];
        snprintf(msg, sizeof(msg), "short write: wrote %d of %d bytes", written, len);
        api_err(msg);
        return;
    }

    if (vfs_sync_all() != 0) {
        api_err("sync failed");
        return;
    }
    api_ok();
}

/* ── cmd: debug super ────────────────────────────────────────────── */

static void cmd_debug_super(void)
{
    const SuperBlock *sb;

    api_mount_fs();
    sb = fs_get_superblock();
    if (!sb) { api_err("not mounted"); return; }

    uint32_t inodes_used = sb->s_inode_total - sb->s_inode_free_count;
    api_start_response();
    printf(",\"ok\":true,"
           "\"magic\":\"0x%08X\","
           "\"inodes_total\":%u,"
           "\"inodes_used\":%u,"
           "\"blocks_total\":%u,"
           "\"blocks_free\":%u,"
           "\"bg\":[",
           sb->s_magic,
           sb->s_inode_total, inodes_used,
           sb->s_block_total, sb->s_block_free_count);

    for (int i = 0; i < BG_COUNT; i++) {
        if (i) putchar(',');
        const BlockGroupDesc *bg = &sb->s_bg_table[i];
        uint32_t gfree = bg_group_free(i);
        printf("{\"anchor\":%u,\"data_start\":%u,\"data_blocks\":%u,\"free\":%u}",
               (unsigned)bg->bgd_anchor_block,
               (unsigned)bg->bgd_data_start,
               (unsigned)bg->bgd_data_blocks,
               gfree);
    }
    puts("]}");
}

/* ── cmd: debug process ──────────────────────────────────────────── */

static void cmd_debug_process(void)
{
    int count = 0;
    PCB *table;

    api_mount_fs();
    table = proc_get_table(&count);

    api_start_response(); fputs(",\"ok\":true,\"procs\":[", stdout);
    int first = 1;
    for (int i = 0; i < count; i++) {
        if (table[i].p_state == PROC_FREE) continue;
        if (!first) putchar(',');
        first = 0;
        printf("{\"pid\":%u,\"ppid\":%u,\"state\":%u,\"name\":",
               table[i].p_pid, table[i].p_ppid,
               (unsigned)table[i].p_state);
        json_print_str(table[i].p_name, (int)strlen(table[i].p_name));
        putchar('}');
    }
    puts("]}");
}

/* ── cmd: debug memory ───────────────────────────────────────────── */

static void cmd_debug_memory(void)
{
    int free_pages;

    api_mount_fs();
    free_pages = mem_free_page_count();
    int total = 0;
    const uint8_t *bm = mem_get_page_bitmap(&total);
    int used = total - free_pages;

    api_start_response();
    printf(",\"ok\":true,"
           "\"total_pages\":%d,"
           "\"used_pages\":%d,"
           "\"kernel_pages\":%u,"
           "\"page_size\":%u,",
           total, used, (unsigned)MEM_KERNEL_PAGES, (unsigned)MEM_PAGE_SIZE);

    /* Downsampled bitmap string: 1 char per step pages */
    printf("\"bitmap\":\"");
    if (bm && total > 0) {
        int step = total > 1024 ? total / 1024 : 1;
        for (int i = 0; i * step < total && i < 1024; i++) {
            int pg = i * step;
            int bit = (bm[pg / 8] >> (pg % 8)) & 1;
            putchar(bit ? '1' : '0');
        }
    }
    puts("\"}");
}

/* ── cmd: debug all ─────────────────────────────────────────────── */

static void cmd_debug_all(void)
{
    const SuperBlock *sb = fs_get_superblock();
    int count = 0;
    PCB *table = proc_get_table(&count);
    int free_pages = mem_free_page_count();
    int total_pg = 0;
    const uint8_t *bm = mem_get_page_bitmap(&total_pg);
    int used_pg = total_pg - free_pages;

    api_start_response(); fputs(",\"ok\":true", stdout);

    /* super */
    if (sb) {
        uint32_t inodes_used = sb->s_inode_total - sb->s_inode_free_count;
        printf(",\"inodes_total\":%u,\"inodes_used\":%u,"
               "\"blocks_total\":%u,\"blocks_free\":%u,\"bg\":[",
               sb->s_inode_total, inodes_used,
               sb->s_block_total, sb->s_block_free_count);
        for (int i = 0; i < BG_COUNT; i++) {
            if (i) putchar(',');
            const BlockGroupDesc *bg = &sb->s_bg_table[i];
            printf("{\"anchor\":%u,\"data_start\":%u,\"data_blocks\":%u,\"free\":%u}",
                   (unsigned)bg->bgd_anchor_block, (unsigned)bg->bgd_data_start,
                   (unsigned)bg->bgd_data_blocks, bg_group_free(i));
        }
        printf("]");
    }

    /* process */
    printf(",\"procs\":[");
    int first = 1;
    for (int i = 0; i < count; i++) {
        if (table[i].p_state == PROC_FREE) continue;
        if (!first) putchar(',');
        first = 0;
        printf("{\"pid\":%u,\"ppid\":%u,\"state\":%u,\"name\":",
               table[i].p_pid, table[i].p_ppid, (unsigned)table[i].p_state);
        json_print_str(table[i].p_name, (int)strlen(table[i].p_name));
        putchar('}');
    }
    printf("]");

    /* memory */
    printf(",\"total_pages\":%d,\"used_pages\":%d,\"kernel_pages\":%u,\"page_size\":%u",
           total_pg, used_pg, (unsigned)MEM_KERNEL_PAGES, (unsigned)MEM_PAGE_SIZE);
    printf(",\"bitmap\":\"");
    if (bm && total_pg > 0) {
        int step = total_pg > 1024 ? total_pg / 1024 : 1;
        for (int i = 0; i * step < total_pg && i < 1024; i++) {
            int pg = i * step;
            putchar((bm[pg / 8] >> (pg % 8)) & 1 ? '1' : '0');
        }
    }
    puts("\"}");
}

/* ── dispatch ────────────────────────────────────────────────────── */

static int g_mounted = 0;

static int path_readable(const char *path)
{
    FILE *fp;

    if (path == NULL || path[0] == '\0')
        return 0;
    fp = fopen(path, "rb");
    if (fp == NULL)
        return 0;
    fclose(fp);
    return 1;
}

static int api_mount_fs(void)
{
    const char *candidates[8];
    int n = 0;
    int i, j;
    const char *shared;
    static const char *fallbacks[] = {
        "testimg/vfs_disk.img",
        "./testimg/vfs_disk.img",
        "../testimg/vfs_disk.img",
        "../../testimg/vfs_disk.img",
        NULL
    };

    if (g_mounted)
        return 0;

    shared = shared_disk_path();
    if (shared && shared[0])
        candidates[n++] = shared;
    for (i = 0; fallbacks[i] != NULL; i++) {
        int dup = 0;
        for (j = 0; j < n; j++) {
            if (strcmp(candidates[j], fallbacks[i]) == 0) {
                dup = 1;
                break;
            }
        }
        if (!dup)
            candidates[n++] = fallbacks[i];
    }

    for (i = 0; i < n; i++) {
        if (!path_readable(candidates[i]))
            continue;
        if (vfs_mount(candidates[i]) != 0)
            continue;
        (void)user_init();
        g_mounted = 1;
        if (g_kernel != NULL && proc_find(0) == NULL) {
            (void)mem_init();
            (void)proc_init();
            (void)sched_init();
            (void)proc_create_init();
        }
        return 0;
    }
    return -1;
}

static void dispatch(const JsonObj *j)
{
    const char *cmd  = json_get(j, "cmd");
    const char *path = json_get(j, "path");
    const char *type = json_get(j, "type");
    const char *cb_id = json_get(j, "_cb");

    g_req_cb = (cb_id && cb_id[0]) ? atoi(cb_id) : -1;

    if (!cmd) { api_err("missing cmd"); return; }

    api_mount_fs();
    if (!g_mounted && strcmp(cmd, "debug") != 0) {
        api_err("not mounted");
        return;
    }

    if (g_mounted)
        fs_reload_super();

    if (strcmp(cmd, "ls") == 0) {
        cmd_ls(path ? path : "/");
    } else if (strcmp(cmd, "stat") == 0) {
        if (!path) { api_err("missing path"); return; }
        cmd_stat(path);
    } else if (strcmp(cmd, "cat") == 0) {
        if (!path) { api_err("missing path"); return; }
        cmd_cat(path);
    } else if (strcmp(cmd, "mkdir") == 0) {
        if (!path) { api_err("missing path"); return; }
        cmd_mkdir(path);
    } else if (strcmp(cmd, "create") == 0) {
        if (!path) { api_err("missing path"); return; }
        cmd_create(path);
    } else if (strcmp(cmd, "rm") == 0) {
        if (!path) { api_err("missing path"); return; }
        cmd_rm(path);
    } else if (strcmp(cmd, "write") == 0) {
        if (!path) { api_err("missing path"); return; }
        cmd_write(path, json_get(j, "data"));
    } else if (strcmp(cmd, "debug") == 0) {
        if (!type) { api_err("missing type"); return; }
        if (strcmp(type, "super") == 0)        cmd_debug_super();
        else if (strcmp(type, "process") == 0)  cmd_debug_process();
        else if (strcmp(type, "memory") == 0)   cmd_debug_memory();
        else if (strcmp(type, "all") == 0)      cmd_debug_all();
        else api_err("unknown debug type");
    } else {
        api_err("unknown cmd");
    }
}

/* ── entry point ─────────────────────────────────────────────────── */

extern KernelShared *g_kernel;

int upfs_api_session(int in_fd, int out_fd)
{
    dup2(in_fd, 0);
    dup2(out_fd, 1);
    dup2(out_fd, 2);
    setvbuf(stdout, NULL, _IONBF, 0);

    vfs_upfs_register();
    if (g_kernel == NULL)
        (void)kernel_local_init();

    static User g_api_user;
    memset(&g_api_user, 0, sizeof(g_api_user));
    g_api_user.u_uid  = 0;
    g_api_user.u_gid  = 0;
    g_api_user.u_cdir = ROOT_INODE_NO;
    dir_bind_user(&g_api_user);

    char line[65536];
    while (fgets(line, (int)sizeof(line), stdin)) {
        int len = (int)strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len == 0) continue;

        JsonObj j;
        if (json_parse(line, &j) < 0) {
            api_err("invalid json");
            continue;
        }
        dispatch(&j);
        fflush(stdout);
    }

    return 0;
}
