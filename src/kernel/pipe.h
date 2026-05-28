// pipe.h —— 环形缓冲管道（进程间通信）

#ifndef PIPE_H
#define PIPE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PIPE_MAX_COUNT  16
#define PIPE_BUF_SIZE   512

typedef struct Pipe {
    uint8_t  data[PIPE_BUF_SIZE];
    int      count;
    int      read_pos;
    int      write_pos;
    int      readers;
    int      writers;
} Pipe;

// 分配管道，返回 pipe id（>=0），失败返回 -1
int  pipe_alloc(void);

// 释放管道（引用计数均为 0 时）
void pipe_free(int pipe_id);

void pipe_add_reader(int pipe_id);
void pipe_add_writer(int pipe_id);
void pipe_close_read(int pipe_id);
void pipe_close_write(int pipe_id);

// 阻塞式读写（等待时让出 CPU 给其他进程）
int  pipe_read(int pipe_id, char *buf, int count);
int  pipe_write(int pipe_id, const char *buf, int count);

#ifdef __cplusplus
}
#endif

#endif // PIPE_H
