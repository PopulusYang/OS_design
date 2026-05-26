// process.c —— 进程管理实现

#include "kernel/process.h"
#include "kernel/memory.h"
#include "fs/file_sys.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static PCB   g_proc_table[PROC_MAX_COUNT];
static PCB  *g_current = NULL;
static int   g_next_pid = 1;

// ---------- 初始化 ----------

int proc_init(void)
{
    memset(g_proc_table, 0, sizeof(g_proc_table));
    g_current = NULL;
    g_next_pid = 1;
    return 0;
}

void proc_shutdown(void)
{
    for (int i = 0; i < PROC_MAX_COUNT; i++) {
        if (g_proc_table[i].p_state != PROC_FREE) {
            for (int j = 0; j < PROC_MAX_FD; j++)
                if (g_proc_table[i].p_ofile[j].fd_type != 0) proc_free_fd(&g_proc_table[i], j);
        }
    }
    memset(g_proc_table, 0, sizeof(g_proc_table));
    g_current = NULL;
}

PCB *proc_current(void) { return g_current; }
void proc_set_current(PCB *p) { g_current = p; }

PCB *proc_find(uint32_t pid)
{
    for (int i = 0; i < PROC_MAX_COUNT; i++)
        if (g_proc_table[i].p_state != PROC_FREE && g_proc_table[i].p_pid == pid)
            return &g_proc_table[i];
    return NULL;
}

PCB *proc_alloc(void)
{
    for (int i = 0; i < PROC_MAX_COUNT; i++) {
        if (g_proc_table[i].p_state == PROC_FREE) {
            memset(&g_proc_table[i], 0, sizeof(PCB));
            g_proc_table[i].p_pid = (uint32_t)g_next_pid++;
            g_proc_table[i].p_state = PROC_READY;
            g_proc_table[i].p_cwd_ino = 1; // root by default
            for (int j = 1; j < MEM_MAX_PROCESS_PAGES; j++)
                g_proc_table[i].p_page_table[j] = 0;
            // 映射代码段起始页
            g_proc_table[i].p_page_table[0] = (uint32_t)mem_alloc_pages(1);
            return &g_proc_table[i];
        }
    }
    return NULL;
}

void proc_free(PCB *p)
{
    if (p == NULL) return;
    // 释放所有物理页
    for (int i = 0; i < MEM_MAX_PROCESS_PAGES; i++) {
        if (p->p_page_table[i] != 0) {
            mem_free_pages((int)p->p_page_table[i], 1);
            p->p_page_table[i] = 0;
        }
    }
    // 释放环境变量
    if (p->p_envp) {
        for (int i = 0; i < p->p_envc; i++) free(p->p_envp[i]);
        free(p->p_envp);
    }
    p->p_state = PROC_FREE;
}

PCB *proc_create_init(void)
{
    PCB *p = proc_alloc();
    if (p == NULL) return NULL;
    p->p_pid = 0;
    p->p_ppid = 0;
    strncpy(p->p_name, "init", PROC_NAME_LEN - 1);
    p->p_state = PROC_RUNNING;
    p->p_text_start = MEM_USER_START;
    p->p_stack_top = PROC_STACK_TOP;
    cpu_init(&p->p_cpu, p->p_page_table, 0, PROC_STACK_TOP);
    proc_set_current(p);
    return p;
}

// ---------- fork ----------

int proc_fork(void)
{
    PCB *parent = g_current;
    if (parent == NULL) return -1;

    PCB *child = proc_alloc();
    if (child == NULL) return -1;

    // 复制页表：为每个已映射的父页分配新物理页并拷贝内容
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

    // 复制寄存器状态
    memcpy(&child->p_cpu, &parent->p_cpu, sizeof(CPUContext));
    child->p_cpu.page_table = child->p_page_table;
    child->p_cpu.ticks_left = CPU_TIMESLICE;

    // 复制元数据
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

    // 复制 FD 表
    for (int i = 0; i < PROC_MAX_FD; i++)
        child->p_ofile[i] = parent->p_ofile[i];

    // 子进程 R0 = 0（fork 返回值）
    child->p_cpu.regs[0] = 0;

    // 记录父子关系
    if (parent->p_child_count < PROC_MAX_COUNT)
        parent->p_children[parent->p_child_count++] = child->p_pid;

    // 父进程返回子进程 PID
    parent->p_cpu.regs[0] = child->p_pid;

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

    // 释放旧页表
    for (int i = 0; i < MEM_MAX_PROCESS_PAGES; i++) {
        if (p->p_page_table[i] != 0) {
            mem_free_pages((int)p->p_page_table[i], 1);
            p->p_page_table[i] = 0;
        }
    }

    // 计算所需页数
    uint32_t text_pages = (hdr->text_size + MEM_PAGE_SIZE - 1) / MEM_PAGE_SIZE;
    if (text_pages == 0) text_pages = 1;
    uint32_t data_pages = (hdr->data_size + MEM_PAGE_SIZE - 1) / MEM_PAGE_SIZE;
    uint32_t bss_pages  = (hdr->bss_size  + MEM_PAGE_SIZE - 1) / MEM_PAGE_SIZE;
    uint32_t stack_pages = (hdr->stack_size > 0 ? hdr->stack_size : PROC_STACK_SIZE)
                           / MEM_PAGE_SIZE;
    if (stack_pages == 0) stack_pages = PROC_STACK_PAGES;
    uint32_t total_pages = text_pages + data_pages + bss_pages + stack_pages;
    if (total_pages > MEM_MAX_PROCESS_PAGES) return -1;

    // 分配 text 页
    uint32_t virt = MEM_USER_START;
    for (uint32_t i = 0; i < text_pages; i++) {
        int phys = mem_alloc_pages(1);
        if (phys < 0) return -1;
        p->p_page_table[(virt + i * MEM_PAGE_SIZE - MEM_USER_START) / MEM_PAGE_SIZE] = (uint32_t)phys;
    }
    p->p_text_start = MEM_USER_START;
    p->p_text_pages = text_pages;

    // 分配 data 页
    uint32_t data_virt = virt + text_pages * MEM_PAGE_SIZE;
    for (uint32_t i = 0; i < data_pages; i++) {
        int phys = mem_alloc_pages(1);
        if (phys < 0) return -1;
        p->p_page_table[(data_virt + i * MEM_PAGE_SIZE - MEM_USER_START) / MEM_PAGE_SIZE] = (uint32_t)phys;
    }
    p->p_data_start = data_virt;
    p->p_data_pages = data_pages;

    // 分配 bss 页
    uint32_t bss_virt = data_virt + data_pages * MEM_PAGE_SIZE;
    for (uint32_t i = 0; i < bss_pages; i++) {
        int phys = mem_alloc_pages(1);
        if (phys < 0) return -1;
        p->p_page_table[(bss_virt + i * MEM_PAGE_SIZE - MEM_USER_START) / MEM_PAGE_SIZE] = (uint32_t)phys;
    }
    p->p_bss_end = bss_virt + bss_pages * MEM_PAGE_SIZE;

    // 分配栈页
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

    // 在拷贝前先设置页表，cpu_virt_to_phys 依赖它
    p->p_cpu.page_table = p->p_page_table;

    // 拷贝 text 到物理内存
    {
        const uint8_t *text_src = data + sizeof(UPXHeader);
        for (uint32_t i = 0; i < hdr->text_size; i++) {
            uint32_t phys;
            if (cpu_virt_to_phys(&p->p_cpu, p->p_text_start + i, &phys) == 0)
                mem_write8(phys, text_src[i]);
        }
    }

    // 拷贝 data 到物理内存
    {
        const uint8_t *data_src = data + sizeof(UPXHeader) + hdr->text_size;
        for (uint32_t i = 0; i < hdr->data_size; i++) {
            uint32_t phys;
            if (cpu_virt_to_phys(&p->p_cpu, p->p_data_start + i, &phys) == 0)
                mem_write8(phys, data_src[i]);
        }
    }

    // 清零 BSS
    for (uint32_t i = 0; i < bss_pages * MEM_PAGE_SIZE; i++) {
        uint32_t phys;
        if (cpu_virt_to_phys(&p->p_cpu, p->p_bss_end + i, &phys) == 0)
            mem_write8(phys, 0);
    }

    // 设置 CPU 上下文（页表已在上面设置）
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
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    // 读取整个文件（最多 1MB）
    uint8_t *buf = (uint8_t *)malloc(1024 * 1024);
    if (buf == NULL) { close(fd); return -1; }
    int total = 0, n;
    while (total < 1024 * 1024 && (n = read(fd, buf + total, 4096)) > 0)
        total += n;
    close(fd);

    if (total <= 0) { free(buf); return -1; }

    int rc = upx_load(p, buf, (uint32_t)total);
    free(buf);
    return rc;
}

// ---------- wait / exit ----------

int proc_wait(int *status)
{
    PCB *parent = g_current;
    if (parent == NULL) return -1;

    // 查找僵尸子进程
    for (int i = 0; i < parent->p_child_count; i++) {
        PCB *child = proc_find(parent->p_children[i]);
        if (child && child->p_state == PROC_ZOMBIE) {
            if (status) *status = child->p_exit_code;
            uint32_t cpid = child->p_pid;
            proc_free(child);
            // 从 children 列表中移除
            for (int j = i; j < parent->p_child_count - 1; j++)
                parent->p_children[j] = parent->p_children[j + 1];
            parent->p_child_count--;
            return (int)cpid;
        }
    }
    return -1; // 无僵尸子进程
}

void proc_exit(int code)
{
    PCB *p = g_current;
    if (p == NULL || p->p_pid == 0) return; // init 不退出

    p->p_exit_code = code;
    p->p_state = PROC_ZOMBIE;
    // R0 = 退出码
    p->p_cpu.regs[0] = (uint32_t)code;
}

// ---------- 辅助 ----------

int proc_count(void)
{
    int cnt = 0;
    for (int i = 0; i < PROC_MAX_COUNT; i++)
        if (g_proc_table[i].p_state != PROC_FREE) cnt++;
    return cnt;
}

PCB *proc_get_table(int *count_out)
{
    if (count_out) *count_out = PROC_MAX_COUNT;
    return g_proc_table;
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
