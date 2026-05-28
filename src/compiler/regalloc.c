// regalloc.c —— 简单线性寄存器分配器实现

#include "compiler/regalloc.h"
#include <string.h>
#include <stdio.h>

void regalloc_init(RegAlloc *ra) {
    memset(ra, 0, sizeof(*ra));
    // 标记不可分配的寄存器
    ra->used[0] = true;   // R0 = 返回值
    ra->used[14] = true;  // R14 = FP
    ra->used[15] = true;  // R15 = SP
    ra->spill_offset = 4; // 溢出栈槽从 FP-4 开始
}

int regalloc_alloc(RegAlloc *ra) {
    // 先找空闲寄存器
    for (int r = REG_ALLOC_MIN; r <= REG_ALLOC_MAX; r++) {
        if (!ra->used[r]) {
            ra->used[r] = true;
            return r;
        }
    }
    // 所有寄存器都忙，溢出最小编号的（简单策略）
    // 注意：在实际使用中，caller 应该在分配前释放不需要的寄存器
    // 这里作为安全回退
    for (int r = REG_ALLOC_MIN; r <= REG_ALLOC_MAX; r++) {
        if (ra->used[r]) {
            ra->spill_offset += 4;
            return r; // 返回已使用的寄存器，caller 需处理溢出
        }
    }
    return -1; // 不应该到达这里
}

void regalloc_free(RegAlloc *ra, int reg) {
    if (reg >= 0 && reg < 16) {
        ra->used[reg] = false;
    }
}

int regalloc_count_used(RegAlloc *ra) {
    int count = 0;
    for (int r = REG_ALLOC_MIN; r <= REG_ALLOC_MAX; r++) {
        if (ra->used[r]) count++;
    }
    return count;
}

int regalloc_spill_offset(RegAlloc *ra) {
    int offset = ra->spill_offset;
    ra->spill_offset += 4;
    return offset;
}
