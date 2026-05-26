// process.h —— 进程管理：PCB、进程表、fork/exec/wait/exit、可执行文件加载
//
// 每个进程有独立的虚拟地址空间（页表），由 CPUContext 描述寄存器状态。
// 调度器通过 schedule() 切换当前进程。

#ifndef PROCESS_H
#define PROCESS_H

#include "kernel/cpu.h"
#include "kernel/memory.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROC_MAX_COUNT         64
#define PROC_NAME_LEN          32
#define PROC_STACK_PAGES       4      // 默认 16KB 栈
#define PROC_STACK_SIZE        (PROC_STACK_PAGES * MEM_PAGE_SIZE)
#define PROC_STACK_TOP         0x01000000U  // 用户栈顶（16MB 地址空间顶端）

enum {
    PROC_FREE    = 0,
    PROC_READY   = 1,
    PROC_RUNNING = 2,
    PROC_BLOCKED = 3,
    PROC_ZOMBIE  = 4,
};

// FD 类型：0=终端, 1=文件
#define PROC_FD_TERM   0
#define PROC_FD_FILE   1

typedef struct ProcFD {
    int  fd_type;           // PROC_FD_TERM / PROC_FD_FILE
    int  fd_fs_fd;          // 如果是文件，UPFS 的底层 fd
    int  fd_mode;           // O_RDONLY / O_WRONLY / O_RDWR
    uint32_t fd_pos;        // 读写位置
} ProcFD;

#define PROC_MAX_FD  16

// UPX 可执行文件头
typedef struct UPXHeader {
    char     magic[4];       // "UPX\0"
    uint32_t entry;          // 入口指令偏移
    uint32_t text_size;
    uint32_t data_size;
    uint32_t bss_size;
    uint32_t stack_size;
} UPXHeader;

// PCB
typedef struct PCB {
    uint32_t  p_pid;
    uint32_t  p_ppid;
    char      p_name[PROC_NAME_LEN];
    uint8_t   p_state;

    // VM 上下文
    CPUContext p_cpu;

    // 内存布局（虚拟地址）
    uint32_t  p_page_table[MEM_MAX_PROCESS_PAGES];
    uint32_t  p_text_start;      // 0x01000000
    uint32_t  p_text_pages;
    uint32_t  p_data_start;
    uint32_t  p_data_pages;
    uint32_t  p_bss_end;
    uint32_t  p_stack_top;       // PROC_STACK_TOP
    uint32_t  p_heap_brk;        // bss_end 之后

    // 打开文件表
    ProcFD    p_ofile[PROC_MAX_FD];
    int       p_cwd_ino;

    // 环境变量
    char      **p_envp;
    int        p_envc;

    // 父子关系
    uint32_t  p_children[PROC_MAX_COUNT];
    int       p_child_count;
    int       p_exit_code;
    uint32_t  p_waiting_parent;   // 是否有父进程在 wait

} PCB;

// ---------- 进程管理 API ----------

// 初始化进程子系统
int  proc_init(void);

// 关闭进程子系统
void proc_shutdown(void);

// 获取当前运行进程的 PCB
PCB *proc_current(void);

// 设置当前运行进程
void proc_set_current(PCB *p);

// 根据 PID 查找进程
PCB *proc_find(uint32_t pid);

// 分配新 PCB（状态 PROC_READY）
PCB *proc_alloc(void);

// 释放 PCB
void proc_free(PCB *p);

// 创建 init 进程（PID 0，idle / shell 主机进程）
PCB *proc_create_init(void);

// fork：复制当前进程，返回子进程 PID（父进程）或 0（子进程）
int  proc_fork(void);

// exec：加载 .upx 可执行文件替换当前进程映像
int  proc_exec(PCB *p, const char *path);

// wait：等待子进程退出，返回子进程 PID，通过 status 获取退出码
int  proc_wait(int *status);

// exit：当前进程退出
void proc_exit(int code);

// 进程数量
int  proc_count(void);

// 获取进程表（用于 ps）
PCB *proc_get_table(int *count_out);

// 验证 UPX 文件头
int  upx_validate(const uint8_t *data, uint32_t size);

// 加载 UPX 二进制到进程地址空间
int  upx_load(PCB *p, const uint8_t *data, uint32_t size);

// 分配 FD
int  proc_alloc_fd(PCB *p);

// 释放 FD
void proc_free_fd(PCB *p, int fd);

// 为进程分配并映射新页（sbrk）
uint32_t proc_sbrk(PCB *p, int32_t increment);

#ifdef __cplusplus
}
#endif

#endif // PROCESS_H
