// main.c —— UPFS 模拟 UNIX Shell 交互主控

#include "vfs_core.h"
#include "disk_io.h"
#include "format.h"
#include "allocator.h"
#include "dir_sys.h"
#include "file_sys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ANSI 真彩色：#CD8987 暗玫瑰粉（提示符 / 成功主色）
#define ANSI_ROSE           "\033[38;2;205;137;135m"
// ANSI 真彩色：#CDACA1 灰粉豆沙（副标题 / 次要信息）
#define ANSI_MAUVE          "\033[38;2;205;172;161m"
#define ANSI_RESET          "\033[0m"
#define ANSI_BOLD           "\033[1m"
#define ANSI_DIM            "\033[2m"
// 玻璃态隐喻：半透明暗色底 + 细边框色
#define ANSI_GLASS_BG       "\033[48;2;28;26;30m"
#define ANSI_GLASS_EDGE     "\033[38;2;120;108;112m"
#define ANSI_ERR            "\033[38;2;180;90;90m"
#define ANSI_OK             ANSI_ROSE

#define LINE_BUF_SIZE       1024
#define MAX_ARGS            32
#define CWD_BUF_SIZE        512

// 全局 Shell 状态
static User   g_user;
static char   g_disk_path[512] = DEFAULT_DISK_PATH;
static char   g_cwd[CWD_BUF_SIZE] = "/";
static int    g_mounted = 0;

// ---------- 终端 UI ----------

static void ui_enable_ansi(void)
{
#if !defined(_WIN32)
    return;
#else
    // Windows 10+ 终端默认支持 ANSI；旧版需自行开启，此处假定现代终端
#endif
}

static void ui_banner(void)
{
    printf("\n");
    fputs(ANSI_GLASS_BG ANSI_GLASS_EDGE, stdout);
    fputs("╔══════════════════════════════════════════════════════════╗\n", stdout);
    fputs(ANSI_RESET, stdout);

    fputs(ANSI_GLASS_BG ANSI_GLASS_EDGE "║" ANSI_RESET "  ", stdout);
    fputs(ANSI_BOLD ANSI_ROSE "UPFS" ANSI_RESET, stdout);
    fputs(ANSI_MAUVE " · Unix File System Simulator", stdout);
    fputs(ANSI_GLASS_BG ANSI_GLASS_EDGE "                          ║\n", stdout);
    fputs(ANSI_RESET, stdout);

    fputs(ANSI_GLASS_BG ANSI_GLASS_EDGE "║" ANSI_RESET "  ", stdout);
    fputs(ANSI_DIM ANSI_MAUVE "Glass Terminal — cold-elegant cyber shell", stdout);
    fputs(ANSI_GLASS_BG ANSI_GLASS_EDGE "                    ║\n", stdout);
    fputs(ANSI_RESET, stdout);

    fputs(ANSI_GLASS_BG ANSI_GLASS_EDGE, stdout);
    fputs("╚══════════════════════════════════════════════════════════╝\n", stdout);
    fputs(ANSI_RESET "\n", stdout);
    fflush(stdout);
}

static void ui_ok(const char *msg)
{
    printf("%s%s✦ %s%s\n", ANSI_BOLD, ANSI_OK, msg, ANSI_RESET);
    fflush(stdout);
}

static void ui_info(const char *msg)
{
    printf("%s%s› %s%s\n", ANSI_DIM, ANSI_MAUVE, msg, ANSI_RESET);
}

static void ui_err(const char *msg)
{
    printf("%s%s✗ %s%s\n", ANSI_BOLD, ANSI_ERR, msg, ANSI_RESET);
}

static void ui_prompt(void)
{
    fputs(ANSI_BOLD ANSI_ROSE "upfs" ANSI_RESET, stdout);
    fputs(ANSI_MAUVE ":", stdout);
    if (!g_mounted) {
        fputs(ANSI_DIM "(unmounted)" ANSI_RESET, stdout);
    } else {
        fputs(g_cwd, stdout);
        fputs(ANSI_RESET, stdout);
    }
    fputs(ANSI_DIM ANSI_MAUVE " ›" ANSI_RESET " ", stdout);
}

// ---------- 磁盘检测与挂载 ----------

static int disk_file_exists(const char *path)
{
    FILE *fp;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

static void startup_disk_probe(void)
{
    ui_info("正在检测本地虚拟盘镜像…");

    if (disk_file_exists(g_disk_path)) {
        printf("\n");
        ui_ok("检测到已有虚拟盘文件");
        ui_info("可使用  mount  恢复文件系统");
        ui_info("或使用  format  重新格式化（将覆盖原有数据）");
        printf("\n");
    } else {
        printf("\n");
        ui_info("未找到虚拟盘镜像，请先执行：");
        printf("    %sformat%s  %s— 初始化并创建 %s%s\n",
               ANSI_ROSE, ANSI_RESET,
               ANSI_DIM, g_disk_path, ANSI_RESET);
        printf("\n");
    }
}

static int shell_mount(const char *path)
{
    const char *use_path = (path != NULL && path[0] != '\0') ? path : g_disk_path;

    if (g_mounted) {
        ui_err("文件系统已挂载，请先 umount");
        return -1;
    }

    if (!disk_file_exists(use_path)) {
        ui_err("虚拟盘文件不存在，请先 format");
        return -1;
    }

    if (fs_mount(use_path) != 0) {
        ui_err("挂载失败：镜像损坏或格式不匹配");
        return -1;
    }

    if (path != NULL && path[0] != '\0' && path != g_disk_path) {
        strncpy(g_disk_path, use_path, sizeof(g_disk_path) - 1);
        g_disk_path[sizeof(g_disk_path) - 1] = '\0';
    }

    memset(&g_user, 0, sizeof(g_user));
    strncpy(g_user.u_name, "root", sizeof(g_user.u_name) - 1);
    g_user.u_uid    = 0;
    g_user.u_gid    = 0;
    g_user.u_cdir   = ROOT_INODE_NO;
    g_user.u_active = 1;
    dir_bind_user(&g_user);

    strncpy(g_cwd, "/", sizeof(g_cwd) - 1);
    g_cwd[sizeof(g_cwd) - 1] = '\0';
    g_mounted = 1;

    ui_ok("文件系统挂载成功");
    return 0;
}

static int shell_umount(void)
{
    if (!g_mounted) {
        ui_err("当前未挂载");
        return -1;
    }

    if (fs_umount() != 0) {
        ui_err("卸载失败");
        return -1;
    }

    g_mounted = 0;
    strncpy(g_cwd, "/", sizeof(g_cwd) - 1);
    ui_ok("文件系统已卸载并保存");
    return 0;
}

static int shell_format(const char *path)
{
    const char *use_path = (path != NULL && path[0] != '\0') ? path : g_disk_path;

    if (g_mounted) {
        ui_err("请先 umount 再格式化");
        return -1;
    }

    if (format(use_path) != 0) {
        ui_err("格式化失败");
        return -1;
    }

    if (path != NULL && path[0] != '\0' && path != g_disk_path) {
        strncpy(g_disk_path, use_path, sizeof(g_disk_path) - 1);
        g_disk_path[sizeof(g_disk_path) - 1] = '\0';
    }
    ui_ok("格式化完成");

    return shell_mount(use_path);
}

// ---------- 工作目录显示路径维护 ----------

static void cwd_normalize(void)
{
    size_t len;

    if (g_cwd[0] == '\0') {
        strncpy(g_cwd, "/", CWD_BUF_SIZE - 1);
    }
    len = strlen(g_cwd);
    if (len > 1 && g_cwd[len - 1] == '/') {
        g_cwd[len - 1] = '\0';
    }
}

static void cwd_set(const char *path)
{
    char tmp[CWD_BUF_SIZE * 2];

    if (path == NULL || path[0] == '\0') {
        return;
    }
    if (path[0] == '/') {
        strncpy(g_cwd, path, CWD_BUF_SIZE - 1);
    } else {
        if (strcmp(g_cwd, "/") == 0) {
            snprintf(tmp, sizeof(tmp), "/%s", path);
        } else {
            snprintf(tmp, sizeof(tmp), "%s/%s", g_cwd, path);
        }
        strncpy(g_cwd, tmp, CWD_BUF_SIZE - 1);
    }
    g_cwd[CWD_BUF_SIZE - 1] = '\0';
    cwd_normalize();
}

static void cwd_update_after_cd(const char *arg)
{
    char *slash;

    if (arg == NULL || arg[0] == '\0') {
        return;
    }

    if (strcmp(arg, ".") == 0) {
        return;
    }

    if (strcmp(arg, "..") == 0) {
        slash = strrchr(g_cwd, '/');
        if (slash != NULL && slash != g_cwd) {
            *slash = '\0';
        } else {
            strncpy(g_cwd, "/", CWD_BUF_SIZE - 1);
        }
        g_cwd[CWD_BUF_SIZE - 1] = '\0';
        return;
    }

    if (arg[0] == '/') {
        strncpy(g_cwd, arg, CWD_BUF_SIZE - 1);
        g_cwd[CWD_BUF_SIZE - 1] = '\0';
        cwd_normalize();
        return;
    }

    cwd_set(arg);
}

// ---------- 命令行解析（支持引号参数） ----------

static char *trim(char *s)
{
    char *end;

    if (s == NULL) {
        return NULL;
    }
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}

static int parse_octal_mode(const char *s, uint16_t *out)
{
    long v;
    char *end;

    if (s == NULL || out == NULL) {
        return -1;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        return -1;
    }
    v = strtol(s, &end, 8);
    if (end == s || *end != '\0' || v < 0 || v > 0777) {
        return -1;
    }
    *out = (uint16_t)v;
    return 0;
}

// 将一行拆分为 argv；支持 "..." 与 '...'；返回参数个数
static int parse_command_line(char *line, char **argv, int max_argc)
{
    int   argc = 0;
    char *p = line;
    char  quote = 0;

    while (*p != '\0') {
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (argc >= max_argc - 1) {
            break;
        }

        if (*p == '"' || *p == '\'') {
            quote = *p++;
            argv[argc++] = p;
            while (*p != '\0' && *p != quote) {
                p++;
            }
            if (*p == quote) {
                *p = '\0';
                p++;
            }
        } else {
            argv[argc++] = p;
            while (*p != '\0' && !isspace((unsigned char)*p)) {
                p++;
            }
            if (*p != '\0') {
                *p = '\0';
                p++;
            }
        }
    }

    argv[argc] = NULL;
    return argc;
}

// ---------- 命令实现 ----------

static void cmd_help(void)
{
    printf("\n");
    printf("%s%s  系统命令%s\n", ANSI_BOLD, ANSI_MAUVE, ANSI_RESET);
    printf("  %sformat%s  [path]          格式化虚拟盘（默认 %s）\n",
           ANSI_ROSE, ANSI_RESET, DEFAULT_DISK_PATH);
    printf("  %smount%s   [path]          挂载 / 恢复已有镜像\n", ANSI_ROSE, ANSI_RESET);
    printf("  %sumount%s                  卸载并保存\n", ANSI_ROSE, ANSI_RESET);
    printf("\n");
    printf("%s%s  目录与导航%s\n", ANSI_BOLD, ANSI_MAUVE, ANSI_RESET);
    printf("  %smkdir%s   <path> [mode]     创建目录（mode 八进制，默认 0755）\n",
           ANSI_ROSE, ANSI_RESET);
    printf("  %scd%s      <path>            切换工作目录\n", ANSI_ROSE, ANSI_RESET);
    printf("  %spwd%s                       显示当前目录\n", ANSI_ROSE, ANSI_RESET);
    printf("  %sls%s      [path]            列出目录（dir 同义）\n", ANSI_ROSE, ANSI_RESET);
    printf("\n");
    printf("%s%s  文件操作%s\n", ANSI_BOLD, ANSI_MAUVE, ANSI_RESET);
    printf("  %screate%s  <path> [mode]     创建普通文件（默认 0644）\n",
           ANSI_ROSE, ANSI_RESET);
    printf("  %swrite%s   <path> <data>     写入文本（支持引号）\n", ANSI_ROSE, ANSI_RESET);
    printf("  %scat%s     <path>            显示文件内容\n", ANSI_ROSE, ANSI_RESET);
    printf("  %srm%s      <path>            删除文件\n", ANSI_ROSE, ANSI_RESET);
    printf("\n");
    printf("%s%s  其它%s\n", ANSI_BOLD, ANSI_MAUVE, ANSI_RESET);
    printf("  %shelp%s                      显示本帮助\n", ANSI_ROSE, ANSI_RESET);
    printf("  %sclear%s                     清屏\n", ANSI_ROSE, ANSI_RESET);
    printf("  %sexit%s                      退出 Shell\n", ANSI_ROSE, ANSI_RESET);
    printf("\n");
}

static int require_mounted(void)
{
    if (!g_mounted) {
        ui_err("文件系统未挂载，请先 mount 或 format");
        return -1;
    }
    return 0;
}

static int cmd_cat(const char *path)
{
    int   fd;
    char  buf[512];
    int   n;

    if (require_mounted() != 0) {
        return -1;
    }
    if (path == NULL) {
        ui_err("用法: cat <path>");
        return -1;
    }

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        ui_err("无法打开文件");
        return -1;
    }

    printf("%s", ANSI_MAUVE);
    while ((n = read(fd, buf, (int)sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        fputs(buf, stdout);
    }
    printf("%s", ANSI_RESET);

    if (n < 0) {
        close(fd);
        ui_err("读取失败");
        return -1;
    }

    if (fputc('\n', stdout) == EOF) {
        /* ignore */
    }
    close(fd);
    return 0;
}

static int cmd_write_existing(const char *path, const char *data)
{
    int fd;
    int n;
    int len;

    fd = open(path, O_WRONLY);
    if (fd < 0) {
        if (create(path, 0644) != 0) {
            ui_err("无法创建或打开文件");
            return -1;
        }
        fd = open(path, O_WRONLY);
        if (fd < 0) {
            ui_err("open 失败");
            return -1;
        }
    }

    len = (int)strlen(data);
    n = write(fd, data, len);
    close(fd);

    if (n != len) {
        ui_err("写入失败");
        return -1;
    }

    ui_ok("写入成功");
    return 0;
}

static int dispatch_command(int argc, char **argv)
{
    const char *cmd;

    if (argc <= 0 || argv[0] == NULL) {
        return 0;
    }

    cmd = argv[0];

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        cmd_help();
        return 0;
    }
    if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
        return 1;
    }
    if (strcmp(cmd, "clear") == 0) {
        printf("\033[2J\033[H");
        ui_banner();
        return 0;
    }
    if (strcmp(cmd, "format") == 0) {
        return shell_format(argc > 1 ? argv[1] : NULL) == 0 ? 0 : -1;
    }
    if (strcmp(cmd, "mount") == 0 || strcmp(cmd, "restore") == 0) {
        return shell_mount(argc > 1 ? argv[1] : NULL) == 0 ? 0 : -1;
    }
    if (strcmp(cmd, "umount") == 0) {
        return shell_umount() == 0 ? 0 : -1;
    }

    if (require_mounted() != 0) {
        return -1;
    }

    if (strcmp(cmd, "mkdir") == 0) {
        uint16_t mode = 0755;
        if (argc < 2) {
            ui_err("用法: mkdir <path> [mode]");
            return -1;
        }
        if (argc >= 3 && parse_octal_mode(argv[2], &mode) != 0) {
            ui_err("mode 须为八进制，如 0755");
            return -1;
        }
        if (mkdir(argv[1], mode) != 0) {
            ui_err("mkdir 失败");
            return -1;
        }
        ui_ok("目录已创建");
        return 0;
    }

    if (strcmp(cmd, "cd") == 0 || strcmp(cmd, "chdir") == 0) {
        if (argc < 2) {
            ui_err("用法: cd <path>");
            return -1;
        }
        if (chdir(argv[1]) != 0) {
            ui_err("cd 失败");
            return -1;
        }
        cwd_update_after_cd(argv[1]);
        ui_ok("已切换目录");
        return 0;
    }

    if (strcmp(cmd, "pwd") == 0) {
        printf("%s%s%s\n", ANSI_MAUVE, g_cwd, ANSI_RESET);
        return 0;
    }

    if (strcmp(cmd, "ls") == 0 || strcmp(cmd, "dir") == 0) {
        if (dir_list(argc > 1 ? argv[1] : NULL) != 0) {
            ui_err("ls 失败");
            return -1;
        }
        return 0;
    }

    if (strcmp(cmd, "create") == 0) {
        uint16_t mode = 0644;
        if (argc < 2) {
            ui_err("用法: create <path> [mode]");
            return -1;
        }
        if (argc >= 3 && parse_octal_mode(argv[2], &mode) != 0) {
            ui_err("mode 须为八进制，如 0644");
            return -1;
        }
        if (create(argv[1], mode) != 0) {
            ui_err("create 失败");
            return -1;
        }
        ui_ok("文件已创建");
        return 0;
    }

    if (strcmp(cmd, "write") == 0) {
        if (argc < 3) {
            ui_err("用法: write <path> <data>");
            return -1;
        }
        return cmd_write_existing(argv[1], argv[2]);
    }

    if (strcmp(cmd, "cat") == 0) {
        return cmd_cat(argv[1]);
    }

    if (strcmp(cmd, "rm") == 0 || strcmp(cmd, "delete") == 0) {
        if (argc < 2) {
            ui_err("用法: rm <path>");
            return -1;
        }
        if (delete(argv[1]) != 0) {
            ui_err("删除失败");
            return -1;
        }
        ui_ok("文件已删除");
        return 0;
    }

    ui_err("未知命令，输入 help 查看帮助");
    return -1;
}

// ---------- 主循环 ----------

int main(void)
{
    char  line[LINE_BUF_SIZE];
    char *argv[MAX_ARGS];
    int   argc;
    int   exit_flag = 0;

    ui_enable_ansi();
    setvbuf(stdout, NULL, _IONBF, 0);
    ui_banner();
    startup_disk_probe();
    ui_info("输入 help 查看命令列表");
    fflush(stdout);

    while (!exit_flag) {
        ui_prompt();

        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }

        if (trim(line)[0] == '\0') {
            continue;
        }

        argc = parse_command_line(line, argv, MAX_ARGS);
        if (dispatch_command(argc, argv) == 1) {
            exit_flag = 1;
        }
    }

    if (g_mounted) {
        fs_umount();
    }

    ui_ok("再见");
    return 0;
}
