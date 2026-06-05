/*
 * process.h
 * 进程控制块、fork/exec/wait 与 UPX 加载相关声明。
 */
#ifndef PROCESS_H
#define PROCESS_H

#include "kernel/cpu.h"
#include "kernel/memory.h"
#include "kernel/ipc.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROC_MAX_COUNT         64
#define PROC_NAME_LEN          32
#define PROC_STACK_PAGES       4
#define PROC_STACK_SIZE        (PROC_STACK_PAGES * MEM_PAGE_SIZE)
#define PROC_STACK_TOP         0x01000000U

enum {
    PROC_FREE    = 0,
    PROC_READY   = 1,
    PROC_RUNNING = 2,
    PROC_BLOCKED = 3,
    PROC_ZOMBIE  = 4,
};

#define PROC_FD_TERM     0
#define PROC_FD_FILE     1
#define PROC_FD_PIPE_RD  2
#define PROC_FD_PIPE_WR  3
#define PROC_FD_FIFO_RD  4
#define PROC_FD_FIFO_WR  5

#define PROC_FD_IS_PIPE_RD(t)  ((t) == PROC_FD_PIPE_RD || (t) == PROC_FD_FIFO_RD)
#define PROC_FD_IS_PIPE_WR(t)  ((t) == PROC_FD_PIPE_WR || (t) == PROC_FD_FIFO_WR)

typedef struct ProcFD {
    int  fd_type;
    int  fd_fs_fd;
    int  fd_pipe_id;
    int  fd_mode;
    uint32_t fd_pos;
} ProcFD;

#define PROC_MAX_FD  16

typedef struct UPXHeader {
    char     magic[4];
    uint32_t entry;
    uint32_t text_size;
    uint32_t data_size;
    uint32_t bss_size;
    uint32_t stack_size;
} UPXHeader;


typedef struct PCB {
    uint32_t  p_pid; 
    uint32_t  p_ppid; 
    char      p_name[PROC_NAME_LEN]; 
    uint8_t   p_state; 


    CPUContext p_cpu; 


    uint32_t  p_page_table[MEM_MAX_PROCESS_PAGES]; 
    uint32_t  p_text_start;    
    uint32_t  p_text_pages;   
    uint32_t  p_data_start;  
    uint32_t  p_data_pages;  
    uint32_t  p_bss_end;
    uint32_t  p_stack_top;
    uint32_t  p_heap_brk;


    ProcFD    p_ofile[PROC_MAX_FD];
    int       p_cwd_ino;


    char      **p_envp;
    int        p_envc;


    uint32_t  p_children[PROC_MAX_COUNT];
    int       p_child_count;
    int       p_exit_code;
    uint32_t  p_waiting_parent;


    uint32_t  p_pending_sig;
    int       p_sigusr1_count;
    ShmAttach p_shm[IPC_SHM_MAX_ATTACH];

} PCB;

// 清零进程表并初始化 IPC 子系统
int  proc_init(void);

// 关闭全部进程的文件描述符并清空进程表
void proc_shutdown(void);

// 返回当前正在运行的进程 PCB
PCB *proc_current(void);

// 设置当前运行进程
void proc_set_current(PCB *p);

// 按 PID 在进程表中查找 PCB
PCB *proc_find(uint32_t pid);

// 分配一个空闲 PCB 槽并初始化页表
PCB *proc_alloc(void);

// 释放进程占用的页、fd 和环境变量
void proc_free(PCB *p);

// 创建或复用 PID 为 0 的 init 进程
PCB *proc_create_init(void);

// 复制父进程地址空间与 fd，子进程返回 0
int  proc_fork(void);

// 创建匿名管道并分配读/写两个 fd
int  proc_pipe(int fds[2]);

// 从路径加载 UPX 程序并替换进程映像
int  proc_exec(PCB *p, const char *path);

// 阻塞等待任一子进程退出并回收其 PCB
int  proc_wait(int *status);

// 当前进程进入僵尸态并记录退出码
void proc_exit(int code);

// 统计非 FREE 状态的进程数量
int  proc_count(void);

// 返回进程表数组及容量
PCB *proc_get_table(int *count_out);

// 校验 UPX 文件头与段大小是否合法
int  upx_validate(const uint8_t *data, uint32_t size);

// 把 UPX 映像加载到进程虚拟地址空间
int  upx_load(PCB *p, const uint8_t *data, uint32_t size);

// 在进程 fd 表中分配一个空闲槽
int  proc_alloc_fd(PCB *p);

// 关闭 fd 对应资源并清空槽位
void proc_free_fd(PCB *p, int fd);

// 调整进程堆顶并按需映射新页
uint32_t proc_sbrk(PCB *p, int32_t increment);

#ifdef __cplusplus
}
#endif

#endif
