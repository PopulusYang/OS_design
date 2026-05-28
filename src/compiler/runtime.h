// runtime.h — C 运行时库函数原型声明
//
// 这些函数在 runtime.s 中实现，由编译器自动附加到编译输出中。

#ifndef COMPILER_RUNTIME_H
#define COMPILER_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

// 比较辅助（返回 0 或 1）
int __rt_lt(int a, int b);
int __rt_gt(int a, int b);
int __rt_le(int a, int b);
int __rt_ge(int a, int b);

// 移位辅助
int __rt_lshift(int value, int shift);
int __rt_rshift(int value, int shift);

// I/O
void __rt_print_int(int value);

// 内存操作
void *__rt_memcpy(void *dst, const void *src, int count);
void *__rt_memset(void *dst, int value, int count);

#ifdef __cplusplus
}
#endif

#endif // COMPILER_RUNTIME_H
