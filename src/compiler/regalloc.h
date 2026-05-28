// regalloc.h —— 寄存器分配器接口

#ifndef COMPILER_REGALLOC_H
#define COMPILER_REGALLOC_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 可用临时寄存器: R1-R8（R0=返回值, R9-R13=callee-saved, R14=FP, R15=SP）
#define REG_ALLOC_MIN  1
#define REG_ALLOC_MAX  8

typedef struct {
    bool    used[16];         // 寄存器使用标记
    int     spill_offset;     // 下一个溢出栈槽偏移（相对于 FP，负方向）
} RegAlloc;

// 初始化寄存器分配器
void regalloc_init(RegAlloc *ra);

// 分配一个寄存器，返回寄存器编号 (R1-R8)
// 如果没有空闲寄存器，溢出最久未使用的
int  regalloc_alloc(RegAlloc *ra);

// 释放寄存器
void regalloc_free(RegAlloc *ra, int reg);

// 获取当前使用的寄存器数
int  regalloc_count_used(RegAlloc *ra);

// 生成溢出加载/存储指令到 fp 的偏移
int  regalloc_spill_offset(RegAlloc *ra);

#ifdef __cplusplus
}
#endif

#endif // COMPILER_REGALLOC_H
