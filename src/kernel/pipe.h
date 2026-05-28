

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


int  pipe_alloc(void);


void pipe_free(int pipe_id);

void pipe_add_reader(int pipe_id);
void pipe_add_writer(int pipe_id);
void pipe_close_read(int pipe_id);
void pipe_close_write(int pipe_id);


int  pipe_read(int pipe_id, char *buf, int count);
int  pipe_write(int pipe_id, const char *buf, int count);

#ifdef __cplusplus
}
#endif

#endif 
