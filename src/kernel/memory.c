

#include "kernel/memory.h"
#include "kernel_shared.h"
#include <stdio.h>
#include <string.h>

extern KernelShared *g_kernel;



int mem_init(void)
{
    if (g_kernel == NULL) return -1;
    
    if (g_kernel->free_pages > 0) return 0;  

    g_kernel->total_pages = (int)MEM_TOTAL_PAGES;
    memset(g_kernel->phys_mem, 0, MEM_TOTAL_SIZE);

    int bitmap_bytes = (g_kernel->total_pages + 7) / 8;
    memset(g_kernel->page_bitmap, 0, (size_t)bitmap_bytes);

    int kernel_pages = (int)MEM_KERNEL_PAGES;
    for (int i = 0; i < kernel_pages; i++)
        g_kernel->page_bitmap[i / 8] |= (uint8_t)(1U << (i % 8));
    g_kernel->free_pages = g_kernel->total_pages - kernel_pages;
    return 0;
}

void mem_shutdown(void)
{
    
    if (g_kernel == NULL) return;
    
}



int mem_alloc_pages(int n)
{
    if (g_kernel == NULL || n <= 0 || n > g_kernel->free_pages) return -1;

    int run_start = -1, run_len = 0;
    for (int i = 0; i < g_kernel->total_pages; i++) {
        if ((g_kernel->page_bitmap[i / 8] & (1U << (i % 8))) == 0) {
            if (run_start < 0) run_start = i;
            run_len++;
            if (run_len >= n) {
                for (int j = run_start; j < run_start + n; j++)
                    g_kernel->page_bitmap[j / 8] |= (uint8_t)(1U << (j % 8));
                g_kernel->free_pages -= n;
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
    if (g_kernel == NULL || page < 0 || n <= 0) return;
    for (int i = page; i < page + n && i < g_kernel->total_pages; i++) {
        g_kernel->page_bitmap[i / 8] &= (uint8_t)(~(1U << (i % 8)));
        g_kernel->free_pages++;
    }
}



void *mem_page_ptr(int page)
{
    if (g_kernel == NULL || page < 0 || page >= g_kernel->total_pages) return NULL;
    return g_kernel->phys_mem + (uint32_t)page * MEM_PAGE_SIZE;
}

void *mem_kernel_ptr(void)
{
    return g_kernel ? g_kernel->phys_mem : NULL;
}

int mem_free_page_count(void) { return g_kernel ? g_kernel->free_pages : 0; }

static int addr_valid(uint32_t addr)
{
    return g_kernel != NULL && addr < MEM_TOTAL_SIZE;
}

uint32_t mem_read32(uint32_t phys_addr)
{
    if (!addr_valid(phys_addr + 3)) return 0;
    uint8_t *p = g_kernel->phys_mem + phys_addr;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void mem_write32(uint32_t phys_addr, uint32_t val)
{
    if (!addr_valid(phys_addr + 3)) return;
    uint8_t *p = g_kernel->phys_mem + phys_addr;
    p[0] = (uint8_t)val; p[1] = (uint8_t)(val >> 8);
    p[2] = (uint8_t)(val >> 16); p[3] = (uint8_t)(val >> 24);
}

uint8_t mem_read8(uint32_t phys_addr)
{
    if (!addr_valid(phys_addr)) return 0;
    return g_kernel->phys_mem[phys_addr];
}

void mem_write8(uint32_t phys_addr, uint8_t val)
{
    if (!addr_valid(phys_addr)) return;
    g_kernel->phys_mem[phys_addr] = val;
}

void mem_copy(uint32_t dst_phys, uint32_t src_phys, uint32_t len)
{
    if (!addr_valid(dst_phys + len - 1) || !addr_valid(src_phys + len - 1)) return;
    memmove(g_kernel->phys_mem + dst_phys, g_kernel->phys_mem + src_phys, len);
}

void mem_zero(uint32_t phys_addr, uint32_t len)
{
    if (!addr_valid(phys_addr + len - 1)) return;
    memset(g_kernel->phys_mem + phys_addr, 0, len);
}

// ---------- 调试输出 ----------

void mem_debug_print(void)
{
    if (g_kernel == NULL) {
        printf("  Kernel not initialized.\n");
        return;
    }

    int total = g_kernel->total_pages;
    int kernel_pages = (int)MEM_KERNEL_PAGES;
    int used = total - g_kernel->free_pages;
    int user_used = used - kernel_pages;

    printf("\n");
    printf("  ── Physical Memory (128 MB) ────────────────────\n");
    printf("  Page size:          %u bytes\n", MEM_PAGE_SIZE);
    printf("  Total pages:        %d\n", total);
    printf("  Kernel reserved:    %d pages (%u MB)\n",
           kernel_pages, (kernel_pages * MEM_PAGE_SIZE) / (1024 * 1024));
    printf("  Free pages:         %d pages (%u KB)\n",
           g_kernel->free_pages,
           (unsigned)(g_kernel->free_pages * MEM_PAGE_SIZE / 1024));
    printf("  Used (user):        %d pages\n", user_used);
    printf("  Used (total):       %d pages\n", used);

    /* 可视化位图 — 64 pages per line */
    printf("\n  ── Page Bitmap (kernel=K, user=U, free=.) ──────\n");
    int bytes = (total + 7) / 8;
    int per_line = 64;
    for (int row = 0; row * per_line < total; row++) {
        int start = row * per_line;
        int end = (start + per_line) < total ? (start + per_line) : total;
        printf("  %4d: ", start);
        for (int i = start; i < end; i++) {
            int allocated = (g_kernel->page_bitmap[i / 8] >> (i % 8)) & 1;
            if (!allocated)      putchar('.');
            else if (i < kernel_pages) putchar('K');
            else                 putchar('U');
            if ((i + 1) % 64 == 0 && i + 1 < end) printf("\n        ");
        }
        putchar('\n');
    }
    printf("\n");
}
