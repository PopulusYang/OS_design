// scheduler.c —— 时间片轮转调度器实现（支持共享/本地模式）

#include "kernel/scheduler.h"
#include "kernel/syscall.h"
#include "kernel_shared.h"
#include <string.h>
#include <stdio.h>

extern KernelShared *g_kernel;

int sched_init(void)
{
    if (g_kernel == NULL) return -1;
    if (g_kernel->sched_inited) return 0;
    memset(g_kernel->ready_indices, 0, sizeof(g_kernel->ready_indices));
    g_kernel->ready_head = g_kernel->ready_tail = g_kernel->ready_count = 0;
    g_kernel->sched_inited = 1;
    return 0;
}

void sched_enqueue(PCB *p)
{
    if (g_kernel == NULL || p == NULL || g_kernel->ready_count >= (PROC_MAX_COUNT + 1)) return;
    int idx = (int)(p - g_kernel->proc_table);
    g_kernel->ready_indices[g_kernel->ready_tail] = idx;
    g_kernel->ready_tail = (g_kernel->ready_tail + 1) % (PROC_MAX_COUNT + 1);
    g_kernel->ready_count++;
    if (p->p_state != PROC_RUNNING) p->p_state = PROC_READY;
}

PCB *sched_pick_next(void)
{
    if (g_kernel == NULL || g_kernel->ready_count == 0) return NULL;
    int idx = g_kernel->ready_indices[g_kernel->ready_head];
    g_kernel->ready_indices[g_kernel->ready_head] = -1;
    g_kernel->ready_head = (g_kernel->ready_head + 1) % (PROC_MAX_COUNT + 1);
    g_kernel->ready_count--;
    PCB *p = &g_kernel->proc_table[idx];
    if (p->p_state != PROC_FREE) p->p_state = PROC_RUNNING;
    return p;
}

void sched_remove(uint32_t pid)
{
    if (g_kernel == NULL) return;
    for (int i = 0; i < (PROC_MAX_COUNT + 1); i++) {
        int idx = g_kernel->ready_indices[i];
        if (idx >= 0 && idx < PROC_MAX_COUNT &&
            g_kernel->proc_table[idx].p_pid == pid) {
            g_kernel->ready_indices[i] = -1;
            g_kernel->ready_count--;
            return;
        }
    }
}

int sched_ready_count(void) { return g_kernel ? g_kernel->ready_count : 0; }

int sched_tick(void)
{
    if (g_kernel == NULL || !g_kernel->sched_inited) return 1;

    PCB *cur = proc_current();
    if (cur == NULL || cur->p_state == PROC_ZOMBIE || cur->p_state == PROC_FREE) {
        cur = sched_pick_next();
        if (cur == NULL) return 1;
        proc_set_current(cur);
        cur->p_cpu.ticks_left = CPU_TIMESLICE;
    }

    while (cur->p_cpu.ticks_left > 0) {
        int rc = cpu_step(&cur->p_cpu);
        if (rc == 1) {
            if (cur->p_cpu.sycall_halt == 2) {
                syscall_dispatch(cur, cur->p_cpu.syscall_no);
                cur->p_cpu.sycall_halt = 0;
                if (cur->p_state == PROC_ZOMBIE) break;
            } else if (cur->p_cpu.sycall_halt == 1) {
                proc_exit(0);
                break;
            }
            if (cur->p_cpu.ticks_left > 0) continue;
        }
    }

    if (cur->p_state == PROC_RUNNING && cur->p_pid != 0) {
        cur->p_cpu.ticks_left = CPU_TIMESLICE;
        sched_enqueue(cur);
    }

    PCB *next = sched_pick_next();
    if (next == NULL) {
        if (proc_count() <= 1) return 1;
        return 1;
    }
    proc_set_current(next);
    next->p_cpu.ticks_left = CPU_TIMESLICE;
    return 0;
}
