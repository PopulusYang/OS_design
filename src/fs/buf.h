/*
 * buf.h
 * 全局块缓存：1024 槽 LRU 加哈希，提供 bread、bwrite 与 read_block 接口。
 */
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
    int         b_dev;
    int         b_blockno;
    uint8_t     b_data[BLOCK_SIZE];
    uint16_t    b_flags;
    int         b_refcnt;
    struct Buf *b_hash_next;
    struct Buf *b_lru_prev;
    struct Buf *b_lru_next;
} Buf;

// 初始化块缓存哈希表与 LRU 链表
void buf_init(void);
// 关闭缓存并刷写所有脏块
void buf_shutdown(void);
// 获取块缓存项，未命中则从磁盘读入
Buf *bread(int dev, int blockno);
// 减少缓存块引用计数
void brelse(Buf *bp);
// 标记缓存块为脏，延迟写回
void bdwrite(Buf *bp);
// 立即把缓存块写回磁盘
void bwrite(Buf *bp);
// 刷写全部脏缓存块
int bflush_all(void);
// 清空缓存哈希表，保留槽位结构
void bcache_invalidate(void);
// 经缓存读取一块，未命中则走 bread
int read_block(int block_no, void *buf);
// 经缓存写入一块并标记脏
int write_block(int block_no, const void *buf);

#ifdef __cplusplus
}
#endif

#endif
