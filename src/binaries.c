// binaries.c —— 预置演示程序 .upx 二进制数据

#include "binaries.h"
#include "kernel/cpu.h"
#include "kernel/process.h"
#include <string.h>
#include <stdio.h>

// 将 uint32 以小端序写入 buf
static void w32(uint8_t *buf, uint32_t val)
{
    buf[0] = (uint8_t)val;
    buf[1] = (uint8_t)(val >> 8);
    buf[2] = (uint8_t)(val >> 16);
    buf[3] = (uint8_t)(val >> 24);
}

// 构建一个完整的 .upx 二进制
static int build_upx(uint8_t *out, size_t out_size,
                     const uint32_t *text, int text_words,
                     const uint8_t *data, uint32_t data_len,
                     uint32_t bss_len, uint32_t stack_len)
{
    size_t header_sz = sizeof(UPXHeader);       // 24
    size_t text_sz   = (size_t)text_words * 4;
    size_t total     = header_sz + text_sz + data_len;

    if (total > out_size) return -1;

    // header
    memcpy(out, "UPX\0", 4);
    w32(out + 4, 0);                            // entry = 0
    w32(out + 8, (uint32_t)text_sz);
    w32(out + 12, data_len);
    w32(out + 16, bss_len);
    w32(out + 20, stack_len);

    // text
    uint8_t *tp = out + header_sz;
    for (int i = 0; i < text_words; i++)
        w32(tp + i * 4, text[i]);

    // data
    if (data_len > 0) memcpy(out + header_sz + text_sz, data, data_len);

    return (int)total;
}

// ===== hello: "Hello, World!\n" =====
// LUI R2,1→page 1; ADD R2,R2,R0→offset 0 (data at page 1 offset 0)
// MOVI R1,1; MOVI R3,14; SYSCALL 8; SYSCALL 0

static const uint8_t hello_data[] = "Hello, World!\n";

static uint32_t hello_text[] = {
    CPU_ENCODE(OP_LUI, 2, 0, 0, 1),             // R2 = 0x1000 (page 1)
    CPU_ENCODE(OP_MOVI, 1, 0, 0, 1),            // R1 = 1 (stdout)
    CPU_ENCODE(OP_MOVI, 3, 0, 0, 14),           // R3 = 14
    CPU_ENCODE(OP_SYSCALL, 0, 0, 0, 8),         // write(1, R2, 14)
    CPU_ENCODE(OP_MOVI, 1, 0, 0, 0),            // R1 = 0
    CPU_ENCODE(OP_SYSCALL, 0, 0, 0, 0),         // exit(0)
};

// ===== counter: 输出 0-9 数字 =====
// R4=counter. Loop: convert digit, store to buf, write, inc, cmp, jnz

static uint32_t counter_text[] = {
    CPU_ENCODE(OP_MOVI, 4, 0, 0, 0),            // R4 = 0
    CPU_ENCODE(OP_LUI,  6, 0, 0, 1),            // R6 = page1 (data buffer)
    // loop_start (PC=2):
    CPU_ENCODE(OP_MOVI, 5, 0, 0, 0x30),         // R5 = '0'
    CPU_ENCODE(OP_ADD,  5, 5, 4, 0),            // R5 = '0' + counter
    CPU_ENCODE(OP_ST,   0, 6, 5, 0),            // [R6] = digit
    CPU_ENCODE(OP_MOVI, 1, 0, 0, 1),            // R1 = 1 (stdout)
    CPU_ENCODE(OP_MOVI, 2, 0, 0, 0),            // R2 = R6
    CPU_ENCODE(OP_ADD,  2, 6, 0, 0),            // R2 = R6 (buf addr)
    CPU_ENCODE(OP_MOVI, 3, 0, 0, 1),            // R3 = 1 (len)
    CPU_ENCODE(OP_SYSCALL, 0, 0, 0, 8),         // write
    // newline
    CPU_ENCODE(OP_MOVI, 5, 0, 0, '\n'),         // R5 = '\n'
    CPU_ENCODE(OP_ST,   0, 6, 5, 0),            // [R6] = '\n'
    CPU_ENCODE(OP_MOVI, 1, 0, 0, 1),            // R1 = 1
    CPU_ENCODE(OP_SYSCALL, 0, 0, 0, 8),         // write
    // inc
    CPU_ENCODE(OP_MOVI, 8, 0, 0, 1),            // R8 = 1
    CPU_ENCODE(OP_ADD,  4, 4, 8, 0),            // R4 += 1
    // cmp
    CPU_ENCODE(OP_MOVI, 8, 0, 0, 10),           // R8 = 10
    CPU_ENCODE(OP_CMP,  0, 4, 8, 0),            // CMP R4, R8
    CPU_ENCODE(OP_JNZ,  0, 0, 0, -17),          // JNZ loop_start (rel=-17)
    CPU_ENCODE(OP_SYSCALL, 0, 0, 0, 0),         // exit(0)
};

// ===== echo: "Echo!\n" =====
static const uint8_t echo_data[] = "Echo!\n";

static uint32_t echo_text[] = {
    CPU_ENCODE(OP_LUI, 2, 0, 0, 1),             // R2 = page 1
    CPU_ENCODE(OP_MOVI, 1, 0, 0, 1),            // R1 = 1
    CPU_ENCODE(OP_MOVI, 3, 0, 0, 6),            // R3 = 6
    CPU_ENCODE(OP_SYSCALL, 0, 0, 0, 8),         // write
    CPU_ENCODE(OP_SYSCALL, 0, 0, 0, 0),         // exit
};

// ===== 构建最终二进制 =====

#define BIN_BUF_SIZE  8192

static uint8_t g_hello_buf[BIN_BUF_SIZE];
static int    g_hello_size = 0;

static uint8_t g_counter_buf[BIN_BUF_SIZE];
static int    g_counter_size = 0;

static uint8_t g_echo_buf[BIN_BUF_SIZE];
static int    g_echo_size = 0;

static int g_inited = 0;

static void init_binaries(void)
{
    if (g_inited) return;
    g_inited = 1;

    g_hello_size = build_upx(g_hello_buf, BIN_BUF_SIZE,
        hello_text, sizeof(hello_text)/4,
        hello_data, sizeof(hello_data), 0, 4096);
    if (g_hello_size < 0) g_hello_size = 0;

    g_counter_size = build_upx(g_counter_buf, BIN_BUF_SIZE,
        counter_text, sizeof(counter_text)/4,
        NULL, 0, 1024, 4096);
    if (g_counter_size < 0) g_counter_size = 0;

    g_echo_size = build_upx(g_echo_buf, BIN_BUF_SIZE,
        echo_text, sizeof(echo_text)/4,
        echo_data, sizeof(echo_data), 0, 4096);
    if (g_echo_size < 0) g_echo_size = 0;
}

static DemoBinary g_demos[] = {
    { "/bin/hello",   NULL, 0 },
    { "/bin/counter", NULL, 0 },
    { "/bin/echo",    NULL, 0 },
};

const DemoBinary *binaries_get_all(int *count)
{
    init_binaries();
    g_demos[0].data = g_hello_buf;   g_demos[0].size = (size_t)g_hello_size;
    g_demos[1].data = g_counter_buf; g_demos[1].size = (size_t)g_counter_size;
    g_demos[2].data = g_echo_buf;    g_demos[2].size = (size_t)g_echo_size;
    if (count) *count = 3;
    return g_demos;
}

const DemoBinary *binaries_find(const char *name)
{
    init_binaries();
    g_demos[0].data = g_hello_buf;   g_demos[0].size = (size_t)g_hello_size;
    g_demos[1].data = g_counter_buf; g_demos[1].size = (size_t)g_counter_size;
    g_demos[2].data = g_echo_buf;    g_demos[2].size = (size_t)g_echo_size;
    for (int i = 0; i < 3; i++)
        if (strcmp(g_demos[i].name, name) == 0)
            return &g_demos[i];
    return NULL;
}
