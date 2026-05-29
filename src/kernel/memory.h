





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


int  mem_init(void);


void mem_shutdown(void);


int  mem_alloc_pages(int n);


void mem_free_pages(int page, int n);


void *mem_page_ptr(int page);


void *mem_kernel_ptr(void);


static inline int mem_addr_to_page(uint32_t addr) { return (int)(addr / MEM_PAGE_SIZE); }


static inline uint32_t mem_page_to_addr(int page) { return (uint32_t)page * MEM_PAGE_SIZE; }


static inline uint32_t mem_page_offset(uint32_t addr) { return addr % MEM_PAGE_SIZE; }


int  mem_free_page_count(void);


uint32_t mem_read32(uint32_t phys_addr);


void     mem_write32(uint32_t phys_addr, uint32_t val);


uint8_t  mem_read8(uint32_t phys_addr);


void     mem_write8(uint32_t phys_addr, uint8_t val);


void     mem_copy(uint32_t dst_phys, uint32_t src_phys, uint32_t len);


void     mem_zero(uint32_t phys_addr, uint32_t len);

const uint8_t *mem_get_page_bitmap(int *total_out);

void mem_debug_print(void);

#ifdef __cplusplus
}
#endif

#endif 
