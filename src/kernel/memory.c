// memory.c —— 128MB 内存管理器实现

#include "kernel/memory.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static uint8_t  *g_phys_mem = NULL;      // 128MB 物理内存
static uint8_t  *g_page_bitmap = NULL;    // 页框位图 (32768 bits = 4096 bytes)
static int       g_total_pages = 0;
static int       g_free_pages = 0;

// ---------- 生命周期 ----------

int mem_init(void)
{
    if (g_phys_mem != NULL) return 0;

    g_total_pages = (int)MEM_TOTAL_PAGES;
    g_phys_mem = (uint8_t *)calloc(1, MEM_TOTAL_SIZE);
    if (g_phys_mem == NULL) return -1;

    int bitmap_bytes = (g_total_pages + 7) / 8;
    g_page_bitmap = (uint8_t *)calloc(1, (size_t)bitmap_bytes);
    if (g_page_bitmap == NULL) { free(g_phys_mem); g_phys_mem = NULL; return -1; }

    // 标记内核预留页为已使用
    int kernel_pages = MEM_KERNEL_PAGES; // 4096 pages = 16MB
    for (int i = 0; i < kernel_pages; i++) {
        g_page_bitmap[i / 8] |= (uint8_t)(1U << (i % 8));
    }
    g_free_pages = g_total_pages - kernel_pages;
    return 0;
}

void mem_shutdown(void)
{
    free(g_phys_mem);  g_phys_mem = NULL;
    free(g_page_bitmap); g_page_bitmap = NULL;
    g_total_pages = 0;
    g_free_pages = 0;
}

// ---------- 页分配 ----------

int mem_alloc_pages(int n)
{
    if (n <= 0 || n > g_free_pages) return -1;

    int run_start = -1, run_len = 0;
    for (int i = 0; i < g_total_pages; i++) {
        if ((g_page_bitmap[i / 8] & (1U << (i % 8))) == 0) {
            if (run_start < 0) run_start = i;
            run_len++;
            if (run_len >= n) {
                for (int j = run_start; j < run_start + n; j++)
                    g_page_bitmap[j / 8] |= (uint8_t)(1U << (j % 8));
                g_free_pages -= n;
                return run_start;
            }
        } else {
            run_start = -1; run_len = 0;
        }
    }
    return -1;
}

void mem_free_pages(int page, int n)
{
    if (page < 0 || n <= 0) return;
    for (int i = page; i < page + n && i < g_total_pages; i++) {
        g_page_bitmap[i / 8] &= (uint8_t)(~(1U << (i % 8)));
        g_free_pages++;
    }
}

// ---------- 地址访问 ----------

void *mem_page_ptr(int page)
{
    if (g_phys_mem == NULL || page < 0 || page >= g_total_pages) return NULL;
    return g_phys_mem + (uint32_t)page * MEM_PAGE_SIZE;
}

void *mem_kernel_ptr(void)
{
    return g_phys_mem;
}

int mem_free_page_count(void) { return g_free_pages; }

// 验证物理地址范围
static int addr_valid(uint32_t addr)
{
    return g_phys_mem != NULL && addr < MEM_TOTAL_SIZE;
}

uint32_t mem_read32(uint32_t phys_addr)
{
    if (!addr_valid(phys_addr + 3)) return 0;
    uint8_t *p = g_phys_mem + phys_addr;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void mem_write32(uint32_t phys_addr, uint32_t val)
{
    if (!addr_valid(phys_addr + 3)) return;
    uint8_t *p = g_phys_mem + phys_addr;
    p[0] = (uint8_t)val; p[1] = (uint8_t)(val >> 8);
    p[2] = (uint8_t)(val >> 16); p[3] = (uint8_t)(val >> 24);
}

uint8_t mem_read8(uint32_t phys_addr)
{
    if (!addr_valid(phys_addr)) return 0;
    return g_phys_mem[phys_addr];
}

void mem_write8(uint32_t phys_addr, uint8_t val)
{
    if (!addr_valid(phys_addr)) return;
    g_phys_mem[phys_addr] = val;
}

void mem_copy(uint32_t dst_phys, uint32_t src_phys, uint32_t len)
{
    if (!addr_valid(dst_phys + len - 1) || !addr_valid(src_phys + len - 1)) return;
    memmove(g_phys_mem + dst_phys, g_phys_mem + src_phys, len);
}

void mem_zero(uint32_t phys_addr, uint32_t len)
{
    if (!addr_valid(phys_addr + len - 1)) return;
    memset(g_phys_mem + phys_addr, 0, len);
}
