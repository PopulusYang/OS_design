/*
 * editor.c
 * vi 风格编辑，经 VFS 读写文件内容。
 */
#include "editor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <errno.h>

enum { M_NORMAL, M_INSERT, M_COMMAND };
#define MAX_LINES   4096
#define TAB_STOP    4

typedef struct {
    char  *lines[MAX_LINES];
    int    nlines;
    int    cx, cy;
    int    scroll;
    int    mode;
    char   cmd[256];
    int    cmd_len;
    char   path[512];
    int    modified;
    int    rows, cols;
    char   status[128];
    int    dirty;
} Ed;

static Ed g_ed;
static struct termios g_orig;
static char *g_yank = NULL;

// 把终端设为原始模式以捕获按键
static void term_raw(void) {
    tcgetattr(STDIN_FILENO, &g_orig);
    struct termios r = g_orig;
    r.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN | ISIG);
    r.c_iflag &= (tcflag_t)~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    r.c_oflag &= (tcflag_t)~(OPOST);
    r.c_cflag |= CS8;
    r.c_cc[VMIN]  = 0;
    r.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &r);
}

// 恢复终端原先属性
static void term_restore(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig);
}

// 获取终端窗口行数与列数
static void get_term_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
        *rows = (int)ws.ws_row;
        *cols = (int)ws.ws_col;
    } else {
        *rows = 24; *cols = 80;
    }
}

// 格式化并保存底部状态栏文字
static void set_status(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(g_ed.status, sizeof(g_ed.status), fmt, ap);
    va_end(ap);
}

// 重绘编辑区、状态栏与命令行
static void ed_render(void) {
    get_term_size(&g_ed.rows, &g_ed.cols);
    int rows = g_ed.rows, cols = g_ed.cols;

    int vis_rows = rows - 2;
    if (vis_rows < 1) vis_rows = 1;
    if (g_ed.cy < g_ed.scroll) g_ed.scroll = g_ed.cy;
    if (g_ed.cy >= g_ed.scroll + vis_rows) g_ed.scroll = g_ed.cy - vis_rows + 1;
    if (g_ed.scroll < 0) g_ed.scroll = 0;


    printf("\033[2J\033[H\033[?25l");


    const char *mode_str = g_ed.mode == M_NORMAL  ? "NORMAL"  :
                           g_ed.mode == M_INSERT  ? "INSERT"  : "COMMAND";
    const char *mod_color = g_ed.mode == M_NORMAL  ? "\033[48;2;100;120;180m\033[37m" :
                            g_ed.mode == M_INSERT  ? "\033[48;2;80;160;80m\033[37m" :
                                                     "\033[48;2;180;140;40m\033[37m";
    printf("%s %-6s \033[0m \033[2m%s\033[0m%s%s\033[K\r\n",
           mod_color, mode_str,
           g_ed.path[0] ? g_ed.path : "[No Name]",
           g_ed.modified ? " \033[38;2;88;166;255m[modified]\033[0m" : "",
           g_ed.path[0] ? "" : "");


    int end = g_ed.scroll + vis_rows;
    if (end > g_ed.nlines) end = g_ed.nlines;
    for (int i = g_ed.scroll; i < end; i++) {

        printf("\033[2m\033[38;2;100;100;100m%4d \033[0m", i + 1);

        char *line = g_ed.lines[i];
        int len = (int)strlen(line);
        int col = 0;
        for (int j = 0; j < len && col < cols - 6; j++) {
            if (line[j] == '\t') {
                int sp = TAB_STOP - (col % TAB_STOP);
                for (int s = 0; s < sp && col < cols - 6; s++, col++)
                    putchar(' ');
            } else {
                putchar(line[j]);
                col++;
            }
        }
        printf("\033[K\r\n");
    }


    for (int i = end - g_ed.scroll; i < vis_rows; i++)
        printf("\033[2m~\033[0m\033[K\r\n");


    if (g_ed.mode == M_COMMAND) {
        printf("\033[48;2;40;40;40m:%s\033[K", g_ed.cmd);
        int cx = g_ed.cmd_len + 1;
        printf("\033[%d;%dH", rows, cx);
    } else {
        printf("\033[48;2;40;40;40m\033[2m%s\033[K", g_ed.status[0] ? g_ed.status : g_ed.path);
    }
    printf("\033[0m");


    int screen_row = (g_ed.cy - g_ed.scroll) + 2;
    int screen_col = 6;
    char *cur_line = g_ed.lines[g_ed.cy];
    for (int j = 0; j < g_ed.cx && j < (int)strlen(cur_line); j++) {
        if (cur_line[j] == '\t') screen_col += TAB_STOP - (screen_col - 6) % TAB_STOP;
        else screen_col++;
    }
    printf("\033[%d;%dH\033[?25h", screen_row, screen_col);
    fflush(stdout);
}

// 在指定行号插入一行文本
static int line_insert(int at, const char *s) {
    if (g_ed.nlines >= MAX_LINES) return -1;
    for (int i = g_ed.nlines; i > at; i--) g_ed.lines[i] = g_ed.lines[i - 1];
    g_ed.lines[at] = strdup(s ? s : "");
    g_ed.nlines++;
    g_ed.modified = 1;
    return 0;
}

// 删除指定行号的文本行
static void line_delete(int at) {
    if (at < 0 || at >= g_ed.nlines) return;
    free(g_ed.lines[at]);
    for (int i = at; i < g_ed.nlines - 1; i++) g_ed.lines[i] = g_ed.lines[i + 1];
    g_ed.nlines--;
    g_ed.modified = 1;
}

// 在光标处插入一个字符
static void char_insert(int ch) {
    char *line = g_ed.lines[g_ed.cy];
    int len = (int)strlen(line);
    line = realloc(line, (size_t)(len + 2));
    if (line == NULL) return;
    g_ed.lines[g_ed.cy] = line;
    memmove(line + g_ed.cx + 1, line + g_ed.cx, (size_t)(len - g_ed.cx + 1));
    line[g_ed.cx] = (char)ch;
    g_ed.cx++;
    g_ed.modified = 1;
}

// 删除光标前一个字符
static void char_delete(void) {
    if (g_ed.cx <= 0) return;
    char *line = g_ed.lines[g_ed.cy];
    int len = (int)strlen(line);
    memmove(line + g_ed.cx - 1, line + g_ed.cx, (size_t)(len - g_ed.cx + 1));
    g_ed.cx--;
    g_ed.modified = 1;
}

// 从 VFS 读文件到行缓冲区
static int ed_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {

        line_insert(0, "");
        g_ed.modified = 0;
        strncpy(g_ed.path, path, sizeof(g_ed.path) - 1);
        return 0;
    }
    char buf[4096];
    while (fgets(buf, sizeof(buf), f)) {
        size_t l = strlen(buf);
        if (l > 0 && buf[l - 1] == '\n') buf[l - 1] = '\0';
        if (l > 0 && buf[l - 1] == '\r') buf[l - 1] = '\0';
        if (g_ed.nlines >= MAX_LINES) break;
        g_ed.lines[g_ed.nlines++] = strdup(buf);
    }
    fclose(f);
    if (g_ed.nlines == 0) line_insert(0, "");
    g_ed.modified = 0;
    strncpy(g_ed.path, path, sizeof(g_ed.path) - 1);
    return 0;
}

// 把行缓冲区写回 VFS 文件
static int ed_save(void) {
    if (!g_ed.path[0]) return -1;
    FILE *f = fopen(g_ed.path, "w");
    if (!f) {
        set_status("Cannot write '%s': %s", g_ed.path, strerror(errno));
        return -1;
    }
    for (int i = 0; i < g_ed.nlines; i++) {
        fputs(g_ed.lines[i], f);
        fputc('\n', f);
    }
    fclose(f);
    g_ed.modified = 0;
    set_status("\"%s\" %dL written", g_ed.path, g_ed.nlines);
    return 0;
}

// 执行命令模式下输入的冒号命令
static int cmd_execute(void) {
    char *c = g_ed.cmd;
    if (strcmp(c, "w") == 0)      { ed_save(); }
    else if (strcmp(c, "q") == 0) {
        if (g_ed.modified) { set_status("No write since last change (:q! to override)"); return 0; }
        return 1;
    }
    else if (strcmp(c, "q!") == 0) { return 1; }
    else if (strcmp(c, "wq") == 0 || strcmp(c, "x") == 0) { ed_save(); return 1; }
    else if (strncmp(c, "e ", 2) == 0) {

        for (int i = 0; i < g_ed.nlines; i++) free(g_ed.lines[i]);
        g_ed.nlines = g_ed.cy = g_ed.cx = g_ed.scroll = 0;
        ed_load(c + 2);
    }
    else { set_status("Unknown command: %s", c); }
    return 0;
}

// 处理普通模式下的按键
static int handle_normal(int ch) {
    switch (ch) {
    case 'h': if (g_ed.cx > 0) g_ed.cx--; break;
    case 'j': if (g_ed.cy < g_ed.nlines - 1) {
                  g_ed.cy++;
                  int nxt = (int)strlen(g_ed.lines[g_ed.cy]);
                  if (g_ed.cx > nxt) g_ed.cx = nxt;
              } break;
    case 'k': if (g_ed.cy > 0) {
                  g_ed.cy--;
                  int nxt = (int)strlen(g_ed.lines[g_ed.cy]);
                  if (g_ed.cx > nxt) g_ed.cx = nxt;
              } break;
    case 'l': if (g_ed.cx < (int)strlen(g_ed.lines[g_ed.cy])) g_ed.cx++; break;
    case '0': g_ed.cx = 0; break;
    case '$': g_ed.cx = (int)strlen(g_ed.lines[g_ed.cy]); break;
    case 'w': case 'W': {

        char *line = g_ed.lines[g_ed.cy];
        int len = (int)strlen(line);
        while (g_ed.cx < len && line[g_ed.cx] != ' ' && line[g_ed.cx] != '\t') g_ed.cx++;
        while (g_ed.cx < len && (line[g_ed.cx] == ' ' || line[g_ed.cx] == '\t')) g_ed.cx++;
        if (g_ed.cx >= len && g_ed.cy < g_ed.nlines - 1) { g_ed.cy++; g_ed.cx = 0; }
        break;
    }
    case 'b': case 'B': {
        if (g_ed.cx == 0 && g_ed.cy > 0) { g_ed.cy--; g_ed.cx = (int)strlen(g_ed.lines[g_ed.cy]); break; }
        char *line = g_ed.lines[g_ed.cy];
        while (g_ed.cx > 0 && (line[g_ed.cx-1] == ' ' || line[g_ed.cx-1] == '\t')) g_ed.cx--;
        while (g_ed.cx > 0 && line[g_ed.cx-1] != ' ' && line[g_ed.cx-1] != '\t') g_ed.cx--;
        break;
    }
    case 'g': g_ed.cy = 0; g_ed.cx = 0; break;
    case 'G': g_ed.cy = g_ed.nlines - 1; g_ed.cx = 0; break;
    case 'i': g_ed.mode = M_INSERT; break;
    case 'a': g_ed.mode = M_INSERT; if (g_ed.cx < (int)strlen(g_ed.lines[g_ed.cy])) g_ed.cx++; break;
    case 'A': g_ed.mode = M_INSERT; g_ed.cx = (int)strlen(g_ed.lines[g_ed.cy]); break;
    case 'o': line_insert(g_ed.cy + 1, ""); g_ed.cy++; g_ed.cx = 0; g_ed.mode = M_INSERT; break;
    case 'O': line_insert(g_ed.cy, ""); g_ed.cx = 0; g_ed.mode = M_INSERT; break;
    case 'x': {
        char *line = g_ed.lines[g_ed.cy];
        int len = (int)strlen(line);
        if (len > 0 && g_ed.cx < len) {
            memmove(line + g_ed.cx, line + g_ed.cx + 1, (size_t)(len - g_ed.cx));
            g_ed.modified = 1;
        } else if (len == 0 || g_ed.cx >= len) {

            if (g_ed.cy < g_ed.nlines - 1) {
                size_t cur_len = strlen(g_ed.lines[g_ed.cy]);
                g_ed.lines[g_ed.cy] = realloc(g_ed.lines[g_ed.cy], cur_len + strlen(g_ed.lines[g_ed.cy + 1]) + 1);
                strcat(g_ed.lines[g_ed.cy], g_ed.lines[g_ed.cy + 1]);
                line_delete(g_ed.cy + 1);
                g_ed.modified = 1;
            }
        }
        break;
    }
    case 'y': {

        int y2 = 0;
        struct pollfd p = { .fd = STDIN_FILENO, .events = POLLIN };
        if (poll(&p, 1, 500) > 0 && read(STDIN_FILENO, &y2, 1) == 1 && y2 == 'y') {
            free(g_yank);
            g_yank = strdup(g_ed.lines[g_ed.cy]);
            set_status("1 line yanked");
        }
        break;
    }
    case 'd': {

        int d2 = 0;
        struct pollfd p = { .fd = STDIN_FILENO, .events = POLLIN };
        if (poll(&p, 1, 500) > 0 && read(STDIN_FILENO, &d2, 1) == 1 && d2 == 'd') {
            if (g_ed.nlines > 1 || strlen(g_ed.lines[0]) > 0) {
                free(g_yank);
                g_yank = strdup(g_ed.lines[g_ed.cy]);
                line_delete(g_ed.cy);
                if (g_ed.cy >= g_ed.nlines) g_ed.cy = g_ed.nlines - 1;
                g_ed.cx = 0;
                g_ed.modified = 1;
            }
        }
        break;
    }
    case 'p':
        if (g_yank) {
            line_insert(g_ed.cy + 1, g_yank);
            g_ed.cy++;
            g_ed.cx = 0;
            g_ed.modified = 1;
        }
        break;
    case 'P':
        if (g_yank) {
            line_insert(g_ed.cy, g_yank);
            g_ed.cx = 0;
            g_ed.modified = 1;
        }
        break;
    case ':': g_ed.mode = M_COMMAND; g_ed.cmd[0] = '\0'; g_ed.cmd_len = 0; break;
    }
    return 0;
}

// 处理插入模式下的按键
static int handle_insert(int ch) {
    if (ch == 27) {
        g_ed.mode = M_NORMAL;
        if (g_ed.cx > 0) g_ed.cx--;
        return 0;
    }
    if (ch == 127 || ch == '\b') {
        if (g_ed.cx > 0) char_delete();
        else if (g_ed.cy > 0) {

            int prev_len = (int)strlen(g_ed.lines[g_ed.cy - 1]);
            g_ed.lines[g_ed.cy - 1] = realloc(g_ed.lines[g_ed.cy - 1], (size_t)(prev_len + strlen(g_ed.lines[g_ed.cy]) + 1));
            strcat(g_ed.lines[g_ed.cy - 1], g_ed.lines[g_ed.cy]);
            line_delete(g_ed.cy);
            g_ed.cy--;
            g_ed.cx = prev_len;
            g_ed.modified = 1;
        }
        return 0;
    }
    if (ch == '\r' || ch == '\n') {

        char *line = g_ed.lines[g_ed.cy];
        char *rest = strdup(line + g_ed.cx);
        line[g_ed.cx] = '\0';
        line_insert(g_ed.cy + 1, rest);
        free(rest);
        g_ed.cy++;
        g_ed.cx = 0;
        g_ed.modified = 1;
        return 0;
    }
    if (ch >= 32 && ch < 127) {
        char_insert(ch);
    }
    return 0;
}

// 处理命令模式下的按键
static int handle_command(int ch) {
    if (ch == 27) {
        g_ed.mode = M_NORMAL;
        g_ed.cmd_len = 0;
        return 0;
    }
    if (ch == '\r' || ch == '\n') {
        g_ed.cmd[g_ed.cmd_len] = '\0';
        int ret = cmd_execute();
        g_ed.mode = M_NORMAL;
        g_ed.cmd_len = 0;
        return ret;
    }
    if (ch == 127 || ch == '\b') {
        if (g_ed.cmd_len > 0) g_ed.cmd_len--;
        return 0;
    }
    if (ch >= 32 && ch < 127 && g_ed.cmd_len < (int)sizeof(g_ed.cmd) - 1) {
        g_ed.cmd[g_ed.cmd_len++] = (char)ch;
    }
    return 0;
}

// 打开文件并进入全屏编辑循环
int editor_open(const char *path) {
    memset(&g_ed, 0, sizeof(g_ed));
    ed_load(path);
    if (g_ed.nlines == 0) line_insert(0, "");
    g_ed.mode = M_NORMAL;

    term_raw();
    printf("\033[?1049h");
    ed_render();

    for (;;) {
        unsigned char seq[8];
        int n = (int)read(STDIN_FILENO, seq, sizeof(seq));
        if (n <= 0) { ed_render(); continue; }

        int ch = seq[0];
        int quit = 0;


        if (ch == 27 && n >= 3 && seq[1] == '[') {
            switch (seq[2]) {
            case 'A': ch = 'k'; break;
            case 'B': ch = 'j'; break;
            case 'C': ch = 'l'; break;
            case 'D': ch = 'h'; break;
            case 'H': ch = '0'; break;
            case 'F': ch = '$'; break;
            case '1': if (n >= 5 && seq[3] == ';') ch = '0'; break;
            case '3': ch = 'x'; break;
            case '5': ch = 'g'; break;
            case '6': ch = 'G'; break;
            default:  ch = 0; break;
            }
        } else if (ch == 27 && n == 1) {

            ch = 27;
        }

        if (ch == 0) { ed_render(); continue; }

        switch (g_ed.mode) {
        case M_NORMAL:  quit = handle_normal(ch); break;
        case M_INSERT:  quit = handle_insert(ch); break;
        case M_COMMAND: quit = handle_command(ch); break;
        }

        ed_render();
        if (quit) break;
    }

    printf("\033[?1049l");
    term_restore();
    return 0;
}
