/*
 * regalloc.c
 * 线性扫描式寄存器池：在 R1 到 R8 之间分配和回收临时寄存器。
 */
#include "compiler/regalloc.h"
#include <string.h>
#include <stdio.h>

void regalloc_init(RegAlloc *ra) {
    memset(ra, 0, sizeof(*ra));
    ra->used[0] = true;
    ra->used[14] = true;
    ra->used[15] = true;
    ra->spill_offset = 4;
}

// 从 R1 到 R8 找空闲寄存器，全满时退回已占用的并推进溢出偏移
int regalloc_alloc(RegAlloc *ra) {
    for (int r = REG_ALLOC_MIN; r <= REG_ALLOC_MAX; r++) {
        if (!ra->used[r]) {
            ra->used[r] = true;
            return r;
        }
    }
    for (int r = REG_ALLOC_MIN; r <= REG_ALLOC_MAX; r++) {
        if (ra->used[r]) {
            ra->spill_offset += 4;
            return r;
        }
    }
    return -1;
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
