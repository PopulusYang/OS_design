// syscall.h —— 系统调用接口

#ifndef SYSCALL_H
#define SYSCALL_H

#include "kernel/process.h"

#ifdef __cplusplus
extern "C" {
#endif

// 分发系统调用
// 参数：R1=arg1, R2=arg2, R3=arg3
// 返回值放入 R0
void syscall_dispatch(PCB *p, uint32_t syscall_no);

// 字符串辅助：从进程虚拟地址空间读取 C 字符串到内核缓冲区
int  syscall_read_str(const PCB *p, uint32_t virt_addr, char *buf, int max_len);

// 字符串辅助：将 C 字符串写入进程虚拟地址空间
int  syscall_write_str(PCB *p, uint32_t virt_addr, const char *str, int max_len);

#ifdef __cplusplus
}
#endif

#endif // SYSCALL_H
