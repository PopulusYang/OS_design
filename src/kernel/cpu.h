// cpu.h —— 32-bit RISC VM 执行引擎
//
// 定长 32-bit 指令：[opcode:8][rd:4][rs1:4][rs2:4][imm12:12]
// 16 个通用寄存器 (R0–R15, R15=SP)，PC，FLAGS(ZF)
// 每次 cpu_step() 执行一条指令；返回 0 正常，1 表示 HALT/SYSCALL

#ifndef CPU_H
#define CPU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CPU_NUM_REGS     16
#define CPU_REG_SP       15
#define CPU_TIMESLICE    100       // 时间片指令数

// 指令操作码
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
    OP_LUI     = 0x15,   // R[rd] = imm12 << 12 (加载高 20 位地址)
};

// 指令编码宏
#define CPU_ENCODE(op, rd, rs1, rs2, imm) \
    (((uint32_t)(op) << 24) | (((uint32_t)(rd) & 0xF) << 20) | \
     (((uint32_t)(rs1) & 0xF) << 16) | (((uint32_t)(rs2) & 0xF) << 12) | \
     ((uint32_t)(imm) & 0xFFF))

#define CPU_OPCODE(i)    (((i) >> 24) & 0xFF)
#define CPU_RD(i)        (((i) >> 20) & 0xF)
#define CPU_RS1(i)       (((i) >> 16) & 0xF)
#define CPU_RS2(i)       (((i) >> 12) & 0xF)
#define CPU_IMM12(i)     ((int32_t)(((i) & 0xFFF) << 20) >> 20) // 符号扩展

// FLAGS 位
#define CPU_FLAG_ZF      0x01

// 系统调用编号（R0 传递）
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
#define SYSCALL_HOST_EDIT  20   // 调用宿主编编辑器
#define SYSCALL_HOST_ASM   21   // 调用宿主汇编器

// VM 执行上下文
// 始终嵌入在 PCB 中，通过 container_of 可反推 PCB 指针
typedef struct CPUContext {
    uint32_t regs[CPU_NUM_REGS];  // R0–R15
    uint32_t pc;                   // 程序计数器（指令索引，非字节偏移）
    uint32_t flags;                // ZF 等
    uint32_t  ticks_left;          // 本时间片剩余指令数
    // 回传信息
    int       sycall_halt;         // 0=正常, 1=HALT, 2=SYSCALL
    uint32_t  syscall_no;          // 触发的系统调用编号
} CPUContext;

// 初始化 VM 上下文
void cpu_init(CPUContext *ctx, uint32_t entry_pc, uint32_t stack_top);

// 执行一条指令，返回 0 继续，1 应调度（HALT/SYSCALL/时间片耗尽）
int  cpu_step(CPUContext *ctx);

// 虚拟地址 → 物理地址（通过页表翻译）
// 成功返回 0，out_phys 为物理地址；失败返回 -1（缺页/越界）
int  cpu_virt_to_phys(const CPUContext *ctx, uint32_t virt_addr, uint32_t *out_phys);

// 从虚拟地址读/写（自动处理跨页）
uint32_t cpu_read32(const CPUContext *ctx, uint32_t virt_addr);
void     cpu_write32(CPUContext *ctx, uint32_t virt_addr, uint32_t val);
uint8_t  cpu_read8(const CPUContext *ctx, uint32_t virt_addr);
void     cpu_write8(CPUContext *ctx, uint32_t virt_addr, uint8_t val);

// 分配一个新的用户页框并映射到虚拟地址（用于 sbrk / 栈扩展）
int  cpu_map_page(CPUContext *ctx, uint32_t virt_addr);

#ifdef __cplusplus
}
#endif

#endif // CPU_H
