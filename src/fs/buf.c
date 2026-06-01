
#include "fs/buf.h"
#include "fs/disk_io.h"

#include <string.h>
#include <stdlib.h>

static Buf           g_bufs[BC_NBUF];
static Buf          *g_hash[BC_HASH_BUCKETS];
static Buf           g_lru_head;
static Buf           g_lru_tail;
static int           g_buf_inited;

static uint32_t buf_hash_key(int dev, int blockno)
{
    return ((uint32_t)(unsigned)dev * 2654435761U) ^ (uint32_t)(unsigned)blockno;
}

static void lru_remove(Buf *bp)
{
    if (bp->b_lru_prev) bp->b_lru_prev->b_lru_next = bp->b_lru_next;
    else g_lru_head.b_lru_next = bp->b_lru_next;
    if (bp->b_lru_next) bp->b_lru_next->b_lru_prev = bp->b_lru_prev;
    else g_lru_tail.b_lru_prev = bp->b_lru_prev;
    bp->b_lru_prev = bp->b_lru_next = NULL;
}

static void lru_insert_mru(Buf *bp)
{
    bp->b_lru_prev = &g_lru_head;
    bp->b_lru_next = g_lru_head.b_lru_next;
    if (g_lru_head.b_lru_next) g_lru_head.b_lru_next->b_lru_prev = bp;
    else g_lru_tail.b_lru_prev = bp;
    g_lru_head.b_lru_next = bp;
}

static Buf *buf_hash_find(int dev, int blockno)
{
    uint32_t key = buf_hash_key(dev, blockno) % BC_HASH_BUCKETS;
    for (Buf *bp = g_hash[key]; bp != NULL; bp = bp->b_hash_next) {
        if (bp->b_dev == dev && bp->b_blockno == blockno)
            return bp;
    }
    return NULL;
}

static void buf_hash_insert(Buf *bp)
{
    uint32_t key = buf_hash_key(bp->b_dev, bp->b_blockno) % BC_HASH_BUCKETS;
    bp->b_hash_next = g_hash[key];
    g_hash[key] = bp;
}

static void buf_hash_remove(Buf *bp)
{
    uint32_t key = buf_hash_key(bp->b_dev, bp->b_blockno) % BC_HASH_BUCKETS;
    Buf **pp = &g_hash[key];
    for (Buf *p = *pp; p != NULL; pp = &p->b_hash_next, p = *pp) {
        if (p == bp) {
            *pp = bp->b_hash_next;
            bp->b_hash_next = NULL;
            return;
        }
    }
}

static int buf_invalidate(Buf *bp)
{
    if (bp->b_refcnt > 0)
        return -1;
    if (bp->b_flags & B_DIRTY) {
        if (disk_write_block(bp->b_blockno, bp->b_data) != 0)
            return -1;
        bp->b_flags &= (uint16_t)~B_DIRTY;
    }
    buf_hash_remove(bp);
    lru_remove(bp);
    bp->b_dev = -1;
    bp->b_blockno = -1;
    bp->b_flags = 0;
    lru_insert_mru(bp);
    return 0;
}

static Buf *buf_alloc_slot(int dev, int blockno)
{
    Buf *bp;

    for (Buf *p = g_lru_tail.b_lru_prev; p != &g_lru_head; p = p->b_lru_prev) {
        if (p->b_refcnt == 0 && !(p->b_flags & B_BUSY)) {
            if (p->b_dev >= 0)
                buf_invalidate(p);
            p->b_dev = dev;
            p->b_blockno = blockno;
            p->b_flags = 0;
            buf_hash_insert(p);
            lru_remove(p);
            lru_insert_mru(p);
            return p;
        }
    }
    return NULL;
}

void buf_init(void)
{
    int i;

    if (g_buf_inited)
        return;

    memset(g_bufs, 0, sizeof(g_bufs));
    memset(g_hash, 0, sizeof(g_hash));
    g_lru_head.b_lru_next = &g_lru_tail;
    g_lru_tail.b_lru_prev = &g_lru_head;

    for (i = 0; i < BC_NBUF; i++) {
        g_bufs[i].b_dev = -1;
        g_bufs[i].b_blockno = -1;
        lru_insert_mru(&g_bufs[i]);
    }
    g_buf_inited = 1;
}

void buf_shutdown(void)
{
    if (!g_buf_inited)
        return;
    bflush_all();
    g_buf_inited = 0;
}

Buf *bread(int dev, int blockno)
{
    Buf *bp;

    if (!g_buf_inited)
        buf_init();
    if (blockno < 0 || blockno >= TOTAL_DISK_BLOCKS)
        return NULL;

    bp = buf_hash_find(dev, blockno);
    if (bp != NULL) {
        if (!(bp->b_flags & B_VALID)) {
            if (disk_read_block(blockno, bp->b_data) != 0)
                return NULL;
            bp->b_flags |= B_VALID;
        }
        bp->b_refcnt++;
        bp->b_flags |= B_BUSY;
        lru_remove(bp);
        lru_insert_mru(bp);
        return bp;
    }

    bp = buf_alloc_slot(dev, blockno);
    if (bp == NULL)
        return NULL;

    if (disk_read_block(blockno, bp->b_data) != 0) {
        buf_hash_remove(bp);
        bp->b_dev = -1;
        bp->b_blockno = -1;
        lru_insert_mru(bp);
        return NULL;
    }

    bp->b_flags = B_VALID | B_BUSY;
    bp->b_refcnt = 1;
    return bp;
}

void brelse(Buf *bp)
{
    if (bp == NULL)
        return;
    if (bp->b_refcnt <= 0)
        return;
    bp->b_refcnt--;
    if (bp->b_refcnt == 0)
        bp->b_flags &= (uint16_t)~B_BUSY;
}

void bdwrite(Buf *bp)
{
    if (bp == NULL)
        return;
    bp->b_flags |= B_DIRTY;
    brelse(bp);
}

void bwrite(Buf *bp)
{
    if (bp == NULL)
        return;
    if (bp->b_flags & B_DIRTY) {
        disk_write_block(bp->b_blockno, bp->b_data);
        bp->b_flags &= (uint16_t)~B_DIRTY;
    }
    bp->b_flags |= B_VALID;
    brelse(bp);
}

int bflush_all(void)
{
    int rc = 0;

    if (!g_buf_inited)
        return 0;

    for (int i = 0; i < BC_NBUF; i++) {
        Buf *bp = &g_bufs[i];
        if (bp->b_dev < 0)
            continue;
        if (!(bp->b_flags & B_DIRTY))
            continue;
        if (bp->b_refcnt > 0)
            continue;
        if (disk_write_block(bp->b_blockno, bp->b_data) != 0)
            rc = -1;
        else
            bp->b_flags &= (uint16_t)~B_DIRTY;
    }
    return rc;
}

void bcache_invalidate(void)
{
    if (!g_buf_inited)
        return;
    for (int i = 0; i < BC_NBUF; i++) {
        Buf *bp = &g_bufs[i];
        if (bp->b_refcnt > 0 || (bp->b_flags & B_BUSY))
            continue;
        bp->b_flags &= (uint16_t)~B_VALID;
    }
}

int read_block(int block_no, void *buf)
{
    Buf *bp = bread(BC_DEV, block_no);
    if (bp == NULL)
        return -1;
    memcpy(buf, bp->b_data, BLOCK_SIZE);
    brelse(bp);
    return 0;
}

int write_block(int block_no, const void *buf)
{
    Buf *bp = bread(BC_DEV, block_no);
    if (bp == NULL)
        return -1;
    memcpy(bp->b_data, buf, BLOCK_SIZE);
    bdwrite(bp);
    return 0;
}
