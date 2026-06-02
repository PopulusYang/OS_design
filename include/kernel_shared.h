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
    uint8_t  page_bitmap[(MEM_TOTAL_PAGES + 7) / 8];
    int      total_pages;
    int      free_pages;

    
    PCB      proc_table[PROC_MAX_COUNT];
    int      next_pid;
    int      current_idx;       

    
    int      ready_indices[SCHED_QUEUE_SIZE]; 
    int      ready_head;
    int      ready_tail;
    int      ready_count;
    int      sched_inited;

    
    Pipe     pipes[PIPE_MAX_COUNT];
    int      pipe_used[PIPE_MAX_COUNT];

    
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
