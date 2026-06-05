/*
 * cpu.h
 * 32 位 RISC 虚拟机的寄存器、标志位与执行接口。
 */
#ifndef CPU_H
#define CPU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CPU_NUM_REGS     16
#define CPU_REG_SP       15
#define CPU_TIMESLICE    100

enum {
    OP_HALT    = 0x00,
    OP_MOVI    = 0x01,
    OP_MOV     = 0x02,
    OP_LD      = 0x03,
    OP_ST      = 0x04,
    OP_ADD     = 0x05,
    OP_SUB     = 0x06,
    OP_MUL     = 0x07,
    OP_DIV     = 0x08,
    OP_AND     = 0x09,
    OP_OR      = 0x0A,
    OP_XOR     = 0x0B,
    OP_CMP     = 0x0C,
    OP_JMP     = 0x0D,
    OP_JZ      = 0x0E,
    OP_JNZ     = 0x0F,
    OP_CALL    = 0x10,
    OP_RET     = 0x11,
    OP_PUSH    = 0x12,
    OP_POP     = 0x13,
    OP_SYSCALL = 0x14,
    OP_LUI     = 0x15,
};

#define CPU_ENCODE(op, rd, rs1, rs2, imm) \
    (((uint32_t)(op) << 24) | (((uint32_t)(rd) & 0xF) << 20) | \
     (((uint32_t)(rs1) & 0xF) << 16) | (((uint32_t)(rs2) & 0xF) << 12) | \
     ((uint32_t)(imm) & 0xFFF))

#define CPU_OPCODE(i)    (((i) >> 24) & 0xFF)
#define CPU_RD(i)        (((i) >> 20) & 0xF)
#define CPU_RS1(i)       (((i) >> 16) & 0xF)
#define CPU_RS2(i)       (((i) >> 12) & 0xF)
#define CPU_IMM12(i)     ((int32_t)(((i) & 0xFFF) << 20) >> 20)

#define CPU_FLAG_ZF      0x01

#define SYSCALL_EXIT      0
#define SYSCALL_FORK      1
#define SYSCALL_EXEC      2
#define SYSCALL_WAIT      3
#define SYSCALL_GETPID    4
#define SYSCALL_OPEN      5
#define SYSCALL_CLOSE     6
#define SYSCALL_READ      7
#define SYSCALL_WRITE     8
#define SYSCALL_SEEK      9
#define SYSCALL_GETCWD    10
#define SYSCALL_CHDIR     11
#define SYSCALL_SBRK      12
#define SYSCALL_GETENV    13
#define SYSCALL_SETENV    14
#define SYSCALL_UNSETENV  15
#define SYSCALL_STAT      16
#define SYSCALL_CREATE    17
#define SYSCALL_DELETE    18
#define SYSCALL_MKDIR     19
#define SYSCALL_HOST_EDIT  20
#define SYSCALL_HOST_ASM   21
#define SYSCALL_PIPE       22
#define SYSCALL_KILL       23
#define SYSCALL_SEMGET     24
#define SYSCALL_SEMOP      25
#define SYSCALL_MSGGET     26
#define SYSCALL_MSGSND     27
#define SYSCALL_MSGRCV     28
#define SYSCALL_SHMGET     29
#define SYSCALL_SHMAT      30
#define SYSCALL_SHMDT      31
#define SYSCALL_MKFIFO     32
#define SYSCALL_GETSIG     33

typedef struct CPUContext {
    uint32_t regs[CPU_NUM_REGS];
    uint32_t pc;
    uint32_t flags;
    uint32_t  ticks_left;

    int       sycall_halt;
    uint32_t  syscall_no;
} CPUContext;

// 初始化 CPU 上下文：入口 PC、栈顶与时间片计数
void cpu_init(CPUContext *ctx, uint32_t entry_pc, uint32_t stack_top);

// 执行一条指令，遇系统调用或时间片耗尽时返回 1
int  cpu_step(CPUContext *ctx);

// 把进程虚拟地址换算成物理字节地址
int  cpu_virt_to_phys(const CPUContext *ctx, uint32_t virt_addr, uint32_t *out_phys);

// 从虚拟地址读 32 位整数
uint32_t cpu_read32(const CPUContext *ctx, uint32_t virt_addr);
// 向虚拟地址写 32 位整数
void     cpu_write32(CPUContext *ctx, uint32_t virt_addr, uint32_t val);
// 从虚拟地址读 8 位字节
uint8_t  cpu_read8(const CPUContext *ctx, uint32_t virt_addr);
// 向虚拟地址写 8 位字节
void     cpu_write8(CPUContext *ctx, uint32_t virt_addr, uint8_t val);

// 为虚拟页分配物理页并写入页表
int  cpu_map_page(CPUContext *ctx, uint32_t virt_addr);

#ifdef __cplusplus
}
#endif

#endif
