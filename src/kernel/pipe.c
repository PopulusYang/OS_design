/*
 * pipe.c
 * 管道分配、读写与读写端引用计数。
 */
#include "kernel/pipe.h"
#include "kernel/scheduler.h"
#include "kernel/process.h"
#include "kernel_shared.h"
#include <string.h>

extern KernelShared *g_kernel;

// 按 id 取有效管道对象
static Pipe *pipe_get(int pipe_id)
{
    if (g_kernel == NULL || pipe_id < 0 || pipe_id >= PIPE_MAX_COUNT) return NULL;
    if (!g_kernel->pipe_used[pipe_id]) return NULL;
    return &g_kernel->pipes[pipe_id];
}

// 分配一个空闲管道槽
int pipe_alloc(void)
{
    if (g_kernel == NULL) return -1;
    for (int i = 0; i < PIPE_MAX_COUNT; i++) {
        if (!g_kernel->pipe_used[i]) {
            memset(&g_kernel->pipes[i], 0, sizeof(Pipe));
            g_kernel->pipe_used[i] = 1;
            return i;
        }
    }
    return -1;
}

// 读写端均关闭后回收管道槽
void pipe_free(int pipe_id)
{
    Pipe *p = pipe_get(pipe_id);
    if (p == NULL) return;
    if (p->readers > 0 || p->writers > 0) return;
    memset(p, 0, sizeof(Pipe));
    g_kernel->pipe_used[pipe_id] = 0;
}

// 增加管道读端引用计数
void pipe_add_reader(int pipe_id)
{
    Pipe *p = pipe_get(pipe_id);
    if (p) p->readers++;
}

// 增加管道写端引用计数
void pipe_add_writer(int pipe_id)
{
    Pipe *p = pipe_get(pipe_id);
    if (p) p->writers++;
}

// 关闭读端，无读写端时释放管道
void pipe_close_read(int pipe_id)
{
    Pipe *p = pipe_get(pipe_id);
    if (p == NULL) return;
    if (p->readers > 0) p->readers--;
    if (p->readers == 0 && p->writers == 0) pipe_free(pipe_id);
}

// 关闭写端，无读写端时释放管道
void pipe_close_write(int pipe_id)
{
    Pipe *p = pipe_get(pipe_id);
    if (p == NULL) return;
    if (p->writers > 0) p->writers--;
    if (p->readers == 0 && p->writers == 0) pipe_free(pipe_id);
}

// 尝试从管道读出一批数据，不阻塞
static int pipe_try_read(Pipe *p, char *buf, int count)
{
    if (p->count == 0) return 0;
    int n = count < p->count ? count : p->count;
    for (int i = 0; i < n; i++) {
        buf[i] = (char)p->data[p->read_pos];
        p->read_pos = (p->read_pos + 1) % PIPE_BUF_SIZE;
    }
    p->count -= n;
    return n;
}

// 尝试向管道写入一批数据，不阻塞
static int pipe_try_write(Pipe *p, const char *buf, int count)
{
    int space = PIPE_BUF_SIZE - p->count;
    if (space == 0) return 0;
    int n = count < space ? count : space;
    for (int i = 0; i < n; i++) {
        p->data[p->write_pos] = (uint8_t)buf[i];
        p->write_pos = (p->write_pos + 1) % PIPE_BUF_SIZE;
    }
    p->count += n;
    return n;
}

// 从管道读取数据，缓冲区空时协作等待
int pipe_read(int pipe_id, char *buf, int count)
{
    if (buf == NULL || count <= 0) return -1;
    Pipe *p = pipe_get(pipe_id);
    if (p == NULL) return -1;

    for (int spin = 0; spin < 100000; spin++) {
        int n = pipe_try_read(p, buf, count);
        if (n > 0) return n;
        if (p->writers == 0) return 0;
        sched_cooperate();
        if (p->count == 0 && p->writers > 0 && sched_ready_count() == 0)
            return -1;
    }
    return -1;
}

// 向管道写入数据，缓冲区满时协作等待
int pipe_write(int pipe_id, const char *buf, int count)
{
    if (buf == NULL || count <= 0) return -1;
    Pipe *p = pipe_get(pipe_id);
    if (p == NULL) return -1;

    int total = 0;
    while (total < count) {
        int n = pipe_try_write(p, buf + total, count - total);
        if (n > 0) {
            total += n;
            continue;
        }
        if (p->readers == 0) return -1;
        sched_cooperate();
        if (p->count == PIPE_BUF_SIZE && p->readers > 0 && sched_ready_count() == 0)
            return total > 0 ? total : -1;
    }
    return total;
}
