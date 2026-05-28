#include "kernel/cpu.h"
#include "kernel/process.h"
#include "kernel/memory.h"
#include <string.h>
#include <stdio.h>

#define VIRT_PAGE_BITS    12
#define VIRT_PAGE_SIZE    4096U
#define VIRT_PAGE_MASK    (VIRT_PAGE_SIZE - 1)
#define VIRT_PAGE_SHIFT   VIRT_PAGE_BITS

#define CPU_TO_PCB(ctx) \
    ((PCB *)((char *)(ctx) - offsetof(PCB, p_cpu)))

void cpu_init(CPUContext *ctx, uint32_t entry_pc, uint32_t stack_top)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->pc = entry_pc;
    ctx->regs[CPU_REG_SP] = stack_top;
    ctx->ticks_left = CPU_TIMESLICE;
    ctx->sycall_halt = 0;
}

int cpu_virt_to_phys(const CPUContext *ctx, uint32_t virt_addr, uint32_t *out_phys)
{
    if (ctx == NULL || out_phys == NULL) return -1;
    uint32_t page_idx = virt_addr >> VIRT_PAGE_SHIFT;
    uint32_t offset = virt_addr & VIRT_PAGE_MASK;
    if (page_idx >= MEM_MAX_PROCESS_PAGES) return -1;
    PCB *p = CPU_TO_PCB(ctx);
    uint32_t phys_page = p->p_page_table[page_idx];
    if (phys_page == 0) return -1;
    *out_phys = (phys_page * VIRT_PAGE_SIZE) + offset;
    return 0;
}

uint32_t cpu_read32(const CPUContext *ctx, uint32_t virt_addr)
{
    uint32_t phys;
    if (cpu_virt_to_phys(ctx, virt_addr, &phys) != 0) return 0;
    return mem_read32(phys);
}

void cpu_write32(CPUContext *ctx, uint32_t virt_addr, uint32_t val)
{
    uint32_t phys;
    if (cpu_virt_to_phys(ctx, virt_addr, &phys) != 0) return;
    mem_write32(phys, val);
}

uint8_t cpu_read8(const CPUContext *ctx, uint32_t virt_addr)
{
    uint32_t phys;
    if (cpu_virt_to_phys(ctx, virt_addr, &phys) != 0) return 0;
    return mem_read8(phys);
}

void cpu_write8(CPUContext *ctx, uint32_t virt_addr, uint8_t val)
{
    uint32_t phys;
    if (cpu_virt_to_phys(ctx, virt_addr, &phys) != 0) return;
    mem_write8(phys, val);
}

int cpu_map_page(CPUContext *ctx, uint32_t virt_addr)
{
    if (ctx == NULL) return -1;
    uint32_t page_idx = virt_addr >> VIRT_PAGE_SHIFT;
    if (page_idx >= MEM_MAX_PROCESS_PAGES) return -1;
    PCB *p = CPU_TO_PCB(ctx);
    if (p->p_page_table[page_idx] != 0) return 0;
    int phys_page = mem_alloc_pages(1);
    if (phys_page < 0) return -1;
    p->p_page_table[page_idx] = (uint32_t)phys_page;
    return 0;
}

int cpu_step(CPUContext *ctx)
{
    if (ctx == NULL) return 1;
    ctx->ticks_left--;

    uint32_t inst = cpu_read32(ctx, ctx->pc * 4);
    uint32_t op = CPU_OPCODE(inst);
    uint32_t rd = CPU_RD(inst);
    uint32_t rs1 = CPU_RS1(inst);
    uint32_t rs2 = CPU_RS2(inst);
    int32_t  imm = CPU_IMM12(inst);

    uint32_t *r = ctx->regs;

    switch (op) {
    case OP_HALT:
        ctx->sycall_halt = 1;
        ctx->syscall_no = SYSCALL_EXIT;
        return 1;

    case OP_MOVI:
        if (rd < CPU_NUM_REGS) r[rd] = (uint32_t)(int32_t)imm;
        ctx->pc++;
        break;

    case OP_MOV:
        if (rd < CPU_NUM_REGS && rs1 < CPU_NUM_REGS) r[rd] = r[rs1];
        ctx->pc++;
        break;

    case OP_LD:
        if (rd < CPU_NUM_REGS && rs1 < CPU_NUM_REGS) {
            uint32_t addr = r[rs1] + (uint32_t)imm;
            r[rd] = cpu_read32(ctx, addr);
        }
        ctx->pc++;
        break;

    case OP_ST:
        if (rs1 < CPU_NUM_REGS && rs2 < CPU_NUM_REGS) {
            uint32_t addr = r[rs1] + (uint32_t)imm;
            cpu_write32(ctx, addr, r[rs2]);
        }
        ctx->pc++;
        break;

    case OP_ADD:
        if (rd < CPU_NUM_REGS && rs1 < CPU_NUM_REGS && rs2 < CPU_NUM_REGS)
            r[rd] = r[rs1] + r[rs2];
        ctx->pc++;
        break;

    case OP_SUB:
        if (rd < CPU_NUM_REGS && rs1 < CPU_NUM_REGS && rs2 < CPU_NUM_REGS)
            r[rd] = r[rs1] - r[rs2];
        ctx->pc++;
        break;

    case OP_MUL:
        if (rd < CPU_NUM_REGS && rs1 < CPU_NUM_REGS && rs2 < CPU_NUM_REGS)
            r[rd] = r[rs1] * r[rs2];
        ctx->pc++;
        break;

    case OP_DIV:
        if (rd < CPU_NUM_REGS && rs1 < CPU_NUM_REGS && rs2 < CPU_NUM_REGS && r[rs2] != 0)
            r[rd] = r[rs1] / r[rs2];
        ctx->pc++;
        break;

    case OP_AND:
        if (rd < CPU_NUM_REGS && rs1 < CPU_NUM_REGS && rs2 < CPU_NUM_REGS)
            r[rd] = r[rs1] & r[rs2];
        ctx->pc++;
        break;

    case OP_OR:
        if (rd < CPU_NUM_REGS && rs1 < CPU_NUM_REGS && rs2 < CPU_NUM_REGS)
            r[rd] = r[rs1] | r[rs2];
        ctx->pc++;
        break;

    case OP_XOR:
        if (rd < CPU_NUM_REGS && rs1 < CPU_NUM_REGS && rs2 < CPU_NUM_REGS)
            r[rd] = r[rs1] ^ r[rs2];
        ctx->pc++;
        break;

    case OP_CMP:
        if (rs1 < CPU_NUM_REGS && rs2 < CPU_NUM_REGS) {
            ctx->flags = (r[rs1] == r[rs2]) ? CPU_FLAG_ZF : 0;
        }
        ctx->pc++;
        break;

    case OP_JMP:
        ctx->pc = (uint32_t)((int32_t)ctx->pc + imm);
        break;

    case OP_JZ:
        if (ctx->flags & CPU_FLAG_ZF)
            ctx->pc = (uint32_t)((int32_t)ctx->pc + imm);
        else
            ctx->pc++;
        break;

    case OP_JNZ:
        if (!(ctx->flags & CPU_FLAG_ZF))
            ctx->pc = (uint32_t)((int32_t)ctx->pc + imm);
        else
            ctx->pc++;
        break;

    case OP_CALL:
        r[CPU_REG_SP] -= 4;
        cpu_write32(ctx, r[CPU_REG_SP], ctx->pc + 1);
        ctx->pc = (uint32_t)((int32_t)ctx->pc + imm);
        break;

    case OP_RET:
        ctx->pc = cpu_read32(ctx, r[CPU_REG_SP]);
        r[CPU_REG_SP] += 4;
        break;

    case OP_PUSH:
        if (rs1 < CPU_NUM_REGS) {
            r[CPU_REG_SP] -= 4;
            cpu_write32(ctx, r[CPU_REG_SP], r[rs1]);
        }
        ctx->pc++;
        break;

    case OP_POP:
        if (rd < CPU_NUM_REGS) {
            r[rd] = cpu_read32(ctx, r[CPU_REG_SP]);
            r[CPU_REG_SP] += 4;
        }
        ctx->pc++;
        break;

    case OP_SYSCALL:
        ctx->sycall_halt = 2;
        ctx->syscall_no = (uint32_t)imm;
        ctx->pc++;
        return 1;

    case OP_LUI:
        if (rd < CPU_NUM_REGS) r[rd] = ((uint32_t)(imm & 0xFFF)) << 12;
        ctx->pc++;
        break;

    default:
        ctx->pc++;
        break;
    }

    if (ctx->ticks_left == 0) {
        return 1;
    }
    return 0;
}
