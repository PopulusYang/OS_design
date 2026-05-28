

#include "kernel/scheduler.h"
#include "kernel/syscall.h"
#include "kernel/ipc.h"
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

static int sched_run_slice(PCB *p)
{
    ipc_deliver_signals(p);
    if (p->p_state == PROC_ZOMBIE) return 1;

    while (p->p_cpu.ticks_left > 0 && p->p_state == PROC_RUNNING) {
        int rc = cpu_step(&p->p_cpu);
        if (rc != 1) continue;
        if (p->p_cpu.sycall_halt == 2) {
            syscall_dispatch(p, p->p_cpu.syscall_no);
            p->p_cpu.sycall_halt = 0;
            ipc_deliver_signals(p);
            if (p->p_state == PROC_ZOMBIE) return 1;
        } else if (p->p_cpu.sycall_halt == 1) {
            proc_exit(0);
            return 1;
        }
        if (p->p_cpu.ticks_left > 0) continue;
    }
    return 0;
}

void sched_cooperate(void)
{
    PCB *self = proc_current();
    if (self == NULL || g_kernel == NULL) return;

    for (int guard = 0; guard < 100000; guard++) {
        if (sched_ready_count() == 0) return;
        PCB *next = sched_pick_next();
        if (next == NULL) return;
        if (next == self) {
            sched_enqueue(self);
            return;
        }
        proc_set_current(next);
        next->p_cpu.ticks_left = CPU_TIMESLICE;
        sched_run_slice(next);
        if (next->p_state == PROC_RUNNING && next->p_pid != 0)
            sched_enqueue(next);
        proc_set_current(self);
    }
}

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
        if (sched_run_slice(cur)) break;
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
