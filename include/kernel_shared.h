// kernel_shared.h —— 多终端共享内核状态
//
// 在 --serve 模式下，所有终端子进程共享同一个内核实例：
//   物理内存、进程表、调度器队列等都放在 mmap 共享内存中。
// 非 --serve 模式退化为普通全局变量（通过宏 KERNEL 访问）。

#ifndef KERNEL_SHARED_H
#define KERNEL_SHARED_H

#include "vfs_core.h"
#include "kernel/memory.h"
#include "kernel/process.h"
#include "kernel/scheduler.h"
#include <pthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCHED_QUEUE_SIZE  (PROC_MAX_COUNT + 1)

typedef struct KernelShared {
    // ---- 内存管理器 ----
    uint8_t  phys_mem[MEM_TOTAL_SIZE];
    uint8_t  page_bitmap[(MEM_TOTAL_PAGES + 7) / 8];
    int      total_pages;
    int      free_pages;

    // ---- 进程管理器 ----
    PCB      proc_table[PROC_MAX_COUNT];
    int      next_pid;
    int      current_idx;       // 当前运行进程在 proc_table 中的下标，-1 表示无

    // ---- 调度器 ----
    int      ready_indices[SCHED_QUEUE_SIZE]; // 用 PCB 下标代替指针
    int      ready_head;
    int      ready_tail;
    int      ready_count;
    int      sched_inited;

    // ---- 同步 ----
    volatile int initialized;   // 0=未初始化, 1=就绪
    pthread_mutex_t lock;
} KernelShared;

// 当前进程：用 proc_table 下标表示（-1 = 无），跨进程安全
// 访问：kernel->proc_table[kernel->current_idx]

// 全局内核实例指针
extern KernelShared *g_kernel;

// 本地模式初始化（非 --serve）
int  kernel_local_init(void);
void kernel_local_shutdown(void);

// ---- 分配 / 访问共享内核 ----

// 创建并初始化共享内核（仅主进程调用一次）
KernelShared *kernel_shared_create(void);

// 附加到已有共享内核（子进程调用）
KernelShared *kernel_shared_attach(void);

// 销毁共享内核
void kernel_shared_destroy(KernelShared *k);

#ifdef __cplusplus
}
#endif

#endif // KERNEL_SHARED_H
