/*
 * memory.h
 * 128MB 物理内存的页分配与读写接口。
 */
#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEM_TOTAL_SIZE          (128U * 1024U * 1024U)
#define MEM_PAGE_SIZE           4096U
#define MEM_TOTAL_PAGES         (MEM_TOTAL_SIZE / MEM_PAGE_SIZE)
#define MEM_KERNEL_PAGES        4096
#define MEM_USER_START          0x00000000U
#define MEM_MAX_PROCESS_PAGES   4096

// 初始化页位图，预留内核区并统计空闲页
int  mem_init(void);

// 释放内存子系统（共享模式下由宿主进程回收）
void mem_shutdown(void);

// 连续分配 n 个空闲物理页，返回起始页号
int  mem_alloc_pages(int n);

// 释放从 page 开始的 n 个物理页
void mem_free_pages(int page, int n);

// 返回指定物理页号对应的内存指针
void *mem_page_ptr(int page);

// 返回物理内存区起始指针
void *mem_kernel_ptr(void);

// 物理字节地址换算为页号
static inline int mem_addr_to_page(uint32_t addr) { return (int)(addr / MEM_PAGE_SIZE); }

// 页号换算为物理字节地址
static inline uint32_t mem_page_to_addr(int page) { return (uint32_t)page * MEM_PAGE_SIZE; }

// 取地址在页内的偏移
static inline uint32_t mem_page_offset(uint32_t addr) { return addr % MEM_PAGE_SIZE; }

// 返回当前空闲物理页数量
int  mem_free_page_count(void);

// 从物理地址读 32 位整数
uint32_t mem_read32(uint32_t phys_addr);

// 向物理地址写 32 位整数
void     mem_write32(uint32_t phys_addr, uint32_t val);

// 从物理地址读 8 位字节
uint8_t  mem_read8(uint32_t phys_addr);

// 向物理地址写 8 位字节
void     mem_write8(uint32_t phys_addr, uint8_t val);

// 在物理地址之间拷贝一段内存
void     mem_copy(uint32_t dst_phys, uint32_t src_phys, uint32_t len);

// 把一段物理内存清零
void     mem_zero(uint32_t phys_addr, uint32_t len);

// 返回页位图指针及总页数
const uint8_t *mem_get_page_bitmap(int *total_out);

// 打印物理内存与页位图使用情况
void mem_debug_print(void);

#ifdef __cplusplus
}
#endif

#endif
