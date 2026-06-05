/*
 * regalloc.h
 * 代码生成阶段的临时寄存器分配，可用范围为 R1 到 R8。
 */
#ifndef COMPILER_REGALLOC_H
#define COMPILER_REGALLOC_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REG_ALLOC_MIN  1
#define REG_ALLOC_MAX  8

typedef struct {
    bool    used[16];
    int     spill_offset;
} RegAlloc;

void regalloc_init(RegAlloc *ra);
int  regalloc_alloc(RegAlloc *ra);
void regalloc_free(RegAlloc *ra, int reg);
int  regalloc_count_used(RegAlloc *ra);
int  regalloc_spill_offset(RegAlloc *ra);

#ifdef __cplusplus
}
#endif

#endif
