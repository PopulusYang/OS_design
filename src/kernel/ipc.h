

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

void ipc_init(void);
void ipc_shutdown(void);


int  ipc_kill(uint32_t pid, int sig);
void ipc_deliver_signals(struct PCB *p);


int  ipc_semget(int key, int initval);
int  ipc_semop(int semid, int delta);


int  ipc_msgget(int key);
int  ipc_msgsnd(int qid, int type, const char *data, int len);
int  ipc_msgrcv(int qid, int type, char *data, int max_len, int *out_type);


int  ipc_shmget(int key, int size);
int  ipc_shmat(struct PCB *p, int shmid, uint32_t virt_addr);
int  ipc_shmdt(struct PCB *p, int shmid);


int  ipc_mkfifo(const char *path);
int  ipc_fifo_open(struct PCB *p, const char *path, uint16_t flags);

#ifdef __cplusplus
}
#endif

#endif 
