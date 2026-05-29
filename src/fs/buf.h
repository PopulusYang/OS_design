
#ifndef BUF_H
#define BUF_H

#include "vfs_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC_NBUF         1024
#define BC_HASH_BUCKETS 1024
#define BC_DEV          0

#define B_VALID         0x0001U
#define B_DIRTY         0x0002U
#define B_BUSY          0x0004U

typedef struct Buf {
    int             b_dev;
    int             b_blockno;
    uint8_t         b_data[BLOCK_SIZE];
    uint16_t        b_flags;
    int             b_refcnt;
    struct Buf     *b_hash_next;
    struct Buf     *b_lru_prev;
    struct Buf     *b_lru_next;
} Buf;

void buf_init(void);
void buf_shutdown(void);

Buf *bread(int dev, int blockno);
void brelse(Buf *bp);
void bdwrite(Buf *bp);
void bwrite(Buf *bp);
int  bflush_all(void);

int read_block(int block_no, void *buf);
int write_block(int block_no, const void *buf);

#ifdef __cplusplus
}
#endif

#endif
