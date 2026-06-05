/*
 * scheduler.h
 * 时间片轮转调度器的就绪队列接口。
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "kernel/process.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化就绪队列环形缓冲区
int  sched_init(void);

// 把进程放入就绪队列尾部
void sched_enqueue(PCB *p);

// 从就绪队列头部取下一个进程
PCB *sched_pick_next(void);

// 按 PID 从就绪队列中移除进程
void sched_remove(uint32_t pid);

// 调度一次：运行当前时间片后切换到下一进程
int  sched_tick(void);

// 返回就绪队列中的进程数
int  sched_ready_count(void);

// 主动让出 CPU，运行其他就绪进程
void sched_cooperate(void);

#ifdef __cplusplus
}
#endif

#endif
