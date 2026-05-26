// scheduler.c —— 时间片轮转调度器实现

#include "kernel/scheduler.h"
#include "kernel/syscall.h"
#include <string.h>
#include <stdio.h>

#define SCHED_QUEUE_SIZE  (PROC_MAX_COUNT + 1)

static PCB  *g_ready_queue[SCHED_QUEUE_SIZE];
static int   g_ready_head = 0;  // 出队位置
static int   g_ready_tail = 0;  // 入队位置
static int   g_ready_count = 0;
static int   g_sched_inited = 0;

int sched_init(void)
{
    memset(g_ready_queue, 0, sizeof(g_ready_queue));
    g_ready_head = g_ready_tail = g_ready_count = 0;
    g_sched_inited = 1;
    return 0;
}

void sched_enqueue(PCB *p)
{
    if (p == NULL || g_ready_count >= SCHED_QUEUE_SIZE) return;
    g_ready_queue[g_ready_tail] = p;
    g_ready_tail = (g_ready_tail + 1) % SCHED_QUEUE_SIZE;
    g_ready_count++;
    if (p->p_state != PROC_RUNNING) p->p_state = PROC_READY;
}

PCB *sched_pick_next(void)
{
    if (g_ready_count == 0) return NULL;
    PCB *p = g_ready_queue[g_ready_head];
    g_ready_queue[g_ready_head] = NULL;
    g_ready_head = (g_ready_head + 1) % SCHED_QUEUE_SIZE;
    g_ready_count--;
    if (p) p->p_state = PROC_RUNNING;
    return p;
}

void sched_remove(uint32_t pid)
{
    for (int i = 0; i < SCHED_QUEUE_SIZE; i++) {
        if (g_ready_queue[i] && g_ready_queue[i]->p_pid == pid) {
            g_ready_queue[i] = NULL;
            g_ready_count--;
            return;
        }
    }
}

int sched_ready_count(void) { return g_ready_count; }

int sched_tick(void)
{
    if (!g_sched_inited) return 1;

    // 如果无当前进程，从就绪队列取
    PCB *cur = proc_current();
    if (cur == NULL || cur->p_state == PROC_ZOMBIE || cur->p_state == PROC_FREE) {
        cur = sched_pick_next();
        if (cur == NULL) return 1;
        proc_set_current(cur);
        cur->p_cpu.ticks_left = CPU_TIMESLICE;
    }

    // 执行直到时间片耗尽 / SYSCALL / HALT
    while (cur->p_cpu.ticks_left > 0) {
        int rc = cpu_step(&cur->p_cpu);
        if (rc == 1) {
            // HALT 或 SYSCALL
            if (cur->p_cpu.sycall_halt == 2) {
                // SYSCALL
                syscall_dispatch(cur, cur->p_cpu.syscall_no);
                cur->p_cpu.sycall_halt = 0;
                // SYSCALL 可能改变状态（如 exit），检查
                if (cur->p_state == PROC_ZOMBIE) break;
            } else if (cur->p_cpu.sycall_halt == 1) {
                // HALT → exit(0)
                proc_exit(0);
                break;
            }
            // 时间片耗尽 → 继续下一轮
            if (cur->p_cpu.ticks_left > 0) continue;
        }
    }

    // 当前进程时间片用完或已退出，放回就绪队列（init 除外）
    if (cur->p_state == PROC_RUNNING && cur->p_pid != 0) {
        cur->p_cpu.ticks_left = CPU_TIMESLICE;
        sched_enqueue(cur);
    }

    // 切换
    PCB *next = sched_pick_next();
    if (next == NULL) {
        // 无就绪进程，检查是否全部完成
        if (proc_count() <= 1) return 1; // 只剩 init
        return 1;
    }
    proc_set_current(next);
    next->p_cpu.ticks_left = CPU_TIMESLICE;
    return 0;
}
