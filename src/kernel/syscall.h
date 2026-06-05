/*
 * syscall.h
 * 系统调用分发与进程地址空间字符串读写。
 */
#ifndef SYSCALL_H
#define SYSCALL_H

#include "kernel/process.h"

#ifdef __cplusplus
extern "C" {
#endif

// 根据系统调用号分发到具体处理逻辑
void syscall_dispatch(PCB *p, uint32_t syscall_no);

// 从进程虚拟地址读取以 \0 结尾的字符串
int  syscall_read_str(const PCB *p, uint32_t virt_addr, char *buf, int max_len);

// 把字符串写入进程虚拟地址
int  syscall_write_str(PCB *p, uint32_t virt_addr, const char *str, int max_len);

#ifdef __cplusplus
}
#endif

#endif
