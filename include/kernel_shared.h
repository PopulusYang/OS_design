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

#define SCHED_QUEUE_SIZE  (PROC_MAX_COUNT + 1)

//内核共享数据结构，驻留在共享内存中，供所有进程访问
typedef struct KernelShared {
    
    uint8_t  phys_mem[MEM_TOTAL_SIZE];  //物理内存模拟 128MB
    uint8_t  page_bitmap[(MEM_TOTAL_PAGES + 7) / 8]; //物理页使用位图，总页数加7除以8向上取整得到字节数
    int      total_pages; //物理内存总页数
    int      free_pages; //剩余可用页数

    
    PCB      proc_table[PROC_MAX_COUNT]; //进程表，包含所有进程的PCB，最多支持PROC_MAX_COUNT个进程
    int      next_pid; //下一个可用的PID，分配时递增
    int      current_idx;  //当前运行进程在proc_table中的索引，-1表示没有正在运行的进程

    
    int      ready_indices[SCHED_QUEUE_SIZE];  //就绪队列，存储处于READY状态的进程在proc_table中的索引，循环使用
    int      ready_head; //就绪队列头索引，指向下一个要调度的进程
    int      ready_tail; //就绪队列尾索引
    int      ready_count; //就绪队列中进程的数量
    int      sched_inited; //调度器是否已初始化

    
    Pipe     pipes[PIPE_MAX_COUNT]; //管道表，包含所有管道的状态，最多支持PIPE_MAX_COUNT个管道
    int      pipe_used[PIPE_MAX_COUNT]; //管道使用标记，0表示空闲，1表示已分配

    
    IpcTable ipc;

    
    volatile int initialized;   
    pthread_mutex_t lock;
} KernelShared;





extern KernelShared *g_kernel;


int  kernel_local_init(void);
void kernel_local_shutdown(void);




KernelShared *kernel_shared_create(void);


KernelShared *kernel_shared_attach(void);


void kernel_shared_destroy(KernelShared *k);

#ifdef __cplusplus
}
#endif

#endif 
