/*
 * binaries.c
 * 内置 hello、counter、echo 等演示程序的二进制数据。
 */
#include "binaries.h"
#include "kernel/cpu.h"
#include "kernel/process.h"
#include <string.h>
#include <stdio.h>

// 向缓冲区写入小端 32 位整数
static void w32(uint8_t *buf, uint32_t val)
{
    buf[0] = (uint8_t)val;
    buf[1] = (uint8_t)(val >> 8);
    buf[2] = (uint8_t)(val >> 16);
    buf[3] = (uint8_t)(val >> 24);
}

static int build_upx(uint8_t *out, size_t out_size,
                     const uint32_t *text, int text_words,
                     const uint8_t *data, uint32_t data_len,
                     uint32_t bss_len, uint32_t stack_len)
{
    size_t header_sz = sizeof(UPXHeader);
    size_t text_sz   = (size_t)text_words * 4;
    size_t total     = header_sz + text_sz + data_len;

    if (total > out_size) return -1;


    memcpy(out, "UPX\0", 4);
    w32(out + 4, 0);
    w32(out + 8, (uint32_t)text_sz);
    w32(out + 12, data_len);
    w32(out + 16, bss_len);
    w32(out + 20, stack_len);


    uint8_t *tp = out + header_sz;
    for (int i = 0; i < text_words; i++)
        w32(tp + i * 4, text[i]);


    if (data_len > 0) memcpy(out + header_sz + text_sz, data, data_len);

    return (int)total;
}

static const uint8_t hello_data[] = "Hello, World!\n";

static uint32_t hello_text[] = {
    CPU_ENCODE(OP_LUI, 2, 0, 0, 1),
    CPU_ENCODE(OP_MOVI, 1, 0, 0, 1),
    CPU_ENCODE(OP_MOVI, 3, 0, 0, 14),
    CPU_ENCODE(OP_SYSCALL, 0, 0, 0, 8),
    CPU_ENCODE(OP_MOVI, 1, 0, 0, 0),
    CPU_ENCODE(OP_SYSCALL, 0, 0, 0, 0),
};

static uint32_t counter_text[] = {
    CPU_ENCODE(OP_MOVI, 4, 0, 0, 0),
    CPU_ENCODE(OP_LUI,  6, 0, 0, 1),

    CPU_ENCODE(OP_MOVI, 5, 0, 0, 0x30),
    CPU_ENCODE(OP_ADD,  5, 5, 4, 0),
    CPU_ENCODE(OP_ST,   0, 6, 5, 0),
    CPU_ENCODE(OP_MOVI, 1, 0, 0, 1),
    CPU_ENCODE(OP_MOV,  2, 6, 0, 0),
    CPU_ENCODE(OP_MOVI, 3, 0, 0, 1),
    CPU_ENCODE(OP_SYSCALL, 0, 0, 0, 8),

    CPU_ENCODE(OP_MOVI, 5, 0, 0, '\n'),
    CPU_ENCODE(OP_ST,   0, 6, 5, 0),
    CPU_ENCODE(OP_MOVI, 1, 0, 0, 1),
    CPU_ENCODE(OP_SYSCALL, 0, 0, 0, 8),

    CPU_ENCODE(OP_MOVI, 8, 0, 0, 1),
    CPU_ENCODE(OP_ADD,  4, 4, 8, 0),

    CPU_ENCODE(OP_MOVI, 8, 0, 0, 10),
    CPU_ENCODE(OP_CMP,  0, 4, 8, 0),
    CPU_ENCODE(OP_JNZ,  0, 0, 0, -16),
    CPU_ENCODE(OP_SYSCALL, 0, 0, 0, 0),
};

static const uint8_t echo_data[] = "Echo!\n";

static uint32_t echo_text[] = {
    CPU_ENCODE(OP_LUI, 2, 0, 0, 1),
    CPU_ENCODE(OP_MOVI, 1, 0, 0, 1),
    CPU_ENCODE(OP_MOVI, 3, 0, 0, 6),
    CPU_ENCODE(OP_SYSCALL, 0, 0, 0, 8),
    CPU_ENCODE(OP_SYSCALL, 0, 0, 0, 0),
};

#define BIN_BUF_SIZE  8192

static uint8_t g_hello_buf[BIN_BUF_SIZE];
static int    g_hello_size = 0;

static uint8_t g_counter_buf[BIN_BUF_SIZE];
static int    g_counter_size = 0;

static uint8_t g_echo_buf[BIN_BUF_SIZE];
static int    g_echo_size = 0;

static uint8_t g_vim_buf[BIN_BUF_SIZE];
static int    g_vim_size = 0;

static uint8_t g_asm_buf[BIN_BUF_SIZE];
static int    g_asm_size = 0;

static int g_inited = 0;

// 懒初始化各演示程序的 UPX 缓冲区
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


    static uint32_t vim_text[] = {
        CPU_ENCODE(OP_SYSCALL, 0, 0, 0, 20),
        CPU_ENCODE(OP_MOVI, 1, 0, 0, 0),
        CPU_ENCODE(OP_SYSCALL, 0, 0, 0, 0),
    };
    g_vim_size = build_upx(g_vim_buf, BIN_BUF_SIZE,
        vim_text, sizeof(vim_text)/4, NULL, 0, 0, 256);
    if (g_vim_size < 0) g_vim_size = 0;


    static uint32_t asm_text[] = {
        CPU_ENCODE(OP_SYSCALL, 0, 0, 0, 21),
        CPU_ENCODE(OP_MOVI, 1, 0, 0, 0),
        CPU_ENCODE(OP_SYSCALL, 0, 0, 0, 0),
    };
    g_asm_size = build_upx(g_asm_buf, BIN_BUF_SIZE,
        asm_text, sizeof(asm_text)/4, NULL, 0, 0, 256);
    if (g_asm_size < 0) g_asm_size = 0;
}

#define DEMO_COUNT 5
static DemoBinary g_demos[DEMO_COUNT] = {
    { "/bin/hello",   NULL, 0 },
    { "/bin/counter", NULL, 0 },
    { "/bin/echo",    NULL, 0 },
    { "/bin/vim",     NULL, 0 },
    { "/bin/as",      NULL, 0 },
};

// 把缓冲区指针填入演示程序表
static void fill_demos(void) {
    g_demos[0].data = g_hello_buf;   g_demos[0].size = (size_t)g_hello_size;
    g_demos[1].data = g_counter_buf; g_demos[1].size = (size_t)g_counter_size;
    g_demos[2].data = g_echo_buf;    g_demos[2].size = (size_t)g_echo_size;
    g_demos[3].data = g_vim_buf;     g_demos[3].size = (size_t)g_vim_size;
    g_demos[4].data = g_asm_buf;     g_demos[4].size = (size_t)g_asm_size;
}

// 返回全部演示程序及数量
const DemoBinary *binaries_get_all(int *count)
{
    init_binaries();
    fill_demos();
    if (count) *count = DEMO_COUNT;
    return g_demos;
}

// 按路径名查找演示程序
const DemoBinary *binaries_find(const char *name)
{
    init_binaries();
    fill_demos();
    for (int i = 0; i < DEMO_COUNT; i++)
        if (strcmp(g_demos[i].name, name) == 0)
            return &g_demos[i];
    return NULL;
}
