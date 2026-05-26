// memory.h —— 128MB 虚拟内存管理器：页分配/释放、地址翻译、sbrk
//
// 物理内存：128MB 线性字节数组，4KB 页框，位图管理
// 内核预留：[0x00000000, 0x01000000) = 16MB
// 用户空间：[0x01000000, 0x08000000) = 112MB，28796 页

#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEM_TOTAL_SIZE          (128U * 1024U * 1024U)   // 128 MB
#define MEM_PAGE_SIZE           4096U
#define MEM_TOTAL_PAGES         (MEM_TOTAL_SIZE / MEM_PAGE_SIZE)  // 32768
#define MEM_KERNEL_PAGES        4096                      // 前 16MB 保留给内核
#define MEM_USER_START          0x00000000U               // 进程虚拟地址从 0 开始
#define MEM_MAX_PROCESS_PAGES   4096                      // 每进程最多 16MB（适配 LUI 12-bit）

// 初始化内存管理器（分配 128MB 数组、位图）
int  mem_init(void);

// 关闭内存管理器（释放资源）
void mem_shutdown(void);

// 分配 n 个连续物理页框，返回物理页号（-1 表示失败）
int  mem_alloc_pages(int n);

// 释放物理页框
void mem_free_pages(int page, int n);

// 获取物理页框的字节指针
void *mem_page_ptr(int page);

// 获取内核空间字节指针
void *mem_kernel_ptr(void);

// 物理地址 → 页号
static inline int mem_addr_to_page(uint32_t addr) { return (int)(addr / MEM_PAGE_SIZE); }

// 页号 → 物理地址
static inline uint32_t mem_page_to_addr(int page) { return (uint32_t)page * MEM_PAGE_SIZE; }

// 物理地址 → 物理页框内偏移
static inline uint32_t mem_page_offset(uint32_t addr) { return addr % MEM_PAGE_SIZE; }

// 获取可用物理页总数
int  mem_free_page_count(void);

// 从物理地址读 4 字节（小端）
uint32_t mem_read32(uint32_t phys_addr);

// 向物理地址写 4 字节（小端）
void     mem_write32(uint32_t phys_addr, uint32_t val);

// 从物理地址读 1 字节
uint8_t  mem_read8(uint32_t phys_addr);

// 向物理地址写 1 字节
void     mem_write8(uint32_t phys_addr, uint8_t val);

// 块拷贝（物理地址之间）
void     mem_copy(uint32_t dst_phys, uint32_t src_phys, uint32_t len);

// 块清零（物理地址）
void     mem_zero(uint32_t phys_addr, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif // MEMORY_H
