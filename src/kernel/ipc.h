/*
 * ipc.h
 * 信号量、消息队列、共享内存、有名管道与进程信号。
 */
#ifndef IPC_H
#define IPC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct PCB;

#define IPC_MAX_SEM         16
#define IPC_MAX_MSGQ        8
#define IPC_MAX_SHM         8
#define IPC_MAX_FIFO        8
#define IPC_MSG_MAX         32
#define IPC_MSG_SIZE        128
#define IPC_SHM_PAGES       4
#define IPC_SHM_MAX_ATTACH  4

#define SIG_TERM            15
#define SIG_KILL            9
#define SIG_USR1            10

typedef struct IpcSem {
    int      used;
    int      key;
    int      value;
} IpcSem;

typedef struct IpcMsgSlot {
    int      type;
    int      len;
    char     data[IPC_MSG_SIZE];
} IpcMsgSlot;

typedef struct IpcMsgq {
    int           used;
    int           key;
    int           head;
    int           tail;
    int           count;
    IpcMsgSlot    slots[IPC_MSG_MAX];
} IpcMsgq;

typedef struct IpcShm {
    int      used;
    int      key;
    int      page_count;
    int      page_ids[IPC_SHM_PAGES];
    int      attach_count;
} IpcShm;

typedef struct IpcFifo {
    int      used;
    char     path[64];
    int      pipe_id;
} IpcFifo;

typedef struct ShmAttach {
    int      shm_id;
    uint32_t virt_start;
    int      page_count;
} ShmAttach;

typedef struct IpcTable {
    IpcSem   sem[IPC_MAX_SEM];
    IpcMsgq  msgq[IPC_MAX_MSGQ];
    IpcShm   shm[IPC_MAX_SHM];
    IpcFifo  fifo[IPC_MAX_FIFO];
} IpcTable;

// 清零 IPC 资源表
void ipc_init(void);
// 释放共享内存页并清空 IPC 表
void ipc_shutdown(void);

// 向目标进程投递信号，必要时唤醒阻塞进程
int  ipc_kill(uint32_t pid, int sig);
// 处理当前进程待投递的终止与用户信号
void ipc_deliver_signals(struct PCB *p);

// 按 key 获取或创建信号量
int  ipc_semget(int key, int initval);
// 对信号量做 P/V 操作，不足时协作等待
int  ipc_semop(int semid, int delta);

// 按 key 获取或创建消息队列
int  ipc_msgget(int key);
// 向消息队列发送一条消息
int  ipc_msgsnd(int qid, int type, const char *data, int len);
// 从消息队列接收匹配类型的消息
int  ipc_msgrcv(int qid, int type, char *data, int max_len, int *out_type);

// 按 key 获取或创建共享内存段
int  ipc_shmget(int key, int size);
// 把共享内存映射到进程虚拟地址空间
int  ipc_shmat(struct PCB *p, int shmid, uint32_t virt_addr);
// 解除进程对共享内存段的映射
int  ipc_shmdt(struct PCB *p, int shmid);

// 在路径上创建有名管道
int  ipc_mkfifo(const char *path);
// 打开有名管道并分配读端或写端 fd
int  ipc_fifo_open(struct PCB *p, const char *path, uint16_t flags);

#ifdef __cplusplus
}
#endif

#endif
