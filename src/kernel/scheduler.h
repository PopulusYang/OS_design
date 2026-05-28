

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "kernel/process.h"

#ifdef __cplusplus
extern "C" {
#endif


int  sched_init(void);


void sched_enqueue(PCB *p);



PCB *sched_pick_next(void);


void sched_remove(uint32_t pid);



int  sched_tick(void);


int  sched_ready_count(void);


void sched_cooperate(void);

#ifdef __cplusplus
}
#endif

#endif 
