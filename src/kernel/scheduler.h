// scheduler.h —— 时间片轮转调度器

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "kernel/process.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化调度器
int  sched_init(void);

// 将进程加入就绪队列
void sched_enqueue(PCB *p);

// 调度：选择下一个就绪进程，设置为当前进程
// 返回新当前进程的 PCB
PCB *sched_pick_next(void);

// 从就绪队列移除指定 PID
void sched_remove(uint32_t pid);

// 运行调度循环：持续执行就绪进程直到全部退出
// 每次调用最多执行一个时间片的指令，返回 0 表示还有进程，1 表示全部完成
int  sched_tick(void);

// 获取就绪队列中的进程数
int  sched_ready_count(void);

// 在系统调用阻塞时运行其他就绪进程（管道读写等）
void sched_cooperate(void);

#ifdef __cplusplus
}
#endif

#endif // SCHEDULER_H
