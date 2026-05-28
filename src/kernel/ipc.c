// ipc.c —— System V 风格 IPC 实现

#include "kernel/ipc.h"
#include "kernel/pipe.h"
#include "kernel/scheduler.h"
#include "kernel/memory.h"
#include "kernel_shared.h"
#include "vfs_core.h"
#include <string.h>

extern KernelShared *g_kernel;

static IpcTable *ipc_table(void)
{
    return g_kernel ? &g_kernel->ipc : NULL;
}

void ipc_init(void)
{
    IpcTable *t = ipc_table();
    if (t) memset(t, 0, sizeof(*t));
}

void ipc_shutdown(void)
{
    IpcTable *t = ipc_table();
    if (t == NULL) return;
    for (int i = 0; i < IPC_MAX_SHM; i++) {
        if (t->shm[i].used) {
            for (int j = 0; j < t->shm[i].page_count; j++) {
                if (t->shm[i].page_ids[j] >= 0)
                    mem_free_pages(t->shm[i].page_ids[j], 1);
            }
        }
    }
    memset(t, 0, sizeof(*t));
}

// ---- 信号 ----

void ipc_deliver_signals(PCB *p)
{
    if (p == NULL || p->p_pending_sig == 0) return;

    if (p->p_pending_sig & (1U << SIG_KILL)) {
        p->p_pending_sig = 0;
        proc_exit(SIG_KILL);
        return;
    }
    if (p->p_pending_sig & (1U << SIG_TERM)) {
        p->p_pending_sig = 0;
        proc_exit(SIG_TERM);
        return;
    }
    if (p->p_pending_sig & (1U << SIG_USR1)) {
        p->p_pending_sig &= ~(1U << SIG_USR1);
        p->p_sigusr1_count++;
    }
}

int ipc_kill(uint32_t pid, int sig)
{
    if (sig <= 0 || sig >= 32) return -1;
    PCB *t = proc_find(pid);
    if (t == NULL || t->p_state == PROC_FREE) return -1;

    t->p_pending_sig |= (1U << sig);
    if (sig == SIG_KILL || sig == SIG_TERM) {
        if (t->p_state == PROC_BLOCKED) {
            t->p_state = PROC_READY;
            sched_enqueue(t);
        }
        if (proc_current() == t) ipc_deliver_signals(t);
    }
    return 0;
}

// ---- 信号量 ----

static IpcSem *sem_get(int semid)
{
    IpcTable *t = ipc_table();
    if (t == NULL || semid < 0 || semid >= IPC_MAX_SEM || !t->sem[semid].used) return NULL;
    return &t->sem[semid];
}

int ipc_semget(int key, int initval)
{
    IpcTable *t = ipc_table();
    if (t == NULL || key <= 0) return -1;

    for (int i = 0; i < IPC_MAX_SEM; i++) {
        if (t->sem[i].used && t->sem[i].key == key) return i;
    }
    for (int i = 0; i < IPC_MAX_SEM; i++) {
        if (!t->sem[i].used) {
            t->sem[i].used = 1;
            t->sem[i].key = key;
            t->sem[i].value = initval < 0 ? 0 : initval;
            return i;
        }
    }
    return -1;
}

int ipc_semop(int semid, int delta)
{
    IpcSem *s = sem_get(semid);
    if (s == NULL) return -1;

    if (delta < 0) {
        for (int spin = 0; spin < 100000; spin++) {
            if (s->value > 0) {
                s->value--;
                return 0;
            }
            sched_cooperate();
            if (sched_ready_count() == 0) return -1;
        }
        return -1;
    }
    if (delta > 0) {
        s->value++;
        return 0;
    }
    return 0;
}

// ---- 消息队列 ----

static IpcMsgq *msgq_get(int qid)
{
    IpcTable *t = ipc_table();
    if (t == NULL || qid < 0 || qid >= IPC_MAX_MSGQ || !t->msgq[qid].used) return NULL;
    return &t->msgq[qid];
}

int ipc_msgget(int key)
{
    IpcTable *t = ipc_table();
    if (t == NULL || key <= 0) return -1;

    for (int i = 0; i < IPC_MAX_MSGQ; i++) {
        if (t->msgq[i].used && t->msgq[i].key == key) return i;
    }
    for (int i = 0; i < IPC_MAX_MSGQ; i++) {
        if (!t->msgq[i].used) {
            memset(&t->msgq[i], 0, sizeof(t->msgq[i]));
            t->msgq[i].used = 1;
            t->msgq[i].key = key;
            return i;
        }
    }
    return -1;
}

int ipc_msgsnd(int qid, int type, const char *data, int len)
{
    IpcMsgq *q = msgq_get(qid);
    if (q == NULL || data == NULL || len <= 0) return -1;
    if (len > IPC_MSG_SIZE) len = IPC_MSG_SIZE;

    for (int spin = 0; spin < 100000; spin++) {
        if (q->count < IPC_MSG_MAX) {
            IpcMsgSlot *m = &q->slots[q->tail];
            m->type = type;
            m->len = len;
            memcpy(m->data, data, (size_t)len);
            q->tail = (q->tail + 1) % IPC_MSG_MAX;
            q->count++;
            return len;
        }
        sched_cooperate();
        if (sched_ready_count() == 0) return -1;
    }
    return -1;
}

int ipc_msgrcv(int qid, int type, char *data, int max_len, int *out_type)
{
    IpcMsgq *q = msgq_get(qid);
    if (q == NULL || data == NULL || max_len <= 0) return -1;

    for (int spin = 0; spin < 100000; spin++) {
        if (q->count > 0) {
            for (int i = 0; i < q->count; i++) {
                int idx = (q->head + i) % IPC_MSG_MAX;
                IpcMsgSlot *m = &q->slots[idx];
                if (type != 0 && m->type != type) continue;

                int n = m->len < max_len ? m->len : max_len;
                memcpy(data, m->data, (size_t)n);
                if (out_type) *out_type = m->type;

                q->head = (idx + 1) % IPC_MSG_MAX;
                q->count--;
                return n;
            }
        }
        if (q->count == 0) {
            sched_cooperate();
            if (sched_ready_count() == 0) return -1;
            continue;
        }
        sched_cooperate();
    }
    return -1;
}

// ---- 共享内存 ----

static IpcShm *shm_get(int shmid)
{
    IpcTable *t = ipc_table();
    if (t == NULL || shmid < 0 || shmid >= IPC_MAX_SHM || !t->shm[shmid].used) return NULL;
    return &t->shm[shmid];
}

static int shm_pages_for_size(int size)
{
    int pages = (size + (int)MEM_PAGE_SIZE - 1) / (int)MEM_PAGE_SIZE;
    if (pages <= 0) pages = 1;
    if (pages > IPC_SHM_PAGES) pages = IPC_SHM_PAGES;
    return pages;
}

int ipc_shmget(int key, int size)
{
    IpcTable *t = ipc_table();
    if (t == NULL || key <= 0) return -1;

    for (int i = 0; i < IPC_MAX_SHM; i++) {
        if (t->shm[i].used && t->shm[i].key == key) return i;
    }
    int pages = shm_pages_for_size(size);
    for (int i = 0; i < IPC_MAX_SHM; i++) {
        if (!t->shm[i].used) {
            IpcShm *s = &t->shm[i];
            memset(s, 0, sizeof(*s));
            s->used = 1;
            s->key = key;
            s->page_count = pages;
            for (int j = 0; j < pages; j++) {
                s->page_ids[j] = mem_alloc_pages(1);
                if (s->page_ids[j] < 0) {
                    for (int k = 0; k < j; k++) mem_free_pages(s->page_ids[k], 1);
                    memset(s, 0, sizeof(*s));
                    return -1;
                }
                mem_zero(mem_page_to_addr((uint32_t)s->page_ids[j]), MEM_PAGE_SIZE);
            }
            return i;
        }
    }
    return -1;
}

static ShmAttach *proc_shm_find(PCB *p, int shmid)
{
    if (p == NULL) return NULL;
    for (int i = 0; i < IPC_SHM_MAX_ATTACH; i++) {
        if (p->p_shm[i].shm_id == shmid) return &p->p_shm[i];
    }
    return NULL;
}

static ShmAttach *proc_shm_alloc_slot(PCB *p)
{
    for (int i = 0; i < IPC_SHM_MAX_ATTACH; i++) {
        if (p->p_shm[i].shm_id < 0) return &p->p_shm[i];
    }
    return NULL;
}

int ipc_shmat(PCB *p, int shmid, uint32_t virt_addr)
{
    IpcShm *s = shm_get(shmid);
    if (p == NULL || s == NULL) return -1;
    if (proc_shm_find(p, shmid) != NULL) return -1;

    ShmAttach *a = proc_shm_alloc_slot(p);
    if (a == NULL) return -1;

    virt_addr &= ~(MEM_PAGE_SIZE - 1);
    for (int i = 0; i < s->page_count; i++) {
        uint32_t v = virt_addr + (uint32_t)i * MEM_PAGE_SIZE;
        uint32_t idx = (v - MEM_USER_START) / MEM_PAGE_SIZE;
        if (idx >= MEM_MAX_PROCESS_PAGES) return -1;
        p->p_page_table[idx] = (uint32_t)s->page_ids[i];
    }

    a->shm_id = shmid;
    a->virt_start = virt_addr;
    a->page_count = s->page_count;
    s->attach_count++;
    return shmid;
}

int ipc_shmdt(PCB *p, int shmid)
{
    IpcShm *s = shm_get(shmid);
    ShmAttach *a = proc_shm_find(p, shmid);
    if (p == NULL || s == NULL || a == NULL) return -1;

    for (int i = 0; i < a->page_count; i++) {
        uint32_t v = a->virt_start + (uint32_t)i * MEM_PAGE_SIZE;
        uint32_t idx = (v - MEM_USER_START) / MEM_PAGE_SIZE;
        if (idx < MEM_MAX_PROCESS_PAGES) p->p_page_table[idx] = 0;
    }

    a->shm_id = -1;
    a->virt_start = 0;
    a->page_count = 0;
    if (s->attach_count > 0) s->attach_count--;
    return 0;
}

// ---- 命名 FIFO ----

static IpcFifo *fifo_find_path(const char *path)
{
    IpcTable *t = ipc_table();
    if (t == NULL || path == NULL) return NULL;
    for (int i = 0; i < IPC_MAX_FIFO; i++) {
        if (t->fifo[i].used && strcmp(t->fifo[i].path, path) == 0) return &t->fifo[i];
    }
    return NULL;
}

int ipc_mkfifo(const char *path)
{
    IpcTable *t = ipc_table();
    if (t == NULL || path == NULL || path[0] != '/') return -1;
    if (fifo_find_path(path) != NULL) return -1;

    int pipe_id = pipe_alloc();
    if (pipe_id < 0) return -1;

    for (int i = 0; i < IPC_MAX_FIFO; i++) {
        if (!t->fifo[i].used) {
            t->fifo[i].used = 1;
            strncpy(t->fifo[i].path, path, sizeof(t->fifo[i].path) - 1);
            t->fifo[i].path[sizeof(t->fifo[i].path) - 1] = '\0';
            t->fifo[i].pipe_id = pipe_id;
            return 0;
        }
    }
    pipe_free(pipe_id);
    return -1;
}

int ipc_fifo_open(PCB *p, const char *path, uint16_t flags)
{
    IpcFifo *f = fifo_find_path(path);
    if (p == NULL || f == NULL) return -1;

    int rd = (flags & O_RDONLY) || (flags & O_RDWR);
    int wr = (flags & O_WRONLY) || (flags & O_RDWR);
    if (!rd && !wr) rd = 1;

    if (rd) {
        int fd = proc_alloc_fd(p);
        if (fd < 0) return -1;
        p->p_ofile[fd].fd_type = PROC_FD_FIFO_RD;
        p->p_ofile[fd].fd_pipe_id = f->pipe_id;
        p->p_ofile[fd].fd_mode = (int)flags;
        pipe_add_reader(f->pipe_id);
        return fd;
    }

    int fd = proc_alloc_fd(p);
    if (fd < 0) return -1;
    p->p_ofile[fd].fd_type = PROC_FD_FIFO_WR;
    p->p_ofile[fd].fd_pipe_id = f->pipe_id;
    p->p_ofile[fd].fd_mode = (int)flags;
    pipe_add_writer(f->pipe_id);
    return fd;
}
