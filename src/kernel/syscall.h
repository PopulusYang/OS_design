

#ifndef SYSCALL_H
#define SYSCALL_H

#include "kernel/process.h"

#ifdef __cplusplus
extern "C" {
#endif




void syscall_dispatch(PCB *p, uint32_t syscall_no);


int  syscall_read_str(const PCB *p, uint32_t virt_addr, char *buf, int max_len);


int  syscall_write_str(PCB *p, uint32_t virt_addr, const char *str, int max_len);

#ifdef __cplusplus
}
#endif

#endif 
