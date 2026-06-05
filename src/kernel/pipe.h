/*
 * pipe.h
 * 匿名管道的环形缓冲区接口。
 */
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

// 分配一个空闲管道槽
int  pipe_alloc(void);

// 读写端均关闭后回收管道槽
void pipe_free(int pipe_id);

// 增加管道读端引用计数
void pipe_add_reader(int pipe_id);
// 增加管道写端引用计数
void pipe_add_writer(int pipe_id);
// 关闭读端，无读写端时释放管道
void pipe_close_read(int pipe_id);
// 关闭写端，无读写端时释放管道
void pipe_close_write(int pipe_id);

// 从管道读取数据，缓冲区空时协作等待
int  pipe_read(int pipe_id, char *buf, int count);
// 向管道写入数据，缓冲区满时协作等待
int  pipe_write(int pipe_id, const char *buf, int count);

#ifdef __cplusplus
}
#endif

#endif
