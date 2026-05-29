
#include "fs/journal.h"
#include "fs/bg.h"
#include "fs/buf.h"
#include "fs/disk_io.h"

#include <string.h>

typedef struct JournalHeader {
    uint32_t jh_magic;
    uint32_t jh_seq;
    uint32_t jh_nlogs;
    uint32_t jh_committed;
} JournalHeader;

typedef struct JournalLogRec {
    uint32_t lr_type;
    uint32_t lr_block;
    uint8_t  lr_data[BLOCK_SIZE];
} JournalLogRec;

static struct {
    int      active;
    uint32_t seq;
    int      nlogs;
    struct {
        int      blockno;
        uint8_t  data[BLOCK_SIZE];
    } logs[JOURNAL_MAX_LOG];
} g_txn;

static int journal_header_block(void)
{
    return JOURNAL_ZONE_START;
}

static int journal_data_block(int slot)
{
    return JOURNAL_ZONE_START + 1 + slot;
}

int journal_is_metadata_block(int blockno)
{
    if (blockno == SUPERBLOCK_BLOCKNO || blockno == BOOT_BLOCKNO)
        return 1;
    if (bg_is_inode_disk_block(blockno))
        return 1;
    if (bg_is_anchor_block(blockno))
        return 1;
    return 0;
}

int journal_init_format(void)
{
    JournalHeader hdr;
    char zero[BLOCK_SIZE];

    memset(&hdr, 0, sizeof(hdr));
    hdr.jh_magic = JOURNAL_MAGIC;
    hdr.jh_seq = 0;
    hdr.jh_nlogs = 0;
    hdr.jh_committed = 1;

    if (disk_write_block(journal_header_block(), &hdr) != 0)
        return -1;

    memset(zero, 0, sizeof(zero));
    for (int i = 0; i < JOURNAL_ZONE_BLOCKS - 1; i++) {
        if (disk_write_block(journal_data_block(i), zero) != 0)
            return -1;
    }
    return 0;
}

static int journal_read_header(JournalHeader *hdr)
{
    if (disk_read_block(journal_header_block(), hdr) != 0)
        return -1;
    if (hdr->jh_magic != JOURNAL_MAGIC)
        return -1;
    return 0;
}

static int journal_apply_log(int blockno, const void *data)
{
    return disk_write_block(blockno, data);
}

int journal_replay(void)
{
    JournalHeader hdr;
    JournalLogRec rec;
    int i;

    if (journal_read_header(&hdr) != 0)
        return 0;

    if (hdr.jh_committed == 0 || hdr.jh_nlogs == 0)
        return 0;

    for (i = 0; i < (int)hdr.jh_nlogs && i < JOURNAL_MAX_LOG; i++) {
        if (disk_read_block(journal_data_block(i), &rec) != 0)
            return -1;
        if (rec.lr_type != JTXN_LOG)
            continue;
        if (journal_apply_log((int)rec.lr_block, rec.lr_data) != 0)
            return -1;
    }

    hdr.jh_committed = 1;
    hdr.jh_nlogs = 0;
    return disk_write_block(journal_header_block(), &hdr);
}

int journal_begin(void)
{
    if (g_txn.active)
        journal_abort();
    g_txn.active = 1;
    g_txn.nlogs = 0;
    return 0;
}

int journal_log_block(int blockno, const void *data)
{
    if (!g_txn.active || data == NULL)
        return -1;
    if (g_txn.nlogs >= JOURNAL_MAX_LOG)
        return -1;
    g_txn.logs[g_txn.nlogs].blockno = blockno;
    memcpy(g_txn.logs[g_txn.nlogs].data, data, BLOCK_SIZE);
    g_txn.nlogs++;
    return 0;
}

void journal_abort(void)
{
    g_txn.active = 0;
    g_txn.nlogs = 0;
}

int journal_commit(void)
{
    JournalHeader hdr;
    JournalLogRec rec;
    int i;

    if (!g_txn.active)
        return -1;

    if (journal_read_header(&hdr) != 0)
        return -1;
    hdr.jh_seq++;
    hdr.jh_nlogs = (uint32_t)g_txn.nlogs;
    hdr.jh_committed = 0;
    if (disk_write_block(journal_header_block(), &hdr) != 0)
        return -1;

    for (i = 0; i < g_txn.nlogs; i++) {
        memset(&rec, 0, sizeof(rec));
        rec.lr_type = JTXN_LOG;
        rec.lr_block = (uint32_t)g_txn.logs[i].blockno;
        memcpy(rec.lr_data, g_txn.logs[i].data, BLOCK_SIZE);
        if (disk_write_block(journal_data_block(i), &rec) != 0) {
            journal_abort();
            return -1;
        }
    }

    hdr.jh_committed = 1;
    if (disk_write_block(journal_header_block(), &hdr) != 0) {
        journal_abort();
        return -1;
    }

    for (i = 0; i < g_txn.nlogs; i++) {
        if (journal_apply_log(g_txn.logs[i].blockno, g_txn.logs[i].data) != 0) {
            journal_abort();
            return -1;
        }
        Buf *bp = bread(BC_DEV, g_txn.logs[i].blockno);
        if (bp) {
            memcpy(bp->b_data, g_txn.logs[i].data, BLOCK_SIZE);
            bp->b_flags |= B_VALID;
            bp->b_flags &= (uint16_t)~B_DIRTY;
            brelse(bp);
        }
    }

    journal_abort();
    return 0;
}

static int journal_write_logged(int blockno, const void *data)
{
    if (!g_txn.active) {
        if (journal_begin() != 0)
            return -1;
    }
    if (journal_log_block(blockno, data) != 0) {
        journal_abort();
        return -1;
    }
    return journal_commit();
}

int journal_write_metadata(int blockno, const void *data)
{
    if (data == NULL)
        return -1;

    if (!journal_is_metadata_block(blockno))
        return write_block(blockno, data);

    return journal_write_logged(blockno, data);
}

int journal_write_dir_block(int blockno, const void *data)
{
    if (data == NULL)
        return -1;

    if (!bg_block_in_data_zone(blockno) || blockno >= JOURNAL_ZONE_START)
        return write_block(blockno, data);

    return journal_write_logged(blockno, data);
}
