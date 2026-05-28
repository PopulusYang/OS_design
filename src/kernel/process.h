




#ifndef PROCESS_H
#define PROCESS_H

#include "kernel/cpu.h"
#include "kernel/memory.h"
#include "kernel/ipc.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROC_MAX_COUNT         64
#define PROC_NAME_LEN          32
#define PROC_STACK_PAGES       4      
#define PROC_STACK_SIZE        (PROC_STACK_PAGES * MEM_PAGE_SIZE)
#define PROC_STACK_TOP         0x01000000U  

enum {
    PROC_FREE    = 0,
    PROC_READY   = 1,
    PROC_RUNNING = 2,
    PROC_BLOCKED = 3,
    PROC_ZOMBIE  = 4,
};


#define PROC_FD_TERM     0
#define PROC_FD_FILE     1
#define PROC_FD_PIPE_RD  2
#define PROC_FD_PIPE_WR  3
#define PROC_FD_FIFO_RD  4
#define PROC_FD_FIFO_WR  5

#define PROC_FD_IS_PIPE_RD(t)  ((t) == PROC_FD_PIPE_RD || (t) == PROC_FD_FIFO_RD)
#define PROC_FD_IS_PIPE_WR(t)  ((t) == PROC_FD_PIPE_WR || (t) == PROC_FD_FIFO_WR)

typedef struct ProcFD {
    int  fd_type;           
    int  fd_fs_fd;          
    int  fd_pipe_id;        
    int  fd_mode;           
    uint32_t fd_pos;        
} ProcFD;

#define PROC_MAX_FD  16


typedef struct UPXHeader {
    char     magic[4];       
    uint32_t entry;          
    uint32_t text_size;
    uint32_t data_size;
    uint32_t bss_size;
    uint32_t stack_size;
} UPXHeader;


typedef struct PCB {
    uint32_t  p_pid;
    uint32_t  p_ppid;
    char      p_name[PROC_NAME_LEN];
    uint8_t   p_state;

    
    CPUContext p_cpu;

    
    uint32_t  p_page_table[MEM_MAX_PROCESS_PAGES];
    uint32_t  p_text_start;      
    uint32_t  p_text_pages;
    uint32_t  p_data_start;
    uint32_t  p_data_pages;
    uint32_t  p_bss_end;
    uint32_t  p_stack_top;       
    uint32_t  p_heap_brk;        

    
    ProcFD    p_ofile[PROC_MAX_FD];
    int       p_cwd_ino;

    
    char      **p_envp;
    int        p_envc;

    
    uint32_t  p_children[PROC_MAX_COUNT];
    int       p_child_count;
    int       p_exit_code;
    uint32_t  p_waiting_parent;

    
    uint32_t  p_pending_sig;
    int       p_sigusr1_count;
    ShmAttach p_shm[IPC_SHM_MAX_ATTACH];

} PCB;




int  proc_init(void);


void proc_shutdown(void);


PCB *proc_current(void);


void proc_set_current(PCB *p);


PCB *proc_find(uint32_t pid);


PCB *proc_alloc(void);


void proc_free(PCB *p);


PCB *proc_create_init(void);


int  proc_fork(void);


int  proc_pipe(int fds[2]);


int  proc_exec(PCB *p, const char *path);


int  proc_wait(int *status);


void proc_exit(int code);


int  proc_count(void);


PCB *proc_get_table(int *count_out);


int  upx_validate(const uint8_t *data, uint32_t size);


int  upx_load(PCB *p, const uint8_t *data, uint32_t size);


int  proc_alloc_fd(PCB *p);


void proc_free_fd(PCB *p, int fd);


uint32_t proc_sbrk(PCB *p, int32_t increment);

#ifdef __cplusplus
}
#endif

#endif 
