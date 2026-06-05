/*
 * kernel_shared.h
 * 多进程共享的内核全局状态：物理内存、页位图、进程表、
 * 就绪队列、管道与 IPC 表。serve 模式下通过 mmap 映射到各子进程。
 */
#ifndef KERNEL_SHARED_H
#define KERNEL_SHARED_H

#include "vfs_core.h"
#include "kernel/memory.h"
#include "kernel/pipe.h"
#include "kernel/ipc.h"
#include "kernel/process.h"
#include "kernel/scheduler.h"
#include <pthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 就绪队列环形数组长度（比最大进程数多 1，便于判满）
#define SCHED_QUEUE_SIZE (PROC_MAX_COUNT + 1)

typedef struct KernelShared {
    uint8_t phys_mem[MEM_TOTAL_SIZE];              // 模拟物理内存
    uint8_t page_bitmap[(MEM_TOTAL_PAGES + 7) / 8]; // 页分配位图
    int total_pages;
    int free_pages;
    PCB proc_table[PROC_MAX_COUNT];                // 进程控制块数组
    int next_pid;                                  // 下一个可分配的 PID
    int current_idx;                               // 当前运行进程在表中的下标
    int ready_indices[SCHED_QUEUE_SIZE];           // 就绪队列：存进程表下标
    int ready_head;
    int ready_tail;
    int ready_count;
    int sched_inited;
    Pipe pipes[PIPE_MAX_COUNT];
    int pipe_used[PIPE_MAX_COUNT];                 // 管道槽是否占用
    IpcTable ipc;                                  // 信号量、消息队列、共享内存等
    volatile int initialized;
    pthread_mutex_t lock;                          // 多终端访问时互斥
} KernelShared;

extern KernelShared *g_kernel;

// 单进程 Shell 模式：在本地堆上分配一份内核状态
int kernel_local_init(void);
void kernel_local_shutdown(void);

// serve 模式：创建或附着 mmap 共享区
KernelShared *kernel_shared_create(void);
KernelShared *kernel_shared_attach(void);
void kernel_shared_destroy(KernelShared *k);

#ifdef __cplusplus
}
#endif

#endif
