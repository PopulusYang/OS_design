/*
 * runtime.h
 * C 编译器运行时辅助函数声明，实现在 runtime.s 中。
 */
#ifndef COMPILER_RUNTIME_H
#define COMPILER_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

int __rt_lt(int a, int b);
int __rt_gt(int a, int b);
int __rt_le(int a, int b);
int __rt_ge(int a, int b);

int __rt_lshift(int value, int shift);
int __rt_rshift(int value, int shift);

void __rt_print_int(int value);

void *__rt_memcpy(void *dst, const void *src, int count);
void *__rt_memset(void *dst, int value, int count);

#ifdef __cplusplus
}
#endif

#endif
