

#include "vfs_core.h"
#include "vfs.h"
#include "fs/disk_io.h"
#include "fs/buf.h"
#include "fs/format.h"
#include "fs/allocator.h"
#include "fs/bg.h"
#include "fs/dir_sys.h"
#include "fs/extent.h"
#include "fs/file_sys.h"
#include "user/user_mgmt.h"
#include "user/env.h"
#include "kernel_shared.h"
#include "kernel/memory.h"
#include "kernel/process.h"
#include "kernel/scheduler.h"
#include "kernel/syscall.h"
#include "kernel/pipe.h"
#include "kernel/ipc.h"
#include "binaries.h"
#include "serve.h"
#include "assembler.h"
#include "editor.h"
#include "compiler/c2s.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <dirent.h>
#include <termios.h>


#define ANSI_ROSE           "\033[38;2;88;166;255m"   /* primary accent (blue) */
#define ANSI_MAUVE          "\033[38;2;120;170;230m" /* secondary accent */
#define ANSI_RESET          "\033[0m"
#define ANSI_BOLD           "\033[1m"
#define ANSI_DIM            "\033[2m"
#define ANSI_ERR            "\033[38;2;180;90;90m"
#define ANSI_OK             ANSI_ROSE

#define LINE_BUF_SIZE       1024
#define MAX_ARGS            32
#define CWD_BUF_SIZE        512


static User   g_users[MAX_USERS];
static int    g_current_user_idx = 0;
static char   g_disk_path[512] = DEFAULT_DISK_PATH;
static char   g_cwd[CWD_BUF_SIZE] = "/";
static char   g_user_home[256] = "/";
static int    g_mounted = 0;

static User *current_user(void)
{
    return &g_users[g_current_user_idx];
}



static void ui_banner(void)
{
    printf("\n%s%sUPFS%s%s . Unix File System Simulator%s\n\n",
           ANSI_BOLD, ANSI_ROSE, ANSI_RESET, ANSI_MAUVE, ANSI_RESET);
    fflush(stdout);
}

static void ui_ok(const char *msg)
{
    printf("%s%s  %s%s\n", ANSI_BOLD, ANSI_OK, msg, ANSI_RESET);
    fflush(stdout);
}

static void ui_info(const char *msg)
{
    printf("%s%s  %s%s\n", ANSI_DIM, ANSI_MAUVE, msg, ANSI_RESET);
}

static void ui_err(const char *msg)
{
    printf("%s%s  %s%s\n", ANSI_BOLD, ANSI_ERR, msg, ANSI_RESET);
}


static void cwd_display(char *out, size_t out_size)
{
    size_t home_len;

    if (out == NULL || out_size == 0) return;

    home_len = strlen(g_user_home);
    if (home_len > 0 && strncmp(g_cwd, g_user_home, home_len) == 0) {
        if (g_cwd[home_len] == '\0')
            snprintf(out, out_size, "~");
        else if (g_cwd[home_len] == '/')
            snprintf(out, out_size, "~%s", g_cwd + home_len);
        else {
            strncpy(out, g_cwd, out_size - 1);
            out[out_size - 1] = '\0';
        }
    } else {
        strncpy(out, g_cwd, out_size - 1);
        out[out_size - 1] = '\0';
    }
}

static void ui_prompt(void)
{
    char display[CWD_BUF_SIZE];
    User *u = current_user();

    cwd_display(display, sizeof(display));

    fputs(ANSI_BOLD ANSI_ROSE, stdout);
    printf("%s", u->u_name[0] ? u->u_name : "upfs");
    fputs(ANSI_RESET, stdout);
    fputs(ANSI_MAUVE ":", stdout);
    if (!g_mounted) {
        fputs(ANSI_DIM "(unmounted)" ANSI_RESET, stdout);
    } else {
        fputs(display, stdout);
        fputs(ANSI_RESET, stdout);
    }
    fputs(ANSI_DIM ANSI_MAUVE " >" ANSI_RESET " ", stdout);
}



static int disk_file_exists(const char *path)
{
    FILE *fp;
    if (path == NULL || path[0] == '\0') return 0;
    fp = fopen(path, "rb");
    if (fp == NULL) return 0;
    fclose(fp);
    return 1;
}

static int probe_disk_at(const char *search_dir)
{
    char candidate[512];
    int n = snprintf(candidate, sizeof(candidate), "%s/%s", search_dir, DEFAULT_DISK_PATH);
    if (n < 0 || (size_t)n >= sizeof(candidate)) return 0;
    if (!disk_file_exists(candidate)) return 0;
    strncpy(g_disk_path, candidate, sizeof(g_disk_path) - 1);
    g_disk_path[sizeof(g_disk_path) - 1] = '\0';
    return 1;
}

static void startup_disk_probe(void)
{
    const char *search_dirs[] = { ".", "..", "../.." };
    const char *found_at = NULL;

    ui_info("Scanning for disk image...");

    for (size_t i = 0; i < sizeof(search_dirs) / sizeof(search_dirs[0]); i++) {
        if (probe_disk_at(search_dirs[i])) { found_at = search_dirs[i]; break; }
    }

    if (found_at != NULL) {
        printf("\n");
        shared_set_disk(g_disk_path);
        ui_ok("Disk image found");
        printf("    %sLocation:%s %s/%s\n", ANSI_DIM, ANSI_RESET, found_at, DEFAULT_DISK_PATH);
        ui_info("Use  mount   to restore filesystem");
        ui_info("Use  format  to create a new one (destroys existing data)");
        printf("\n");
    } else {
        printf("\n");
        ui_info("No disk image found. Run:");
        printf("    %sformat%s  %s— create %s%s\n",
               ANSI_ROSE, ANSI_RESET, ANSI_DIM, g_disk_path, ANSI_RESET);
        printf("\n");
    }
}


static void init_root_user(void)
{
    User *u = current_user();
    memset(u, 0, sizeof(User));
    strncpy(u->u_name, "root", sizeof(u->u_name) - 1);
    u->u_uid = 0; u->u_gid = 0;
    u->u_cdir = ROOT_INODE_NO;
    u->u_active = 1;
}

static void shell_bind_user(void)
{
    dir_bind_user(current_user());
}




static int read_password(char *buf, int max_len)
{
    int tty_fd = fileno(stdin);
    struct termios old_tio, new_tio;

    if (tcgetattr(tty_fd, &old_tio) == 0) {
        new_tio = old_tio;
        new_tio.c_lflag &= (tcflag_t)~ECHO;
        tcsetattr(tty_fd, TCSAFLUSH, &new_tio);

        if (fgets(buf, max_len, stdin) == NULL) {
            tcsetattr(tty_fd, TCSAFLUSH, &old_tio);
            return -1;
        }
        tcsetattr(tty_fd, TCSAFLUSH, &old_tio);
    } else {
        
        if (fgets(buf, max_len, stdin) == NULL) return -1;
    }

    char *nl = strchr(buf, '\n');
    if (nl) *nl = '\0';
    printf("\n");
    return (int)strlen(buf);
}


static int shell_login_prompt(void)
{
    char user_buf[64], pass_buf[128];

    printf("\n");
    printf("%sUsername:%s ", ANSI_BOLD ANSI_MAUVE, ANSI_RESET);
    fflush(stdout);
    if (fgets(user_buf, (int)sizeof(user_buf), stdin) == NULL) return -1;
    { char *nl = strchr(user_buf, '\n'); if (nl) *nl = '\0'; }

    printf("%sPassword:%s ", ANSI_BOLD ANSI_MAUVE, ANSI_RESET);
    fflush(stdout);
    if (read_password(pass_buf, (int)sizeof(pass_buf)) < 0) return -1;

    int uid = user_verify(user_buf, pass_buf);
    if (uid < 0) {
        ui_err("Authentication failed");
        return -1;
    }
    return uid;
}


static int shell_create_first_user(void)
{
    char user_buf[64], pass_buf[128];
    int retry;

    printf("\n");
    ui_info("Create the first user account");

    for (retry = 0; retry < 3; retry++) {
        printf("%sUsername:%s ", ANSI_BOLD ANSI_MAUVE, ANSI_RESET);
        fflush(stdout);
        if (fgets(user_buf, (int)sizeof(user_buf), stdin) == NULL) return -1;
        { char *nl = strchr(user_buf, '\n'); if (nl) *nl = '\0'; }

        if (user_buf[0] == '\0') { ui_err("Username cannot be empty"); continue; }
        if (strlen(user_buf) > 30) { ui_err("Username too long (max 30 chars)"); continue; }
        break;
    }
    if (retry >= 3) return -1;

    for (retry = 0; retry < 3; retry++) {
        printf("%sPassword:%s ", ANSI_BOLD ANSI_MAUVE, ANSI_RESET);
        fflush(stdout);
        if (read_password(pass_buf, (int)sizeof(pass_buf)) < 0) return -1;

        if (pass_buf[0] == '\0') { ui_err("Password cannot be empty"); continue; }
        break;
    }
    if (retry >= 3) return -1;

    if (user_add(user_buf, pass_buf) != 0) {
        ui_err("Failed to create user");
        return -1;
    }

    
    {
        char root_pass[128];

        printf("%sSet root password:%s ", ANSI_BOLD ANSI_MAUVE, ANSI_RESET);
        fflush(stdout);
        if (read_password(root_pass, (int)sizeof(root_pass)) >= 0 && root_pass[0] != '\0') {
            user_add("root", root_pass); 
        }
    }

    
    {
        const UserAccount *ua = user_find(user_buf);
        if (ua == NULL) return -1;

        User *u = current_user();
        memset(u, 0, sizeof(User));
        strncpy(u->u_name, ua->ua_name, sizeof(u->u_name) - 1);
        u->u_uid = ua->ua_uid; u->u_gid = ua->ua_gid; u->u_active = 1;

        MemINode *home_ip = namei(ua->ua_home);
        u->u_cdir = home_ip ? home_ip->m_inode_no : ROOT_INODE_NO;
        if (home_ip) iput(home_ip);

        shell_bind_user();
        strncpy(g_user_home, ua->ua_home, sizeof(g_user_home) - 1);
        g_user_home[sizeof(g_user_home) - 1] = '\0';
        strncpy(g_cwd, ua->ua_home, CWD_BUF_SIZE - 1);
        g_cwd[CWD_BUF_SIZE - 1] = '\0';

        
        env_set_current_user(ua->ua_name);
        env_user_load(ua->ua_name);
    }

    printf("\n");
    ui_ok("User created and logged in");
    return 0;
}

static int shell_mount(const char *path)
{
    const char *use_path = (path && path[0]) ? path : g_disk_path;

    if (g_mounted) { ui_err("Already mounted, umount first"); return -1; }
    if (!disk_file_exists(use_path)) { ui_err("Disk image not found; format first"); return -1; }
    if (vfs_mount(use_path) != 0) { ui_err("Mount failed: corrupted image or wrong format"); return -1; }
    shared_set_disk(use_path);

    if (path && path[0] && path != g_disk_path) {
        strncpy(g_disk_path, use_path, sizeof(g_disk_path) - 1);
        g_disk_path[sizeof(g_disk_path) - 1] = '\0';
    }

    memset(g_users, 0, sizeof(g_users));
    g_current_user_idx = 0;
    init_root_user();
    shell_bind_user();
    strncpy(g_cwd, "/", sizeof(g_cwd) - 1);
    g_user_home[0] = '\0';
    g_mounted = 1;

    sys_open_file_init();
    if (proc_find(0) == NULL) {
        mem_init();
        proc_init();
        sched_init();
        env_init();
        env_system_load();
        proc_create_init();
    } else {
        env_init();
        env_system_load();
    }

    if (user_init() < 0)
        ui_info("Warning: could not load user database");

    if (user_count() > 0) {
        printf("\n");
        ui_info("Login required");
        int uid = shell_login_prompt();
        if (uid >= 0) {
            const UserAccount *ua = user_find_by_uid((uint16_t)uid);
            if (ua) {
                User *u = current_user();
                strncpy(u->u_name, ua->ua_name, sizeof(u->u_name) - 1);
                u->u_uid = ua->ua_uid; u->u_gid = ua->ua_gid;
                strncpy(g_user_home, ua->ua_home, sizeof(g_user_home) - 1);
                MemINode *home_ip = namei(ua->ua_home);
                if (home_ip) { u->u_cdir = home_ip->m_inode_no; iput(home_ip); }
                strncpy(g_cwd, ua->ua_home, CWD_BUF_SIZE - 1);
                g_cwd[CWD_BUF_SIZE - 1] = '\0';

                env_set_current_user(ua->ua_name);
                env_user_load(ua->ua_name);
            }
            ui_ok("Login successful");
        } else {
            ui_info("Login failed; operating as root");
        }
    } else {
        ui_ok("Mounted successfully (no users configured)");
    }
    return 0;
}

static int shell_umount(void)
{
    if (!g_mounted) { ui_err("Not mounted"); return -1; }
    user_db_save();
    env_system_save();
    proc_shutdown();
    mem_shutdown();
    if (vfs_umount() != 0) { ui_err("Umount failed"); return -1; }
    g_mounted = 0;
    strncpy(g_cwd, "/", sizeof(g_cwd) - 1);
    g_user_home[0] = '\0';
    ui_ok("Unmounted and saved");
    return 0;
}

static int shell_format(const char *path)
{
    const char *use_path = (path && path[0]) ? path : g_disk_path;

    if (g_mounted) { ui_err("Umount first before format"); return -1; }
    if (vfs_format_disk(use_path) != 0) { ui_err("Format failed"); return -1; }

    if (path && path[0] && path != g_disk_path) {
        strncpy(g_disk_path, use_path, sizeof(g_disk_path) - 1);
        g_disk_path[sizeof(g_disk_path) - 1] = '\0';
    }
    ui_ok("Format complete");

    
    if (vfs_mount(use_path) != 0) { ui_err("Mount after format failed"); return -1; }
    shared_set_disk(use_path);

    memset(g_users, 0, sizeof(g_users));
    g_current_user_idx = 0;
    init_root_user();
    shell_bind_user();
    strncpy(g_cwd, "/", sizeof(g_cwd) - 1);
    g_user_home[0] = '\0';
    g_mounted = 1;

    mem_init(); proc_init(); sched_init(); env_init();
    sys_open_file_init();
    env_system_load(); proc_create_init();

    
    if (user_create_posix_dirs(0, 0) != 0) {
        ui_err("Failed to create POSIX directories");
        return -1;
    }
    if (user_init() < 0) {
        ui_err("Failed to initialize user database");
        return -1;
    }

    
    {
        int bc = 0;
        const DemoBinary *demos = binaries_get_all(&bc);
        for (int i = 0; i < bc; i++) {
            int fd = vfs_open(demos[i].name, O_WRONLY);
            if (fd < 0) { vfs_create(demos[i].name, 0755); fd = vfs_open(demos[i].name, O_WRONLY); }
            if (fd >= 0) { vfs_write(fd, demos[i].data, (int)demos[i].size); vfs_close(fd); }
        }
    }

    
    {
        vfs_mkdir("/src", 0755);
        const char *search[] = { "involve_src", "../involve_src", "../../involve_src" };
        const char *src_dir = NULL;
        for (size_t si = 0; si < sizeof(search)/sizeof(search[0]); si++) {
            DIR *d = opendir(search[si]);
            if (d) { src_dir = search[si]; closedir(d); break; }
        }
        if (src_dir) {
            DIR *d = opendir(src_dir);
            if (d) {
                struct dirent *de;
                while ((de = readdir(d)) != NULL) {
                    size_t nl = strlen(de->d_name);
                    int is_asm = (nl > 4 && strcmp(de->d_name + nl - 4, ".asm") == 0);
                    int is_c   = (nl > 2 && strcmp(de->d_name + nl - 2, ".c") == 0);
                    if (is_asm || is_c) {
                        char host_path[512], vfs_path[512];
                        snprintf(host_path, sizeof(host_path), "%s/%s", src_dir, de->d_name);
                        snprintf(vfs_path,  sizeof(vfs_path),  "/src/%s", de->d_name);
                        FILE *f = fopen(host_path, "r");
                        if (f) {
                            fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
                            char *buf = malloc((size_t)(sz + 1));
                            if (buf) {
                                fread(buf, 1, (size_t)sz, f);
                                vfs_delete(vfs_path);
                                vfs_create(vfs_path, 0644);
                                int fd = vfs_open(vfs_path, O_WRONLY);
                                if (fd >= 0) { vfs_write(fd, buf, (int)sz); vfs_close(fd); }
                                free(buf);
                            }
                            fclose(f);
                        }
                    }
                }
                closedir(d);
            }
        }
    }

    
    env_set("PATH", "/bin:/usr/bin");
    env_system_save();

    
    if (shell_create_first_user() != 0) {
        ui_info("Falling back to root user");
        g_current_user_idx = 0;
        init_root_user();
        shell_bind_user();
        strncpy(g_user_home, "/root", sizeof(g_user_home) - 1);
    }

    return 0;
}




static void cwd_resolve_dots(void)
{
    char resolved[CWD_BUF_SIZE];
    char *dst = resolved;
    const char *src = g_cwd;

    while (*src) {
        while (*src == '/') src++;
        if (*src == '\0') break;

        const char *end = src;
        while (*end && *end != '/') end++;
        size_t len = (size_t)(end - src);

        if (len == 1 && src[0] == '.') {
            src = end;
            continue;
        }
        if (len == 2 && src[0] == '.' && src[1] == '.') {
            if (dst > resolved + 1) {
                dst--;
                while (dst > resolved && *(dst - 1) != '/') dst--;
            }
            src = end;
            continue;
        }
        *dst++ = '/';
        memmove(dst, src, len);
        dst += len;
        src = end;
    }

    if (dst == resolved) *dst++ = '/';
    *dst = '\0';
    strncpy(g_cwd, resolved, CWD_BUF_SIZE - 1);
    g_cwd[CWD_BUF_SIZE - 1] = '\0';
}

static void cwd_update_after_cd(const char *arg)
{
    if (arg == NULL || arg[0] == '\0') return;
    if (strcmp(arg, ".") == 0) return;

    char tmp[CWD_BUF_SIZE * 2];
    if (arg[0] == '/') {
        strncpy(g_cwd, arg, CWD_BUF_SIZE - 1);
    } else {
        if (strcmp(g_cwd, "/") == 0)
            snprintf(tmp, sizeof(tmp), "/%s", arg);
        else
            snprintf(tmp, sizeof(tmp), "%s/%s", g_cwd, arg);
        strncpy(g_cwd, tmp, CWD_BUF_SIZE - 1);
    }
    g_cwd[CWD_BUF_SIZE - 1] = '\0';
    cwd_resolve_dots();
}



static char *trim(char *s)
{
    char *end;
    if (s == NULL) return NULL;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) { *end = '\0'; end--; }
    return s;
}

static int parse_octal_mode(const char *s, uint16_t *out)
{
    if (s == NULL || out == NULL) return -1;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) return -1;
    char *end;
    long v = strtol(s, &end, 8);
    if (end == s || *end != '\0' || v < 0 || v > 0777) return -1;
    *out = (uint16_t)v;
    return 0;
}

static int parse_command_line(char *line, char **argv, int max_argc)
{
    int argc = 0;
    char *p = line, quote = 0;

    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (argc >= max_argc - 1) break;

        if (*p == '"' || *p == '\'') {
            quote = *p++;
            argv[argc++] = p;
            while (*p && *p != quote) p++;
            if (*p == quote) { *p = '\0'; p++; }
        } else {
            argv[argc++] = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            if (*p) { *p = '\0'; p++; }
        }
    }
    argv[argc] = NULL;
    return argc;
}



static void cmd_help(void)
{
    printf("\n");
    printf("%s%s  System%s\n", ANSI_BOLD, ANSI_MAUVE, ANSI_RESET);
    printf("  %sformat%s  [path]             format disk image (default %s)\n", ANSI_ROSE, ANSI_RESET, DEFAULT_DISK_PATH);
    printf("  %smount%s   [path]             mount / restore existing image\n", ANSI_ROSE, ANSI_RESET);
    printf("  %sumount%s                     unmount and save\n", ANSI_ROSE, ANSI_RESET);
    printf("\n");
    printf("%s%s  Directories%s\n", ANSI_BOLD, ANSI_MAUVE, ANSI_RESET);
    printf("  %smkdir%s   <path> [mode]       create directory (default 0755)\n", ANSI_ROSE, ANSI_RESET);
    printf("  %scd%s      [path]              change dir (no arg / ~ = go home)\n", ANSI_ROSE, ANSI_RESET);
    printf("  %spwd%s                         print working directory\n", ANSI_ROSE, ANSI_RESET);
    printf("  %sls%s      [path]              list directory contents\n", ANSI_ROSE, ANSI_RESET);
    printf("\n");
    printf("%s%s  Files%s\n", ANSI_BOLD, ANSI_MAUVE, ANSI_RESET);
    printf("  %screate%s  <path> [mode]       create regular file (default 0644)\n", ANSI_ROSE, ANSI_RESET);
    printf("  %swrite%s   <path> <data>        write text to file\n", ANSI_ROSE, ANSI_RESET);
    printf("  %scat%s     <path>               display file contents\n", ANSI_ROSE, ANSI_RESET);
    printf("  %srm%s      <path>               delete file\n", ANSI_ROSE, ANSI_RESET);
    printf("  %scp%s      <src> <dst>           copy file\n", ANSI_ROSE, ANSI_RESET);
    printf("  %sln%s      <target> <link>       create hard link\n", ANSI_ROSE, ANSI_RESET);
    printf("  %sstat%s    <path>               show file info (inode, mode, size, etc.)\n", ANSI_ROSE, ANSI_RESET);
    printf("  %schmod%s   <path> <mode>        change file permissions (octal)\n", ANSI_ROSE, ANSI_RESET);
    printf("\n");
    printf("%s%s  Users%s\n", ANSI_BOLD, ANSI_MAUVE, ANSI_RESET);
    printf("  %suseradd%s <name> <password>    add a new user\n", ANSI_ROSE, ANSI_RESET);
    printf("  %slogin%s  <name> <password>     switch user\n", ANSI_ROSE, ANSI_RESET);
    printf("  %ssu%s     [name]                switch user (default root)\n", ANSI_ROSE, ANSI_RESET);
    printf("  %slogout%s                       log out (switch to root)\n", ANSI_ROSE, ANSI_RESET);
    printf("  %swhoami%s                       show current username\n", ANSI_ROSE, ANSI_RESET);
    printf("  %spasswd%s <name> <newpass>      change user password\n", ANSI_ROSE, ANSI_RESET);
    printf("  %susers%s                        list all users\n", ANSI_ROSE, ANSI_RESET);
    printf("\n");
    printf("%s%s  Processes & Env%s\n", ANSI_BOLD, ANSI_MAUVE, ANSI_RESET);
    printf("  %sasm%s    <source.s> [out.upx]  assemble .upx binary\n", ANSI_ROSE, ANSI_RESET);
    printf("  %scc%s     <source.c> [out.upx]  compile C to .upx binary\n", ANSI_ROSE, ANSI_RESET);
    printf("  %svim%s    <file>                edit a file (host filesystem)\n", ANSI_ROSE, ANSI_RESET);
    printf("  %srun%s    <binary>              run a program\n", ANSI_ROSE, ANSI_RESET);
    printf("  %scmd1%s | %scmd2%s [| ...]       pipeline (2-8 stages)\n", ANSI_ROSE, ANSI_RESET, ANSI_ROSE, ANSI_RESET);
    printf("  %skill%s   <pid> [sig]            send signal (9/15/10)\n", ANSI_ROSE, ANSI_RESET);
    printf("  %smkfifo%s </path>                create named FIFO\n", ANSI_ROSE, ANSI_RESET);
    printf("  %sps%s                          list processes\n", ANSI_ROSE, ANSI_RESET);
    printf("  %sdesign_debug%s <topic>          show OS internals (super/inodes/blocks/sof/memory/process/all)\n", ANSI_ROSE, ANSI_RESET);
    printf("  %senv%s                          show environment variables\n", ANSI_ROSE, ANSI_RESET);
    printf("  %sexport%s <KEY=VALUE>           set environment variable\n", ANSI_ROSE, ANSI_RESET);
    printf("  %sunset%s  <KEY>                 remove environment variable\n", ANSI_ROSE, ANSI_RESET);
    printf("\n");
    printf("%s%s  Other%s\n", ANSI_BOLD, ANSI_MAUVE, ANSI_RESET);
    printf("  %shelp%s                         show this help\n", ANSI_ROSE, ANSI_RESET);
    printf("  %sclear%s                        clear screen\n", ANSI_ROSE, ANSI_RESET);
    printf("  %sexit%s                         exit shell\n", ANSI_ROSE, ANSI_RESET);
    printf("\n");
}

static int require_mounted(void)
{
    if (!g_mounted) { ui_err("Filesystem not mounted; use mount or format first"); return -1; }
    return 0;
}

static int file_is_binary(const char *path)
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

static int cmd_cat(const char *path)
{
    if (require_mounted()) return -1;
    if (path == NULL) { ui_err("Usage: cat <path>"); return -1; }

    if (file_is_binary(path)) {
        ui_err("Binary file (not displayed). Use 'stat' to inspect.");
        return -1;
    }

    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) { ui_err("Cannot open file"); return -1; }

    char buf[512]; int n;
    printf("%s", ANSI_MAUVE);
    while ((n = vfs_read(fd, buf, (int)sizeof(buf) - 1)) > 0) { buf[n] = '\0'; fputs(buf, stdout); }
    printf("%s", ANSI_RESET);
    if (n < 0) { vfs_close(fd); ui_err("Read failed"); return -1; }
    putchar('\n');
    vfs_close(fd);
    return 0;
}

static void normalize_vfs_path(const char *in, char *out, size_t out_sz)
{
    char base[1024];
    char tmp[1024];
    char *tokens[256];
    int top = 0;
    char *save = NULL;
    char *tok;

    if (out == NULL || out_sz == 0)
        return;
    out[0] = '\0';
    if (in == NULL || in[0] == '\0')
        return;

    if (in[0] == '/') {
        snprintf(base, sizeof(base), "%s", in);
    } else if (strcmp(g_cwd, "/") == 0) {
        snprintf(base, sizeof(base), "/%s", in);
    } else {
        snprintf(base, sizeof(base), "%s/%s", g_cwd, in);
    }

    snprintf(tmp, sizeof(tmp), "%s", base);
    tok = strtok_r(tmp, "/", &save);
    while (tok != NULL) {
        if (strcmp(tok, ".") == 0 || tok[0] == '\0') {
            /* skip */
        } else if (strcmp(tok, "..") == 0) {
            if (top > 0) top--;
        } else if (top < (int)(sizeof(tokens) / sizeof(tokens[0]))) {
            tokens[top++] = tok;
        }
        tok = strtok_r(NULL, "/", &save);
    }

    if (top == 0) {
        snprintf(out, out_sz, "/");
        return;
    }

    out[0] = '\0';
    for (int i = 0; i < top; i++) {
        size_t cur = strlen(out);
        size_t left = out_sz > cur ? out_sz - cur : 0;
        if (left <= 1)
            break;
        strncat(out, "/", left - 1);
        cur = strlen(out);
        left = out_sz > cur ? out_sz - cur : 0;
        if (left <= 1)
            break;
        strncat(out, tokens[i], left - 1);
    }
}

static int paths_same(const char *a, const char *b)
{
    char na[1024], nb[1024];
    if (a == NULL || b == NULL)
        return 0;
    normalize_vfs_path(a, na, sizeof(na));
    normalize_vfs_path(b, nb, sizeof(nb));
    return (na[0] != '\0' && nb[0] != '\0' && strcmp(na, nb) == 0);
}

/* 覆盖写入：清空旧内容后写入，避免 delete+create 误伤同名路径或破坏目录项 */
static int vfs_write_bytes(const char *path, const void *data, int len)
{
    MemINode *ip;
    int fd;
    int written;
    uint16_t mode, nlink, uid, gid, ino;
    uint32_t size;

    if (path == NULL || path[0] == '\0')
        return -1;
    if (data == NULL)
        data = "";
    if (len < 0)
        len = 0;

    if (vfs_stat(path, &mode, &size, &nlink, &uid, &gid, &ino) == 0) {
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
    } else {
        if (vfs_create(path, 0644) != 0)
            return -1;
    }

    if (len == 0)
        return vfs_sync_all() == 0 ? 0 : -1;

    fd = vfs_open(path, O_WRONLY);
    if (fd < 0)
        return -1;
    written = vfs_write(fd, data, len);
    vfs_close(fd);
    if (written != len)
        return -1;
    return vfs_sync_all() == 0 ? 0 : -1;
}

static int cmd_write_existing(const char *path, const char *data)
{
    int len = (int)strlen(data);
    if (vfs_write_bytes(path, data, len) != 0) {
        ui_err("Write failed");
        return -1;
    }
    ui_ok("Write successful");
    return 0;
}



static int cmd_useradd(const char *username, const char *password)
{
    if (username == NULL || password == NULL) { ui_err("Usage: useradd <name> <password>"); return -1; }
    if (user_count() >= MAX_USERS) { ui_err("User limit reached"); return -1; }
    if (user_add(username, password) != 0) { ui_err("Failed to add user (already exists?)"); return -1; }
    vfs_sync_all();
    ui_ok("User added");
    return 0;
}

static int cmd_login(const char *username, const char *password)
{
    if (username == NULL || password == NULL) { ui_err("Usage: login <name> <password>"); return -1; }

    int uid = user_verify(username, password);
    if (uid < 0) { ui_err("Login failed: wrong username or password"); return -1; }

    const UserAccount *ua = user_find_by_uid((uint16_t)uid);
    if (ua == NULL) return -1;

    User *u = current_user();
    memset(u, 0, sizeof(User));
    strncpy(u->u_name, ua->ua_name, sizeof(u->u_name) - 1);
    u->u_uid = ua->ua_uid; u->u_gid = ua->ua_gid; u->u_active = 1;

    MemINode *home_ip = namei(ua->ua_home);
    if (home_ip) { u->u_cdir = home_ip->m_inode_no; iput(home_ip); }
    else u->u_cdir = ROOT_INODE_NO;

    shell_bind_user();
    strncpy(g_user_home, ua->ua_home, sizeof(g_user_home) - 1);
    g_user_home[sizeof(g_user_home) - 1] = '\0';
    strncpy(g_cwd, ua->ua_home, CWD_BUF_SIZE - 1);
    g_cwd[CWD_BUF_SIZE - 1] = '\0';

    env_set_current_user(ua->ua_name);
    env_user_load(ua->ua_name);
    ui_ok("Login successful");
    return 0;
}


static int cmd_su(const char *username)
{
    const char *target = (username && username[0]) ? username : "root";
    char pass_buf[128];

    printf("%sPassword [%s]:%s ", ANSI_BOLD ANSI_MAUVE, target, ANSI_RESET);
    fflush(stdout);
    if (read_password(pass_buf, (int)sizeof(pass_buf)) < 0) {
        ui_err("Failed to read password");
        return -1;
    }

    int uid = user_verify(target, pass_buf);
    if (uid < 0) { ui_err("su: authentication failed"); return -1; }

    const UserAccount *ua = user_find_by_uid((uint16_t)uid);
    if (ua == NULL) return -1;

    
    User *old = current_user();
    if (old->u_uid != 0) env_user_save(old->u_name);

    
    memset(old, 0, sizeof(User));
    strncpy(old->u_name, ua->ua_name, sizeof(old->u_name) - 1);
    old->u_uid = ua->ua_uid; old->u_gid = ua->ua_gid; old->u_active = 1;

    MemINode *home_ip = namei(ua->ua_home);
    if (home_ip) { old->u_cdir = home_ip->m_inode_no; iput(home_ip); }
    else old->u_cdir = ROOT_INODE_NO;

    shell_bind_user();
    strncpy(g_user_home, ua->ua_home, sizeof(g_user_home) - 1);
    g_user_home[sizeof(g_user_home) - 1] = '\0';
    strncpy(g_cwd, ua->ua_home, CWD_BUF_SIZE - 1);
    g_cwd[CWD_BUF_SIZE - 1] = '\0';

    env_set_current_user(ua->ua_name);
    env_user_load(ua->ua_name);
    ui_ok("Switched user");
    return 0;
}

static int cmd_logout(void)
{
    User *u = current_user();
    if (u->u_uid == 0) { ui_info("Already root; cannot logout further"); return 0; }

    env_user_save(u->u_name);
    memset(u, 0, sizeof(User));
    strncpy(u->u_name, "root", sizeof(u->u_name) - 1);
    u->u_uid = 0; u->u_gid = 0; u->u_cdir = ROOT_INODE_NO; u->u_active = 1;
    shell_bind_user();
    strncpy(g_cwd, "/", CWD_BUF_SIZE - 1);
    g_user_home[0] = '\0';
    ui_ok("Logged out; now operating as root");
    return 0;
}

static int cmd_whoami(void)
{
    printf("%s%s%s\n", ANSI_MAUVE, current_user()->u_name, ANSI_RESET);
    return 0;
}

static int cmd_passwd(const char *username, const char *new_password)
{
    if (username == NULL || new_password == NULL) { ui_err("Usage: passwd <name> <new_password>"); return -1; }

    User *u = current_user();
    if (u->u_uid != 0 && strcmp(u->u_name, username) != 0) {
        ui_err("Permission denied: only root or the user can change a password");
        return -1;
    }
    if (user_passwd(username, new_password) != 0) { ui_err("Failed to change password"); return -1; }
    vfs_sync_all();
    ui_ok("Password changed");
    return 0;
}

static int cmd_users(void)
{
    int cnt = user_count();
    if (cnt == 0) { ui_info("No users configured"); return 0; }

    printf("\n");
    printf("%s%s%-24s %6s  %s%s\n", ANSI_BOLD, ANSI_MAUVE, "Username", "UID", "Home", ANSI_RESET);
    printf("%s-----------------------------------------------%s\n", ANSI_DIM, ANSI_RESET);
    for (int i = 0; i < cnt; i++) {
        const UserAccount *ua = user_get(i);
        printf("  %-24s %6u  %s\n", ua->ua_name, (unsigned)ua->ua_uid, ua->ua_home);
    }
    printf("\n");
    return 0;
}



static void resolve_program_path(const char *path, char *resolved, size_t sz)
{
    if (path == NULL || resolved == NULL || sz == 0) return;
    if (strchr(path, '/') == NULL)
        snprintf(resolved, sz, "./%s", path);
    else {
        strncpy(resolved, path, sz - 1);
        resolved[sz - 1] = '\0';
    }
}

static void shell_vm_enter_raw(struct termios *saved)
{
    if (!isatty(STDIN_FILENO)) return;
    tcgetattr(STDIN_FILENO, saved);
    struct termios raw = *saved;
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL | BRKINT);
    raw.c_oflag &= (tcflag_t)~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void shell_vm_leave_raw(const struct termios *saved)
{
    if (!isatty(STDIN_FILENO)) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, saved);
}

static int cmd_run(const char *path)
{
    if (path == NULL) { ui_err("Usage: run <binary>"); return -1; }

    char resolved[256];
    resolve_program_path(path, resolved, sizeof(resolved));

    PCB *p = proc_alloc();
    if (p == NULL) { ui_err("Failed to allocate process"); return -1; }
    strncpy(p->p_name, path, PROC_NAME_LEN - 1);
    p->p_cwd_ino = current_user()->u_cdir;

    if (proc_exec(p, resolved) != 0) {
        proc_free(p);
        ui_err("Failed to load program");
        return -1;
    }
    sched_enqueue(p);
    struct termios old_tio;
    shell_vm_enter_raw(&old_tio);
    while (sched_tick() == 0) {}
    shell_vm_leave_raw(&old_tio);

    if (p->p_state == PROC_ZOMBIE) {
        proc_free(p);
    }
    return 0;
}

static const char *pipeline_program(const char *cmd)
{
    while (cmd != NULL && isspace((unsigned char)*cmd)) cmd++;
    if (cmd == NULL || *cmd == '\0') return cmd;
    if (strncmp(cmd, "run ", 4) == 0) return cmd + 4;
    return cmd;
}

#define PIPELINE_MAX_STAGES 8

static void proc_bind_pipe_in(PCB *p, int pipe_id)
{
    p->p_ofile[0].fd_type = PROC_FD_PIPE_RD;
    p->p_ofile[0].fd_pipe_id = pipe_id;
    pipe_add_reader(pipe_id);
}

static void proc_bind_pipe_out(PCB *p, int pipe_id)
{
    p->p_ofile[1].fd_type = PROC_FD_PIPE_WR;
    p->p_ofile[1].fd_pipe_id = pipe_id;
    pipe_add_writer(pipe_id);
}

static int cmd_run_pipeline_chain(int stage_count, char **stages)
{
    if (stage_count < 2 || stage_count > PIPELINE_MAX_STAGES) {
        ui_err("Pipeline supports 2-8 stages");
        return -1;
    }

    PCB  *procs[PIPELINE_MAX_STAGES];
    int   pipes[PIPELINE_MAX_STAGES - 1];
    char  paths[PIPELINE_MAX_STAGES][256];

    memset(procs, 0, sizeof(procs));
    for (int i = 0; i < stage_count - 1; i++) pipes[i] = -1;

    for (int i = 0; i < stage_count - 1; i++) {
        pipes[i] = pipe_alloc();
        if (pipes[i] < 0) {
            ui_err("No free pipes");
            goto fail;
        }
    }

    for (int i = 0; i < stage_count; i++) {
        const char *cmd = pipeline_program(stages[i]);
        if (cmd == NULL || cmd[0] == '\0') { ui_err("Empty pipeline stage"); goto fail; }
        resolve_program_path(cmd, paths[i], sizeof(paths[i]));

        procs[i] = proc_alloc();
        if (procs[i] == NULL) { ui_err("Failed to allocate process"); goto fail; }
        strncpy(procs[i]->p_name, paths[i], PROC_NAME_LEN - 1);
        procs[i]->p_cwd_ino = current_user()->u_cdir;

        if (i > 0) proc_bind_pipe_in(procs[i], pipes[i - 1]);
        if (i < stage_count - 1) proc_bind_pipe_out(procs[i], pipes[i]);

        if (proc_exec(procs[i], paths[i]) != 0) {
            ui_err("Failed to load program");
            goto fail;
        }
        sched_enqueue(procs[i]);
    }

    {
        struct termios old_tio;
        shell_vm_enter_raw(&old_tio);
        while (sched_tick() == 0) {}
        shell_vm_leave_raw(&old_tio);
    }

    for (int i = 0; i < stage_count; i++) {
        if (procs[i] && procs[i]->p_state == PROC_ZOMBIE) proc_free(procs[i]);
    }
    for (int i = 0; i < stage_count - 1; i++) {
        if (pipes[i] >= 0) pipe_free(pipes[i]);
    }
    return 0;

fail:
    for (int i = 0; i < stage_count; i++) {
        if (procs[i]) proc_free(procs[i]);
    }
    for (int i = 0; i < stage_count - 1; i++) {
        if (pipes[i] >= 0) pipe_free(pipes[i]);
    }
    return -1;
}

static int split_pipeline_segments(char *line, char *segments[], int max_seg)
{
    int count = 0;
    char *part = line;
    while (count < max_seg) {
        char *bar = strchr(part, '|');
        if (bar) *bar = '\0';
        char *seg = trim(part);
        if (seg[0] != '\0') segments[count++] = seg;
        if (bar == NULL) break;
        part = bar + 1;
    }
    return count;
}

static int try_run_pipeline(char *line)
{
    if (strchr(line, '|') == NULL) return 0;

    char *segments[PIPELINE_MAX_STAGES];
    int stage_count = split_pipeline_segments(line, segments, PIPELINE_MAX_STAGES);
    if (stage_count < 2) {
        ui_err("Usage: cmd1 | cmd2 [| cmd3 ...]");
        return -1;
    }
    if (require_mounted() != 0) return -1;
    return cmd_run_pipeline_chain(stage_count, segments) == 0 ? 1 : -1;
}

static int cmd_kill_cmd(int argc, char **argv)
{
    if (argc < 2) { ui_err("Usage: kill <pid> [sig]"); return -1; }
    uint32_t pid = (uint32_t)strtoul(argv[1], NULL, 10);
    int sig = (argc >= 3) ? (int)strtol(argv[2], NULL, 10) : SIG_TERM;
    if (ipc_kill(pid, sig) != 0) { ui_err("kill failed"); return -1; }
    ui_ok("Signal sent");
    return 0;
}

static int cmd_mkfifo(const char *path)
{
    if (path == NULL || path[0] != '/') { ui_err("Usage: mkfifo </path>"); return -1; }
    if (ipc_mkfifo(path) != 0) { ui_err("mkfifo failed"); return -1; }
    ui_ok("FIFO created");
    return 0;
}

static int cmd_design_debug(int argc, char **argv)
{
    if (argc < 2) {
        ui_err("Usage: design_debug <super|inodes|blocks|bg|sof|memory|process|all>");
        return -1;
    }
    const char *sub = argv[1];

    if (strcmp(sub, "super") == 0 || strcmp(sub, "sb") == 0 || strcmp(sub, "all") == 0) {
        if (require_mounted()) return -1;
        fs_debug_print_super();
    }
    if (strcmp(sub, "inodes") == 0 || strcmp(sub, "ino") == 0 || strcmp(sub, "all") == 0) {
        if (require_mounted()) return -1;
        fs_debug_print_inodes();
    }
    if (strcmp(sub, "blocks") == 0 || strcmp(sub, "blk") == 0) {
        if (require_mounted()) return -1;
        fs_debug_print_super();
    }
    if (strcmp(sub, "bg") == 0 || strcmp(sub, "blockgroup") == 0) {
        if (require_mounted()) return -1;
        bg_debug_print();
    }
    if (strcmp(sub, "sof") == 0 || strcmp(sub, "all") == 0) {
        if (require_mounted()) return -1;
        const SysOpenFile *sof = sys_open_file_table();
        int sof_cnt = sys_open_file_count();
        printf("\n");
        printf("  ── System Open File Table (%d/%d) ─────────────\n",
               sof_cnt, SYS_OPEN_FILE_MAX);
        printf("  %-4s %-6s %-6s %-8s %-4s %s\n", "Idx", "Inode", "Mode", "Offset", "Ref", "Flags");
        for (int i = 0; i < SYS_OPEN_FILE_MAX; i++) {
            if (sof[i].f_inode == NULL) continue;
            printf("  %-4d %-6u 0x%04X %-8u %-4u 0x%04X\n",
                   i, (unsigned)sof[i].f_inode->m_inode_no,
                   (unsigned)sof[i].f_mode, (unsigned)sof[i].f_offset,
                   (unsigned)sof[i].f_count, (unsigned)sof[i].f_flags);
        }
        printf("\n");
    }
    if (strcmp(sub, "memory") == 0 || strcmp(sub, "mem") == 0 || strcmp(sub, "all") == 0) {
        mem_debug_print();
    }
    if (strcmp(sub, "process") == 0 || strcmp(sub, "proc") == 0 || strcmp(sub, "all") == 0) {
        int count = 0;
        PCB *table = proc_get_table(&count);
        if (table == NULL) { printf("  No process table.\n"); return 0; }

        printf("\n");
        printf("  ── Process Table (%d/%d slots) ─────────────────\n",
               proc_count(), PROC_MAX_COUNT);
        const char *states[] = { "FREE", "READY", "RUN", "BLOCK", "ZOMBIE" };
        for (int i = 0; i < count; i++) {
            if (table[i].p_state == PROC_FREE) continue;
            printf("\n");
            printf("  PID %-5u  %-16s  state=%-6s  ppid=%u\n",
                   (unsigned)table[i].p_pid, table[i].p_name,
                   states[table[i].p_state < 5 ? table[i].p_state : 0],
                   (unsigned)table[i].p_ppid);
            printf("    mem: text=%u pages (%u KB)  data=%u pages  bss_end=0x%X  stack_top=0x%X  heap=0x%X\n",
                   table[i].p_text_pages,
                   table[i].p_text_pages * MEM_PAGE_SIZE / 1024,
                   table[i].p_data_pages,
                   (unsigned)table[i].p_bss_end,
                   (unsigned)table[i].p_stack_top,
                   (unsigned)table[i].p_heap_brk);
            CPUContext *cpu = &table[i].p_cpu;
            printf("    cpu: PC=0x%X  SP=0x%X  FP=0x%X  FLAGS=0x%X  ticks_left=%u\n",
                   (unsigned)cpu->pc, (unsigned)cpu->regs[CPU_REG_SP],
                   (unsigned)cpu->regs[14],  /* R14 = FP */
                   (unsigned)cpu->flags, (unsigned)cpu->ticks_left);
            printf("    fd: ");
            int fd_any = 0;
            for (int j = 0; j < PROC_MAX_FD; j++) {
                if (table[i].p_ofile[j].fd_type != 0) {
                    const char *ft = "?";
                    switch (table[i].p_ofile[j].fd_type) {
                        case PROC_FD_TERM: ft = "term"; break;
                        case PROC_FD_FILE: ft = "file"; break;
                        case PROC_FD_PIPE_RD: ft = "pipe-rd"; break;
                        case PROC_FD_PIPE_WR: ft = "pipe-wr"; break;
                        case PROC_FD_FIFO_RD: ft = "fifo-rd"; break;
                        case PROC_FD_FIFO_WR: ft = "fifo-wr"; break;
                    }
                    printf("%d:%s(fs=%d) ", j, ft, table[i].p_ofile[j].fd_fs_fd);
                    fd_any++;
                }
            }
            if (!fd_any) printf("(none)");
            printf("\n");
            if (table[i].p_pending_sig)
                printf("    pending_sig=%u  sigusr1_count=%d\n",
                       (unsigned)table[i].p_pending_sig,
                       table[i].p_sigusr1_count);
            if (table[i].p_child_count > 0) {
                printf("    children: ");
                for (int j = 0; j < table[i].p_child_count; j++)
                    printf("%u ", (unsigned)table[i].p_children[j]);
                printf("\n");
            }
        }
        printf("\n");
    }

    if (strcmp(sub, "all") != 0 &&
        strcmp(sub, "super") != 0 && strcmp(sub, "sb") != 0 &&
        strcmp(sub, "inodes") != 0 && strcmp(sub, "ino") != 0 &&
        strcmp(sub, "blocks") != 0 && strcmp(sub, "blk") != 0 &&
        strcmp(sub, "sof") != 0 &&
        strcmp(sub, "memory") != 0 && strcmp(sub, "mem") != 0 &&
        strcmp(sub, "process") != 0 && strcmp(sub, "proc") != 0) {
        ui_err("Unknown subcommand. Try: super, inodes, blocks, sof, memory, process, all");
        return -1;
    }
    return 0;
}

static int cmd_ps(void)
{
    int count = 0;
    PCB *table = proc_get_table(&count);
    if (table == NULL) { ui_info("No process table"); return 0; }

    printf("\n");
    printf("%s%s%-6s %-16s %-10s %s%s\n",
           ANSI_BOLD, ANSI_MAUVE, "PID", "Name", "State", "", ANSI_RESET);
    printf("%s----------------------------------------%s\n", ANSI_DIM, ANSI_RESET);

    const char *states[] = { "FREE", "READY", "RUN", "BLOCK", "ZOMBIE" };
    for (int i = 0; i < count; i++) {
        if (table[i].p_state == PROC_FREE) continue;
        printf("  %-6u %-16s %-10s\n",
               (unsigned)table[i].p_pid, table[i].p_name,
               states[table[i].p_state < 5 ? table[i].p_state : 0]);
    }
    printf("\n");
    return 0;
}

static void env_print_cb(const char *name, const char *value, int is_system)
{
    printf("  %s%-24s = %s%s%s\n",
           is_system ? ANSI_DIM : ANSI_RESET,
           name, value, is_system ? " [sys]" : "", ANSI_RESET);
}

static int cmd_env(void)
{
    printf("\n");
    env_foreach(env_print_cb);
    return 0;
}

static int cmd_export(const char *arg)
{
    if (arg == NULL) { ui_err("Usage: export KEY=VALUE"); return -1; }
    char buf[512];
    strncpy(buf, arg, sizeof(buf) - 1);
    char *eq = strchr(buf, '=');
    if (eq == NULL) { ui_err("Format: export KEY=VALUE"); return -1; }
    *eq = '\0';
    if (env_set(buf, eq + 1) != 0) { ui_err("Failed to set variable"); return -1; }
    ui_ok("Variable set");
    return 0;
}

static int cmd_unset(const char *key)
{
    if (key == NULL) { ui_err("Usage: unset KEY"); return -1; }
    if (env_unset(key) != 0) { ui_err("Variable not found"); return -1; }
    ui_ok("Variable removed");
    return 0;
}



static int dispatch_command(int argc, char **argv)
{
    if (argc <= 0 || argv[0] == NULL) return 0;
    const char *cmd = argv[0];

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) { cmd_help(); return 0; }
    if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) return 1;
    if (strcmp(cmd, "clear") == 0) { printf("\033[2J\033[H"); ui_banner(); return 0; }
    if (strcmp(cmd, "format") == 0) return shell_format(argc > 1 ? argv[1] : NULL) == 0 ? 0 : -1;
    if (strcmp(cmd, "mount") == 0 || strcmp(cmd, "restore") == 0) return shell_mount(argc > 1 ? argv[1] : NULL) == 0 ? 0 : -1;
    if (strcmp(cmd, "umount") == 0) return shell_umount() == 0 ? 0 : -1;
    if (strcmp(cmd, "asm") == 0) {
        if (argc >= 2 && strcmp(argv[1], "--help") == 0) {
            printf("UPFS Assembler v0.1.0\n\n");
            printf("Usage: asm <source.s> [output.upx]\n\n");
            printf("  source.s     Assembly source file (.s / .asm)\n");
            printf("  output.upx   Output binary (default: a.upx)\n");
            return 0;
        }
        if (argc < 2) { ui_err("Usage: asm <source.s> [output.upx]"); return -1; }
        const char *out_name = argc > 2 ? argv[2] : "a.upx";
        if (paths_same(argv[1], out_name)) {
            ui_err("Output path must differ from source path");
            return -1;
        }
        
        char tmp_src[256], tmp_out[256];
        snprintf(tmp_src, sizeof(tmp_src), "/tmp/upfs_asm_src_%d", getpid());
        snprintf(tmp_out, sizeof(tmp_out), "/tmp/upfs_asm_out_%d", getpid());
        
        {
            MemINode *ip = namei(argv[1]);
            if (!ip) { ui_err("Source file not found"); return -1; }
            iput(ip);
            int fd = vfs_open(argv[1], O_RDONLY);
            if (fd < 0) { ui_err("Cannot open source"); return -1; }
            char *buf = malloc(65536); int total = 0, n;
            if (!buf) { vfs_close(fd); ui_err("Out of memory"); return -1; }
            while ((n = vfs_read(fd, buf + total, 65536 - total - 1)) > 0) total += n;
            vfs_close(fd);
            FILE *f = fopen(tmp_src, "w");
            if (f) { fwrite(buf, 1, (size_t)total, f); fclose(f); }
            free(buf);
        }

        if (assemble_file(tmp_src, tmp_out) != 0) { unlink(tmp_src); unlink(tmp_out); ui_err("Assembly failed"); return -1; }
        
        {
            FILE *f = fopen(tmp_out, "rb");
            if (f) {
                fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
                if (sz > 0) {
                    char *buf = malloc((size_t)sz);
                    if (buf) {
                        fread(buf, 1, (size_t)sz, f);
                        if (vfs_write_bytes(out_name, buf, (int)sz) != 0) {
                            free(buf);
                            unlink(tmp_src); unlink(tmp_out);
                            ui_err("Failed to write output to VFS");
                            return -1;
                        }
                        free(buf);
                    }
                }
                fclose(f);
            }
        }
        unlink(tmp_src); unlink(tmp_out);
        ui_ok("Assembly complete"); return 0;
    }
    if (strcmp(cmd, "vim") == 0) {
        if (argc < 2) { ui_err("Usage: vim <file>"); return -1; }
        
        char tmp_path[256];
        snprintf(tmp_path, sizeof(tmp_path), "/tmp/upfs_vim_%d", getpid());
        
        MemINode *ip = namei(argv[1]);
        if (ip != NULL) {
            iput(ip);
            int fd = vfs_open(argv[1], O_RDONLY);
            if (fd >= 0) {
                char buf[4096]; int total = 0, n;
                while ((n = vfs_read(fd, buf + total, (int)sizeof(buf) - total - 1)) > 0)
                    total += n;
                vfs_close(fd);
                FILE *tf = fopen(tmp_path, "w");
                if (tf) { fwrite(buf, 1, (size_t)total, tf); fclose(tf); }
            }
        }
        
        printf("\033[2J\033[H"); fflush(stdout);
        editor_open(tmp_path);
        printf("\033[2J\033[H"); fflush(stdout);
        
        FILE *tf = fopen(tmp_path, "r");
        if (tf) {
            fseek(tf, 0, SEEK_END); long sz = ftell(tf); rewind(tf);
            if (sz > 0) {
                char *buf = malloc((size_t)(sz + 1));
                if (buf) {
                    fread(buf, 1, (size_t)sz, tf);
                    
                    vfs_delete(argv[1]);
                    if (vfs_create(argv[1], 0644) != 0) {
                        vfs_delete(argv[1]);
                        vfs_create(argv[1], 0644);
                    }
                    int fd = vfs_open(argv[1], O_WRONLY);
                    if (fd >= 0) {
                        vfs_write(fd, buf, (int)sz);
                        vfs_close(fd);
                    }
                    free(buf);
                }
            } else {
                
                vfs_delete(argv[1]);
                vfs_create(argv[1], 0644);
            }
            fclose(tf);
        }
        unlink(tmp_path);
        ui_banner();
        return 0;
    }
    if (strcmp(cmd, "cc") == 0) {
        if (argc < 2) { ui_err("Usage: cc <source.c> [output.upx] [--asm]"); return -1; }
        // 扫描 --asm 标志，有则移除
        int save_asm = 0;
        for (int ai = 1; ai < argc; ai++) {
            if (strcmp(argv[ai], "--asm") == 0) {
                save_asm = 1;
                for (int aj = ai; aj < argc; aj++) argv[aj] = argv[aj + 1];
                argc--;
                break;
            }
        }
        const char *out_name = argc > 2 ? argv[2] : "a.upx";
        if (paths_same(argv[1], out_name)) {
            ui_err("Output path must differ from source path");
            return -1;
        }
        char tmp_src[256], tmp_asm[256], tmp_out[256];
        snprintf(tmp_src, sizeof(tmp_src), "/tmp/upfs_cc_src_%d.c", getpid());
        snprintf(tmp_asm, sizeof(tmp_asm), "/tmp/upfs_cc_asm_%d.s", getpid());
        snprintf(tmp_out, sizeof(tmp_out), "/tmp/upfs_cc_out_%d.upx", getpid());
        // 1. 导出 VFS 源文件
        {
            MemINode *ip = namei(argv[1]);
            if (!ip) { ui_err("Source file not found"); return -1; }
            iput(ip);
            int fd = vfs_open(argv[1], O_RDONLY);
            if (fd < 0) { ui_err("Cannot open source"); return -1; }
            char *buf = malloc(65536); int total = 0, n;
            if (!buf) { vfs_close(fd); ui_err("Out of memory"); return -1; }
            while ((n = vfs_read(fd, buf + total, 65536 - total - 1)) > 0) total += n;
            vfs_close(fd);
            FILE *f = fopen(tmp_src, "w");
            if (f) { fwrite(buf, 1, (size_t)total, f); fclose(f); }
            free(buf);
        }
        // 2. 编译 C → 汇编
        if (compile_c_to_asm(tmp_src, tmp_asm) != 0) {
            unlink(tmp_src); unlink(tmp_asm); unlink(tmp_out);
            ui_err("Compilation failed"); return -1;
        }
        // 3. 附加运行时库
        {
            FILE *asm_f = fopen(tmp_asm, "a");
            if (asm_f) {
                // 查找运行时库路径
                const char *rt_paths[] = {
                    "src/compiler/runtime.s",
                    "../src/compiler/runtime.s",
                    "../../src/compiler/runtime.s",
                    NULL
                };
                for (int ri = 0; rt_paths[ri]; ri++) {
                    FILE *rt_f = fopen(rt_paths[ri], "r");
                    if (rt_f) {
                        char buf[4096]; size_t n;
                        while ((n = fread(buf, 1, sizeof(buf), rt_f)) > 0)
                            fwrite(buf, 1, n, asm_f);
                        fclose(rt_f);
                        break;
                    }
                }
                fclose(asm_f);
            }
        }
        // 4. 汇编 → .upx
        if (assemble_file(tmp_asm, tmp_out) != 0) {
            unlink(tmp_src); unlink(tmp_asm); unlink(tmp_out);
            ui_err("Assembly after compilation failed"); return -1;
        }
        // 5. 导入输出到 VFS
        {
            FILE *f = fopen(tmp_out, "rb");
            if (f) {
                fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
                if (sz > 0) {
                    char *buf = malloc((size_t)sz);
                    if (buf) {
                        fread(buf, 1, (size_t)sz, f);
                        if (vfs_write_bytes(out_name, buf, (int)sz) != 0) {
                            free(buf);
                            unlink(tmp_src); unlink(tmp_asm); unlink(tmp_out);
                            ui_err("Failed to write output to VFS");
                            return -1;
                        }
                        free(buf);
                    }
                }
                fclose(f);
            }
        }
        // --asm: 保存汇编到 VFS，与源文件同目录
        if (save_asm) {
            char asm_vfs[512];
            const char *s = argv[1];
            const char *dot = strrchr(s, '.');
            size_t base_len = dot ? (size_t)(dot - s) : strlen(s);
            memcpy(asm_vfs, s, base_len);
            strcpy(asm_vfs + base_len, ".s");
            FILE *af = fopen(tmp_asm, "r");
            if (af) {
                fseek(af, 0, SEEK_END); long sz = ftell(af); rewind(af);
                if (sz > 0) {
                    char *abuf = malloc((size_t)(sz + 1));
                    if (abuf) {
                        size_t nread = fread(abuf, 1, (size_t)sz, af);
                        if (nread > 0) {
                            if (vfs_write_bytes(asm_vfs, abuf, (int)nread) == 0)
                                printf("  asm saved: %s (%zu bytes)\n", asm_vfs, nread);
                        }
                        free(abuf);
                    }
                }
                fclose(af);
            }
        }
        unlink(tmp_src); unlink(tmp_asm); unlink(tmp_out);
        ui_ok("Compilation complete"); return 0;
    }

    if (require_mounted()) return -1;

    
    if (cmd[0] == '.' && cmd[1] == '/') { return cmd_run(cmd); }

    if (strcmp(cmd, "mkdir") == 0) {
        uint16_t mode = 0755;
        if (argc < 2) { ui_err("Usage: mkdir <path> [mode]"); return -1; }
        if (argc >= 3 && parse_octal_mode(argv[2], &mode) != 0) { ui_err("Mode must be octal, e.g. 0755"); return -1; }
        if (vfs_mkdir(argv[1], mode) != 0) { ui_err("mkdir failed"); return -1; }
        vfs_sync_all(); ui_ok("Directory created"); return 0;
    }
    if (strcmp(cmd, "cd") == 0 || strcmp(cmd, "chdir") == 0) {
        const char *target = argv[1];
        if (argc < 2 || strcmp(target, "~") == 0) {
            
            if (g_user_home[0] == '\0') { ui_err("No home directory configured"); return -1; }
            target = g_user_home;
        }
        if (chdir(target) != 0) { ui_err("cd failed"); return -1; }
        cwd_update_after_cd(target); ui_ok("Directory changed"); return 0;
    }
    if (strcmp(cmd, "pwd") == 0) {
        char display[CWD_BUF_SIZE];
        cwd_display(display, sizeof(display));
        printf("%s%s%s\n", ANSI_MAUVE, display, ANSI_RESET);
        return 0;
    }
    if (strcmp(cmd, "ls") == 0 || strcmp(cmd, "dir") == 0) {
        if (dir_list(argc > 1 ? argv[1] : NULL) != 0) { ui_err("ls failed"); return -1; }
        return 0;
    }
    if (strcmp(cmd, "create") == 0) {
        uint16_t mode = 0644;
        if (argc < 2) { ui_err("Usage: create <path> [mode]"); return -1; }
        if (argc >= 3 && parse_octal_mode(argv[2], &mode) != 0) { ui_err("Mode must be octal, e.g. 0644"); return -1; }
        if (vfs_create(argv[1], mode) != 0) { ui_err("create failed"); return -1; }
        vfs_sync_all(); ui_ok("File created"); return 0;
    }
    if (strcmp(cmd, "write") == 0) {
        if (argc < 3) { ui_err("Usage: write <path> <data>"); return -1; }
        int rc = cmd_write_existing(argv[1], argv[2]);
        if (rc == 0) fs_sync_disk();
        return rc;
    }
    if (strcmp(cmd, "cat") == 0) return cmd_cat(argv[1]);
    if (strcmp(cmd, "rm") == 0 || strcmp(cmd, "delete") == 0) {
        if (argc < 2) { ui_err("Usage: rm <path>"); return -1; }
        if (vfs_delete(argv[1]) != 0) { ui_err("Delete failed"); return -1; }
        vfs_sync_all(); ui_ok("File deleted"); return 0;
    }
    if (strcmp(cmd, "cp") == 0 || strcmp(cmd, "copy") == 0) {
        if (argc < 3) { ui_err("Usage: cp <src> <dst>"); return -1; }
        if (vfs_copy(argv[1], argv[2]) != 0) { ui_err("Copy failed"); return -1; }
        vfs_sync_all(); ui_ok("File copied"); return 0;
    }
    if (strcmp(cmd, "ln") == 0 || strcmp(cmd, "link") == 0) {
        if (argc < 3) { ui_err("Usage: ln <target> <link_name>"); return -1; }
        if (vfs_link(argv[1], argv[2]) != 0) { ui_err("Link failed"); return -1; }
        vfs_sync_all(); ui_ok("Hard link created"); return 0;
    }
    if (strcmp(cmd, "stat") == 0) {
        if (argc < 2) { ui_err("Usage: stat <path>"); return -1; }
        uint16_t st_mode, st_nlink, st_uid, st_gid, st_ino;
        uint32_t st_size;
        if (vfs_stat(argv[1], &st_mode, &st_size, &st_nlink, &st_uid, &st_gid, &st_ino) != 0) {
            ui_err("stat failed"); return -1;
        }
        printf("\n");
        printf("  File:     %s\n", argv[1]);
        printf("  Inode:    %u\n", (unsigned)st_ino);
        printf("  Type:     %s\n", (st_mode & IFDIR) ? "directory" : (st_mode & IFREG) ? "regular file" : "unknown");
        printf("  Mode:     %06o\n", (unsigned)(st_mode & 0777));
        printf("  Links:    %u\n", (unsigned)st_nlink);
        printf("  Size:     %u bytes\n", (unsigned)st_size);
        printf("  Owner:    uid=%u gid=%u\n", (unsigned)st_uid, (unsigned)st_gid);
        printf("\n");
        return 0;
    }
    if (strcmp(cmd, "chmod") == 0) {
        if (argc < 3) { ui_err("Usage: chmod <path> <mode>"); return -1; }
        uint16_t new_mode;
        if (parse_octal_mode(argv[2], &new_mode) != 0) { ui_err("Mode must be octal, e.g. 0755"); return -1; }
        if (vfs_chmod(argv[1], new_mode) != 0) { ui_err("chmod failed (permission denied?)"); return -1; }
        vfs_sync_all(); ui_ok("Mode changed"); return 0;
    }
    if (strcmp(cmd, "useradd") == 0) {
        if (argc < 3) { ui_err("Usage: useradd <name> <password>"); return -1; }
        return cmd_useradd(argv[1], argv[2]);
    }
    if (strcmp(cmd, "login") == 0) {
        if (argc < 3) { ui_err("Usage: login <name> <password>"); return -1; }
        return cmd_login(argv[1], argv[2]);
    }
    if (strcmp(cmd, "su") == 0) return cmd_su(argc > 1 ? argv[1] : NULL);
    if (strcmp(cmd, "logout") == 0) return cmd_logout();
    if (strcmp(cmd, "whoami") == 0) return cmd_whoami();
    if (strcmp(cmd, "passwd") == 0) {
        if (argc < 3) { ui_err("Usage: passwd <name> <new_password>"); return -1; }
        return cmd_passwd(argv[1], argv[2]);
    }
    if (strcmp(cmd, "users") == 0) return cmd_users();
    if (strcmp(cmd, "run") == 0) { if (argc < 2) { ui_err("Usage: run <binary>"); return -1; } return cmd_run(argv[1]); }
    if (strcmp(cmd, "kill") == 0) return cmd_kill_cmd(argc, argv);
    if (strcmp(cmd, "mkfifo") == 0) {
        if (argc < 2) { ui_err("Usage: mkfifo </path>"); return -1; }
        return cmd_mkfifo(argv[1]);
    }
    if (strcmp(cmd, "design_debug") == 0 || strcmp(cmd, "dd") == 0)
        return cmd_design_debug(argc, argv);
    if (strcmp(cmd, "ps") == 0) return cmd_ps();
    if (strcmp(cmd, "env") == 0) return cmd_env();
    if (strcmp(cmd, "export") == 0) { if (argc < 2) { ui_err("Usage: export KEY=VALUE"); return -1; } return cmd_export(argv[1]); }
    if (strcmp(cmd, "unset") == 0) { if (argc < 2) { ui_err("Usage: unset KEY"); return -1; } return cmd_unset(argv[1]); }

    
    {
        const char *path_env = env_get_path();
        if (path_env != NULL) {
            char path_buf[512];
            strncpy(path_buf, path_env, sizeof(path_buf) - 1);
            path_buf[sizeof(path_buf) - 1] = '\0';

            char *save = NULL;
            char *dir = strtok_r(path_buf, ":", &save);
            while (dir != NULL) {
                char full_path[CWD_BUF_SIZE];
                snprintf(full_path, sizeof(full_path), "%s/%s", dir, cmd);
                MemINode *ip = namei(full_path);
                if (ip != NULL) {
                    int is_reg = (ip->m_dinode.d_mode & IFREG) != 0;
                    iput(ip);
                    if (is_reg) return cmd_run(full_path);
                }
                dir = strtok_r(NULL, ":", &save);
            }
        }
    }

    ui_err("Unknown command. Type  help  for a command list.");
    return -1;
}



extern int dup2(int oldfd, int newfd);

/*
@brief 主会话函数，处理输入输出重定向和命令循环
@param in_fd 输入文件描述符，通常为0（标准输入）
@param out_fd 输出文件描述符，通常为1（标准输出）
@return 0表示正常退出，非0表示发生错误
*/
int upfs_session(int in_fd, int out_fd)
{
    vfs_upfs_register(); //注册UPFS文件系统各个函数

    if (g_kernel == NULL) kernel_local_init(); //内核初始化，设置全局内核对象指针

    if (dup2(out_fd, 1) < 0) return 1;   
    if (dup2(out_fd, 2) < 0) return 1;   
    if (dup2(in_fd,  0) < 0) return 1;   

    char line[LINE_BUF_SIZE];
    char *argv[MAX_ARGS];
    int exit_flag = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin,  NULL, _IONBF, 0);
    ui_banner();
    const char *shared = shared_disk_path();
    if (shared && shared[0]) {
        strncpy(g_disk_path, shared, sizeof(g_disk_path) - 1);
        g_disk_path[sizeof(g_disk_path) - 1] = '\0';
        if (disk_file_exists(g_disk_path)) {
            printf("\n");
            ui_ok("Shared disk detected");
            printf("    %sLocation:%s %s\n", ANSI_DIM, ANSI_RESET, g_disk_path);
            shell_mount(g_disk_path);
        } else {
            printf("\n");
            ui_info("Shared disk not accessible, scanning locally...");
            startup_disk_probe();
        }
    } else {
        startup_disk_probe();
    }
    if (!g_mounted) ui_info("Type  help  for command list");
    fflush(stdout);

    while (!exit_flag) {
        ui_prompt();
        if (fgets(line, sizeof(line), stdin) == NULL) { printf("\n"); break; }
        if (trim(line)[0] == '\0') continue;

        {
            char pipe_buf[LINE_BUF_SIZE];
            strncpy(pipe_buf, line, sizeof(pipe_buf) - 1);
            pipe_buf[sizeof(pipe_buf) - 1] = '\0';
            int prc = try_run_pipeline(pipe_buf);
            if (prc == 1) continue;
            if (prc < 0) continue;
        }

        int argc = parse_command_line(line, argv, MAX_ARGS);
        if (g_mounted) (void)fs_reload_super();
        if (dispatch_command(argc, argv) == 1) exit_flag = 1;
    }

    
    int is_shared_session = (shared_disk_path() != NULL);
    if (g_mounted) {
        user_db_save();
        env_system_save();
        if (!is_shared_session) {
            proc_shutdown();
            mem_shutdown();
        }
        vfs_umount();
    }
    ui_ok("Goodbye");
    fflush(stdout);
    return 0;
}



int main(int argc, char *argv[])
{
    if (argc >= 2 && strcmp(argv[1], "--serve") == 0) {
        int port = 4096;
        if (argc >= 3) port = atoi(argv[2]);  //检测第二参数作为转发端口，否则默认
        return serve_main(port);
    }
    return upfs_session(0, 1);  
}
