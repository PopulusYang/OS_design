// process.c —— 进程管理实现（支持共享/本地模式）

#include "kernel/process.h"
#include "kernel/memory.h"
#include "kernel/scheduler.h"
#include "kernel_shared.h"
#include "fs/file_sys.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern KernelShared *g_kernel;

// ---------- 辅助 ----------

// 通过 proc_table 下标获取 PCB 指针
static PCB *proc_by_idx(int idx) {
    if (idx < 0 || idx >= PROC_MAX_COUNT) return NULL;
    if (g_kernel->proc_table[idx].p_state == PROC_FREE) return NULL;
    return &g_kernel->proc_table[idx];
}

// PCB 指针转下标
static int proc_to_idx(PCB *p) {
    if (p == NULL) return -1;
    return (int)(p - g_kernel->proc_table);
}

// ---------- 初始化 ----------

int proc_init(void)
{
    if (g_kernel == NULL) return -1;
    if (g_kernel->initialized) return 0; // 共享内核已由 kernel_shared_create 初始化
    memset(g_kernel->proc_table, 0, sizeof(g_kernel->proc_table));
    g_kernel->current_idx = -1;
    g_kernel->next_pid = 1;
    return 0;
}

void proc_shutdown(void)
{
    if (g_kernel == NULL) return;
    for (int i = 0; i < PROC_MAX_COUNT; i++) {
        if (g_kernel->proc_table[i].p_state != PROC_FREE) {
            for (int j = 0; j < PROC_MAX_FD; j++)
                if (g_kernel->proc_table[i].p_ofile[j].fd_type != 0)
                    proc_free_fd(&g_kernel->proc_table[i], j);
        }
    }
    memset(g_kernel->proc_table, 0, sizeof(g_kernel->proc_table));
    g_kernel->current_idx = -1;
}

PCB *proc_current(void) { return proc_by_idx(g_kernel ? g_kernel->current_idx : -1); }

void proc_set_current(PCB *p) {
    g_kernel->current_idx = proc_to_idx(p);
}

PCB *proc_find(uint32_t pid)
{
    if (g_kernel == NULL) return NULL;
    for (int i = 0; i < PROC_MAX_COUNT; i++)
        if (g_kernel->proc_table[i].p_state != PROC_FREE && g_kernel->proc_table[i].p_pid == pid)
            return &g_kernel->proc_table[i];
    return NULL;
}

PCB *proc_alloc(void)
{
    if (g_kernel == NULL) return NULL;
    for (int i = 0; i < PROC_MAX_COUNT; i++) {
        if (g_kernel->proc_table[i].p_state == PROC_FREE) {
            memset(&g_kernel->proc_table[i], 0, sizeof(PCB));
            g_kernel->proc_table[i].p_pid = (uint32_t)g_kernel->next_pid++;
            g_kernel->proc_table[i].p_state = PROC_READY;
            g_kernel->proc_table[i].p_cwd_ino = 1;
            for (int j = 1; j < MEM_MAX_PROCESS_PAGES; j++)
                g_kernel->proc_table[i].p_page_table[j] = 0;
            g_kernel->proc_table[i].p_page_table[0] = (uint32_t)mem_alloc_pages(1);
            return &g_kernel->proc_table[i];
        }
    }
    return NULL;
}

void proc_free(PCB *p)
{
    if (p == NULL) return;
    for (int i = 0; i < MEM_MAX_PROCESS_PAGES; i++) {
        if (p->p_page_table[i] != 0) {
            mem_free_pages((int)p->p_page_table[i], 1);
            p->p_page_table[i] = 0;
        }
    }
    if (p->p_envp) {
        for (int i = 0; i < p->p_envc; i++) free(p->p_envp[i]);
        free(p->p_envp);
    }
    p->p_state = PROC_FREE;
}

PCB *proc_create_init(void)
{
    // 共享模式下 init 可能已被其他终端会话创建
    PCB *existing = proc_find(0);
    if (existing) {
        proc_set_current(existing);
        return existing;
    }

    PCB *p = proc_alloc();
    if (p == NULL) return NULL;
    p->p_pid = 0;
    p->p_ppid = 0;
    strncpy(p->p_name, "init", PROC_NAME_LEN - 1);
    p->p_state = PROC_RUNNING;
    p->p_text_start = MEM_USER_START;
    p->p_stack_top = PROC_STACK_TOP;
    cpu_init(&p->p_cpu, 0, PROC_STACK_TOP);
    proc_set_current(p);
    return p;
}

// ---------- fork ----------

int proc_fork(void)
{
    PCB *parent = proc_current();
    if (parent == NULL) return -1;

    PCB *child = proc_alloc();
    if (child == NULL) return -1;

    for (int i = 0; i < MEM_MAX_PROCESS_PAGES; i++) {
        if (parent->p_page_table[i] != 0) {
            int new_page = mem_alloc_pages(1);
            if (new_page < 0) { proc_free(child); return -1; }
            child->p_page_table[i] = (uint32_t)new_page;
            uint32_t src_phys = parent->p_page_table[i] * MEM_PAGE_SIZE;
            uint32_t dst_phys = (uint32_t)new_page * MEM_PAGE_SIZE;
            mem_copy(dst_phys, src_phys, MEM_PAGE_SIZE);
        }
    }

    memcpy(&child->p_cpu, &parent->p_cpu, sizeof(CPUContext));
    child->p_cpu.ticks_left = CPU_TIMESLICE;

    child->p_ppid = parent->p_pid;
    child->p_text_start = parent->p_text_start;
    child->p_text_pages = parent->p_text_pages;
    child->p_data_start = parent->p_data_start;
    child->p_data_pages = parent->p_data_pages;
    child->p_bss_end = parent->p_bss_end;
    child->p_heap_brk = parent->p_heap_brk;
    child->p_stack_top = parent->p_stack_top;
    child->p_cwd_ino = parent->p_cwd_ino;
    strncpy(child->p_name, parent->p_name, PROC_NAME_LEN - 1);

    for (int i = 0; i < PROC_MAX_FD; i++)
        child->p_ofile[i] = parent->p_ofile[i];

    child->p_cpu.regs[0] = 0;

    if (parent->p_child_count < PROC_MAX_COUNT)
        parent->p_children[parent->p_child_count++] = child->p_pid;

    parent->p_cpu.regs[0] = child->p_pid;

    sched_enqueue(child);

    return (int)child->p_pid;
}

// ---------- exec（加载 UPX） ----------

int upx_validate(const uint8_t *data, uint32_t size)
{
    if (size < sizeof(UPXHeader)) return -1;
    const UPXHeader *hdr = (const UPXHeader *)data;
    if (hdr->magic[0] != 'U' || hdr->magic[1] != 'P' || hdr->magic[2] != 'X' || hdr->magic[3] != 0)
        return -1;
    if (hdr->entry >= hdr->text_size / 4) return -1;
    if (sizeof(UPXHeader) + hdr->text_size + hdr->data_size > size) return -1;
    return 0;
}

int upx_load(PCB *p, const uint8_t *data, uint32_t size)
{
    if (upx_validate(data, size) != 0) return -1;
    const UPXHeader *hdr = (const UPXHeader *)data;

    for (int i = 0; i < MEM_MAX_PROCESS_PAGES; i++) {
        if (p->p_page_table[i] != 0) {
            mem_free_pages((int)p->p_page_table[i], 1);
            p->p_page_table[i] = 0;
        }
    }

    uint32_t text_pages = (hdr->text_size + MEM_PAGE_SIZE - 1) / MEM_PAGE_SIZE;
    if (text_pages == 0) text_pages = 1;
    uint32_t data_pages = (hdr->data_size + MEM_PAGE_SIZE - 1) / MEM_PAGE_SIZE;
    uint32_t bss_pages  = (hdr->bss_size  + MEM_PAGE_SIZE - 1) / MEM_PAGE_SIZE;
    uint32_t stack_pages = (hdr->stack_size > 0 ? hdr->stack_size : PROC_STACK_SIZE)
                           / MEM_PAGE_SIZE;
    if (stack_pages == 0) stack_pages = PROC_STACK_PAGES;
    uint32_t total_pages = text_pages + data_pages + bss_pages + stack_pages;
    if (total_pages > MEM_MAX_PROCESS_PAGES) return -1;

    uint32_t virt = MEM_USER_START;
    for (uint32_t i = 0; i < text_pages; i++) {
        int phys = mem_alloc_pages(1);
        if (phys < 0) return -1;
        p->p_page_table[(virt + i * MEM_PAGE_SIZE - MEM_USER_START) / MEM_PAGE_SIZE] = (uint32_t)phys;
    }
    p->p_text_start = MEM_USER_START;
    p->p_text_pages = text_pages;

    uint32_t data_virt = virt + text_pages * MEM_PAGE_SIZE;
    for (uint32_t i = 0; i < data_pages; i++) {
        int phys = mem_alloc_pages(1);
        if (phys < 0) return -1;
        p->p_page_table[(data_virt + i * MEM_PAGE_SIZE - MEM_USER_START) / MEM_PAGE_SIZE] = (uint32_t)phys;
    }
    p->p_data_start = data_virt;
    p->p_data_pages = data_pages;

    uint32_t bss_virt = data_virt + data_pages * MEM_PAGE_SIZE;
    for (uint32_t i = 0; i < bss_pages; i++) {
        int phys = mem_alloc_pages(1);
        if (phys < 0) return -1;
        p->p_page_table[(bss_virt + i * MEM_PAGE_SIZE - MEM_USER_START) / MEM_PAGE_SIZE] = (uint32_t)phys;
    }
    p->p_bss_end = bss_virt + bss_pages * MEM_PAGE_SIZE;

    uint32_t stack_virt = PROC_STACK_TOP - stack_pages * MEM_PAGE_SIZE;
    for (uint32_t i = 0; i < stack_pages; i++) {
        int phys = mem_alloc_pages(1);
        if (phys < 0) return -1;
        uint32_t page_idx = (stack_virt + i * MEM_PAGE_SIZE - MEM_USER_START) / MEM_PAGE_SIZE;
        if (page_idx < MEM_MAX_PROCESS_PAGES)
            p->p_page_table[page_idx] = (uint32_t)phys;
    }
    p->p_stack_top = PROC_STACK_TOP;
    p->p_heap_brk = p->p_bss_end;

    // 拷贝 text 到物理内存（cpu_virt_to_phys 通过 container_of 取 p_page_table）
    {
        const uint8_t *text_src = data + sizeof(UPXHeader);
        for (uint32_t i = 0; i < hdr->text_size; i++) {
            uint32_t phys;
            if (cpu_virt_to_phys(&p->p_cpu, p->p_text_start + i, &phys) == 0)
                mem_write8(phys, text_src[i]);
        }
    }

    {
        const uint8_t *data_src = data + sizeof(UPXHeader) + hdr->text_size;
        for (uint32_t i = 0; i < hdr->data_size; i++) {
            uint32_t phys;
            if (cpu_virt_to_phys(&p->p_cpu, p->p_data_start + i, &phys) == 0)
                mem_write8(phys, data_src[i]);
        }
    }

    for (uint32_t i = 0; i < bss_pages * MEM_PAGE_SIZE; i++) {
        uint32_t phys;
        if (cpu_virt_to_phys(&p->p_cpu, p->p_bss_end + i, &phys) == 0)
            mem_write8(phys, 0);
    }

    p->p_cpu.pc = hdr->entry;
    p->p_cpu.regs[CPU_REG_SP] = p->p_stack_top - 4;
    p->p_cpu.regs[0] = 0;
    p->p_cpu.flags = 0;
    p->p_cpu.ticks_left = CPU_TIMESLICE;
    p->p_cpu.sycall_halt = 0;

    return 0;
}

int proc_exec(PCB *p, const char *path)
{
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) return -1;

    uint8_t *buf = (uint8_t *)malloc(1024 * 1024);
    if (buf == NULL) { vfs_close(fd); return -1; }
    int total = 0, n;
    while (total < 1024 * 1024 && (n = vfs_read(fd, buf + total, 4096)) > 0)
        total += n;
    vfs_close(fd);

    if (total <= 0) { free(buf); return -1; }

    int rc = upx_load(p, buf, (uint32_t)total);
    free(buf);
    return rc;
}

// ---------- wait / exit ----------

int proc_wait(int *status)
{
    PCB *parent = proc_current();
    if (parent == NULL) return -1;

    for (int i = 0; i < parent->p_child_count; i++) {
        PCB *child = proc_find(parent->p_children[i]);
        if (child && child->p_state == PROC_ZOMBIE) {
            if (status) *status = child->p_exit_code;
            uint32_t cpid = child->p_pid;
            proc_free(child);
            for (int j = i; j < parent->p_child_count - 1; j++)
                parent->p_children[j] = parent->p_children[j + 1];
            parent->p_child_count--;
            return (int)cpid;
        }
    }
    return -1;
}

void proc_exit(int code)
{
    PCB *p = proc_current();
    if (p == NULL || p->p_pid == 0) return;

    p->p_exit_code = code;
    p->p_state = PROC_ZOMBIE;
    p->p_cpu.regs[0] = (uint32_t)code;
}

// ---------- 辅助 ----------

int proc_count(void)
{
    if (g_kernel == NULL) return 0;
    int cnt = 0;
    for (int i = 0; i < PROC_MAX_COUNT; i++)
        if (g_kernel->proc_table[i].p_state != PROC_FREE) cnt++;
    return cnt;
}

PCB *proc_get_table(int *count_out)
{
    if (count_out) *count_out = PROC_MAX_COUNT;
    return g_kernel ? g_kernel->proc_table : NULL;
}

int proc_alloc_fd(PCB *p)
{
    for (int i = 0; i < PROC_MAX_FD; i++)
        if (p->p_ofile[i].fd_type == 0) return i;
    return -1;
}

void proc_free_fd(PCB *p, int fd)
{
    if (fd >= 0 && fd < PROC_MAX_FD)
        memset(&p->p_ofile[fd], 0, sizeof(ProcFD));
}

uint32_t proc_sbrk(PCB *p, int32_t increment)
{
    uint32_t old_brk = p->p_heap_brk;
    uint32_t new_brk = (uint32_t)((int32_t)old_brk + increment);

    if (increment > 0) {
        uint32_t need_pages = (new_brk - old_brk + MEM_PAGE_SIZE - 1) / MEM_PAGE_SIZE;
        for (uint32_t i = 0; i < need_pages; i++) {
            uint32_t virt = old_brk + i * MEM_PAGE_SIZE;
            if (cpu_map_page(&p->p_cpu, virt) != 0) return old_brk;
        }
    }
    p->p_heap_brk = new_brk;
    return old_brk;
}
