
#include "fs/inomap.h"
#include "fs/bg.h"
#include "fs/buf.h"
#include "fs/journal.h"
#include "fs/disk_io.h"

#include <stdio.h>
#include <string.h>

#define IMAP_LOC_MAGIC   0x494D4C46U  /* IMLF */
#define IMAP_CHK_MAGIC   0x494D434BU  /* IMCK */
#define IMAP_HDR_SIZE    8
#define IMAP_LOC_MAX     ((BLOCK_SIZE - IMAP_HDR_SIZE) / 8)
#define IMAP_CHK_MAX     ((BLOCK_SIZE - IMAP_HDR_SIZE) / 4)

#define IMAP_FREE_EMPTY  0xFFFFU
#define IMAP_FREE_CAP    128

typedef struct InodeLoc {
    uint32_t il_ino;
    uint16_t il_chunk;
    uint8_t  il_slot;
    uint8_t  il_pad;
} InodeLoc;

typedef struct ChunkEnt {
    uint16_t ce_chunk;
    uint16_t ce_free;
} ChunkEnt;

typedef struct ImapIdxEnt {
    uint32_t ie_key;
    uint16_t ie_child;
    uint16_t ie_pad;
} ImapIdxEnt;

static int           g_imap_formatting;

static struct {
    uint32_t     next_ino;
    uint32_t     inode_count;
    uint32_t     inode_free_count;
    uint16_t     loc_root;
    uint16_t     loc_level;
    uint16_t     chk_root;
    uint16_t     chk_level;
    uint16_t     free_top;
    uint32_t     free_stack[IMAP_FREE_CAP];
    int          loaded;
} g_imap;

static int imap_blk_read(int blk, void *buf)
{
    if (g_imap_formatting)
        return disk_read_block(blk, buf);
    return read_block(blk, buf);
}

static int imap_leaf_read(int blk, uint32_t magic, uint16_t *cnt, void *ents, int ent_sz, int max_n)
{
    char buf[BLOCK_SIZE];
    uint32_t mg;

    if (imap_blk_read(blk, buf) != 0)
        return -1;
    memcpy(&mg, buf, sizeof(mg));
    if (mg != magic)
        return -1;
    memcpy(cnt, buf + 4, sizeof(uint16_t));
    if (*cnt > (uint16_t)max_n)
        return -1;
    if (*cnt > 0)
        memcpy(ents, buf + IMAP_HDR_SIZE, (size_t)(*cnt) * (size_t)ent_sz);
    return 0;
}

static int imap_write_blk(int blk, const void *data)
{
    if (g_imap_formatting)
        return disk_write_block(blk, data);
    return journal_write_dir_block(blk, data);
}

static int imap_leaf_write(int blk, uint32_t magic, uint16_t cnt, const void *ents, int ent_sz)
{
    char buf[BLOCK_SIZE];

    memset(buf, 0, sizeof(buf));
    memcpy(buf, &magic, sizeof(magic));
    memcpy(buf + 4, &cnt, sizeof(cnt));
    if (cnt > 0)
        memcpy(buf + IMAP_HDR_SIZE, ents, (size_t)cnt * (size_t)ent_sz);
    return imap_write_blk(blk, buf);
}

static int imap_index_read(int blk, uint16_t *cnt, uint16_t *level, ImapIdxEnt *ents)
{
    char buf[BLOCK_SIZE];
    uint32_t magic = IMAP_LOC_MAGIC;

    if (imap_blk_read(blk, buf) != 0)
        return -1;
    memcpy(&magic, buf, sizeof(magic));
    if (magic != IMAP_LOC_MAGIC && magic != IMAP_CHK_MAGIC)
        return -1;
    memcpy(cnt, buf + 4, sizeof(uint16_t));
    memcpy(level, buf + 6, sizeof(uint16_t));
    if (*cnt > (uint16_t)IMAP_LOC_MAX)
        return -1;
    if (*cnt > 0)
        memcpy(ents, buf + IMAP_HDR_SIZE, (size_t)(*cnt) * sizeof(ImapIdxEnt));
    return 0;
}

static int imap_index_write(int blk, uint32_t magic, uint16_t cnt, uint16_t level,
                            const ImapIdxEnt *ents)
{
    char buf[BLOCK_SIZE];

    memset(buf, 0, sizeof(buf));
    memcpy(buf, &magic, sizeof(magic));
    memcpy(buf + 4, &cnt, sizeof(cnt));
    memcpy(buf + 6, &level, sizeof(level));
    if (cnt > 0)
        memcpy(buf + IMAP_HDR_SIZE, ents, (size_t)cnt * sizeof(ImapIdxEnt));
    return imap_write_blk(blk, buf);
}

static int imap_alloc_meta(uint32_t parent_ino, uint16_t *out_blk)
{
    int blk;

    blk = bg_balloc_for((uint16_t)(parent_ino > 65535U ? 0U : parent_ino));
    if (blk < 0)
        return -1;
    *out_blk = (uint16_t)blk;
    return 0;
}

static int loc_find_leaf(uint32_t ino, int *leaf_blk)
{
    ImapIdxEnt idx[IMAP_LOC_MAX];
    uint16_t count;
    uint16_t dummy_level;
    int blk;
    int i;
    int cur_level;

    if (g_imap.loc_root == 0)
        return -1;

    blk = (int)g_imap.loc_root;
    cur_level = (int)g_imap.loc_level;

    while (cur_level > 0) {
        if (imap_index_read(blk, &count, &dummy_level, idx) != 0)
            return -1;
        for (i = 0; i < (int)count; i++) {
            if (idx[i].ie_key >= ino)
                break;
        }
        if (i >= (int)count)
            i = (int)count - 1;
        blk = (int)idx[i].ie_child;
        cur_level--;
    }

    *leaf_blk = blk;
    return 0;
}

static int loc_lookup(uint32_t ino, InodeLoc *out)
{
    InodeLoc ents[IMAP_LOC_MAX];
    uint16_t count;
    int leaf;
    int i;

    if (loc_find_leaf(ino, &leaf) != 0)
        return -1;
    if (imap_leaf_read(leaf, IMAP_LOC_MAGIC, &count, ents, (int)sizeof(InodeLoc),
                       IMAP_LOC_MAX) != 0)
        return -1;

    for (i = (int)count - 1; i >= 0; i--) {
        if (ents[i].il_ino == ino) {
            *out = ents[i];
            return 0;
        }
        if (ents[i].il_ino < ino)
            break;
    }
    return -1;
}

static int loc_split_leaf(int old_leaf, const InodeLoc *ents, uint16_t count)
{
    uint16_t new_leaf_blk;
    int mid = (int)count / 2;

    if (imap_alloc_meta(0, &new_leaf_blk) != 0)
        return -1;

    if (imap_leaf_write(old_leaf, IMAP_LOC_MAGIC, (uint16_t)mid, ents,
                        (int)sizeof(InodeLoc)) != 0) {
        bg_bfree((int)new_leaf_blk);
        return -1;
    }

    if (imap_leaf_write((int)new_leaf_blk, IMAP_LOC_MAGIC,
                        (uint16_t)((int)count - mid), &ents[mid],
                        (int)sizeof(InodeLoc)) != 0) {
        bg_bfree((int)new_leaf_blk);
        return -1;
    }

    if (g_imap.loc_level == 0) {
        uint16_t idx_blk;
        ImapIdxEnt idx_ents[2];

        if (imap_alloc_meta(0, &idx_blk) != 0) {
            bg_bfree((int)new_leaf_blk);
            return -1;
        }

        idx_ents[0].ie_key = ents[mid - 1].il_ino;
        idx_ents[0].ie_child = (uint16_t)old_leaf;
        idx_ents[0].ie_pad = 0;
        idx_ents[1].ie_key = 0xFFFFFFFFU;
        idx_ents[1].ie_child = new_leaf_blk;
        idx_ents[1].ie_pad = 0;

        if (imap_index_write((int)idx_blk, IMAP_LOC_MAGIC, 2, 1, idx_ents) != 0) {
            bg_bfree((int)new_leaf_blk);
            bg_bfree((int)idx_blk);
            return -1;
        }

        g_imap.loc_root = idx_blk;
        g_imap.loc_level = 1;
    } else {
        ImapIdxEnt idx[IMAP_LOC_MAX];
        uint16_t idx_count;
        uint16_t dummy;
        int found = -1;

        if (imap_index_read((int)g_imap.loc_root, &idx_count, &dummy, idx) != 0) {
            bg_bfree((int)new_leaf_blk);
            return -1;
        }
        if (idx_count >= (uint16_t)IMAP_LOC_MAX) {
            bg_bfree((int)new_leaf_blk);
            return -1;
        }

        for (int j = 0; j < (int)idx_count; j++) {
            if (idx[j].ie_child == (uint16_t)old_leaf) {
                found = j;
                break;
            }
        }
        if (found < 0) {
            bg_bfree((int)new_leaf_blk);
            return -1;
        }

        uint32_t old_key = idx[found].ie_key;
        idx[found].ie_key = ents[mid - 1].il_ino;

        int ins = found + 1;
        if (ins < (int)idx_count)
            memmove(&idx[ins + 1], &idx[ins],
                    (size_t)(idx_count - (uint16_t)ins) * sizeof(ImapIdxEnt));
        idx[ins].ie_key = old_key;
        idx[ins].ie_child = new_leaf_blk;
        idx[ins].ie_pad = 0;
        idx_count++;

        if (imap_index_write((int)g_imap.loc_root, IMAP_LOC_MAGIC,
                             idx_count, g_imap.loc_level, idx) != 0) {
            bg_bfree((int)new_leaf_blk);
            return -1;
        }
    }

    return 0;
}

static int loc_insert(const InodeLoc *loc)
{
    InodeLoc ents[IMAP_LOC_MAX];
    uint16_t count;
    int leaf;
    int i;
    int pos;

    if (g_imap.loc_root == 0) {
        if (imap_alloc_meta(0, &g_imap.loc_root) != 0)
            return -1;
        g_imap.loc_level = 0;
        ents[0] = *loc;
        return imap_leaf_write((int)g_imap.loc_root, IMAP_LOC_MAGIC, 1, ents,
                               (int)sizeof(InodeLoc));
    }

    if (loc_find_leaf(loc->il_ino, &leaf) != 0)
        return -1;
    if (imap_leaf_read(leaf, IMAP_LOC_MAGIC, &count, ents, (int)sizeof(InodeLoc),
                       IMAP_LOC_MAX) != 0)
        return -1;

    for (i = 0; i < (int)count; i++) {
        if (ents[i].il_ino == loc->il_ino)
            return 0;
    }

    if (count >= (uint16_t)IMAP_LOC_MAX) {
        if (loc_split_leaf(leaf, ents, count) != 0)
            return -1;
        return loc_insert(loc);
    }

    pos = (int)count;
    for (i = 0; i < (int)count; i++) {
        if (loc->il_ino < ents[i].il_ino) {
            pos = i;
            break;
        }
    }
    if (pos < (int)count)
        memmove(&ents[pos + 1], &ents[pos], (size_t)(count - (uint16_t)pos) * sizeof(InodeLoc));
    ents[pos] = *loc;
    count++;
    return imap_leaf_write(leaf, IMAP_LOC_MAGIC, count, ents, (int)sizeof(InodeLoc));
}

static int chk_get(uint16_t chunk, uint16_t *mask_out)
{
    ChunkEnt ents[IMAP_CHK_MAX];
    uint16_t count;
    int i;

    if (g_imap.chk_root == 0)
        return -1;
    if (g_imap.chk_level != 0)
        return -1;

    if (imap_leaf_read((int)g_imap.chk_root, IMAP_CHK_MAGIC, &count, ents,
                       (int)sizeof(ChunkEnt), IMAP_CHK_MAX) != 0)
        return -1;

    for (i = 0; i < (int)count; i++) {
        if (ents[i].ce_chunk == chunk) {
            *mask_out = ents[i].ce_free;
            return 0;
        }
    }
    return -1;
}

static int chk_set(uint16_t chunk, uint16_t mask)
{
    ChunkEnt ents[IMAP_CHK_MAX];
    uint16_t count;
    int i;
    int pos;

    if (g_imap.chk_root == 0) {
        if (imap_alloc_meta(0, &g_imap.chk_root) != 0)
            return -1;
        g_imap.chk_level = 0;
        ents[0].ce_chunk = chunk;
        ents[0].ce_free = mask;
        return imap_leaf_write((int)g_imap.chk_root, IMAP_CHK_MAGIC, 1, ents,
                               (int)sizeof(ChunkEnt));
    }

    if (imap_leaf_read((int)g_imap.chk_root, IMAP_CHK_MAGIC, &count, ents,
                       (int)sizeof(ChunkEnt), IMAP_CHK_MAX) != 0)
        return -1;

    for (i = 0; i < (int)count; i++) {
        if (ents[i].ce_chunk == chunk) {
            ents[i].ce_free = mask;
            return imap_leaf_write((int)g_imap.chk_root, IMAP_CHK_MAGIC, count,
                                   ents, (int)sizeof(ChunkEnt));
        }
    }

    if (count >= (uint16_t)IMAP_CHK_MAX)
        return -1;

    pos = (int)count;
    for (i = 0; i < (int)count; i++) {
        if (chunk < ents[i].ce_chunk) {
            pos = i;
            break;
        }
    }
    if (pos < (int)count)
        memmove(&ents[pos + 1], &ents[pos], (size_t)(count - (uint16_t)pos) * sizeof(ChunkEnt));
    ents[pos].ce_chunk = chunk;
    ents[pos].ce_free = mask;
    count++;
    return imap_leaf_write((int)g_imap.chk_root, IMAP_CHK_MAGIC, count, ents,
                           (int)sizeof(ChunkEnt));
}

static int chunk_find_free_slot(int bg, uint16_t *chunk_out, uint8_t *slot_out)
{
    ChunkEnt ents[IMAP_CHK_MAX];
    uint16_t count;
    int i;

    if (g_imap.chk_root == 0)
        return -1;
    if (imap_leaf_read((int)g_imap.chk_root, IMAP_CHK_MAGIC, &count, ents,
                       (int)sizeof(ChunkEnt), IMAP_CHK_MAX) != 0)
        return -1;

    for (i = 0; i < (int)count; i++) {
        if (ents[i].ce_free == 0)
            continue;
        if (bg >= 0 && bg_from_block((int)ents[i].ce_chunk) != bg)
            continue;
        for (int s = 0; s < INODES_PER_CHUNK; s++) {
            if (ents[i].ce_free & (uint16_t)(1U << s)) {
                *chunk_out = ents[i].ce_chunk;
                *slot_out = (uint8_t)s;
                return 0;
            }
        }
    }
    return -1;
}

static int chunk_alloc_new(uint32_t parent_ino, uint16_t *chunk_out, uint8_t *slot_out)
{
    int blk;
    char zero[BLOCK_SIZE];
    uint16_t mask;

    blk = bg_balloc_for((uint16_t)(parent_ino > 65535U ? 0U : parent_ino));
    if (blk < 0)
        return -1;

    memset(zero, 0, sizeof(zero));
    if (imap_write_blk(blk, zero) != 0) {
        bg_bfree(blk);
        return -1;
    }

    mask = (INODES_PER_CHUNK >= 16)
               ? (uint16_t)0xFFFFU
               : (uint16_t)((1U << INODES_PER_CHUNK) - 1U);
    *slot_out = 0;
    mask &= (uint16_t)~(1U << 0);
    *chunk_out = (uint16_t)blk;

    if (chk_set(*chunk_out, mask) != 0) {
        bg_bfree(blk);
        return -1;
    }
    return 0;
}

int inomap_is_chunk_block(int blockno)
{
    ChunkEnt ents[IMAP_CHK_MAX];
    uint16_t count;
    int i;

    if (!g_imap.loaded || g_imap.chk_root == 0)
        return 0;
    if (imap_leaf_read((int)g_imap.chk_root, IMAP_CHK_MAGIC, &count, ents,
                       (int)sizeof(ChunkEnt), IMAP_CHK_MAX) != 0)
        return 0;

    for (i = 0; i < (int)count; i++) {
        if ((int)ents[i].ce_chunk == blockno)
            return 1;
    }
    return 0;
}

int inomap_lookup(uint32_t ino, int *out_blk, int *out_off)
{
    InodeLoc loc;

    if (!g_imap.loaded || out_blk == NULL || out_off == NULL || ino == 0)
        return -1;
    if (loc_lookup(ino, &loc) != 0)
        return -1;

    *out_blk = (int)loc.il_chunk;
    *out_off = (int)loc.il_slot * DISK_INODE_SIZE;
    return 0;
}

int inomap_read_disk_inode(uint32_t ino, DiskINode *out)
{
    char buf[BLOCK_SIZE];
    int blk;
    int off;

    if (out == NULL)
        return -1;
    if (inomap_lookup(ino, &blk, &off) != 0)
        return -1;
    if (read_block(blk, buf) != 0)
        return -1;
    memcpy(out, buf + off, (size_t)DISK_INODE_SIZE);
    return 0;
}

int inomap_write_disk_inode(uint32_t ino, const DiskINode *inode)
{
    char buf[BLOCK_SIZE];
    int blk;
    int off;

    if (inode == NULL)
        return -1;
    if (inomap_lookup(ino, &blk, &off) != 0)
        return -1;
    if (g_imap_formatting) {
        if (disk_read_block(blk, buf) != 0)
            return -1;
    } else if (read_block(blk, buf) != 0) {
        return -1;
    }
    memcpy(buf + off, inode, (size_t)DISK_INODE_SIZE);
    if (g_imap_formatting)
        return disk_write_block(blk, buf);
    return journal_write_metadata(blk, buf);
}

static void imap_push_free(uint32_t ino)
{
    if (g_imap.free_top >= IMAP_FREE_CAP)
        return;
    g_imap.free_stack[g_imap.free_top++] = ino;
    g_imap.inode_free_count++;
}

static int imap_pop_free(uint32_t *ino_out)
{
    if (g_imap.free_top == 0)
        return -1;
    *ino_out = g_imap.free_stack[--g_imap.free_top];
    if (g_imap.inode_free_count > 0)
        g_imap.inode_free_count--;
    return 0;
}

int inomap_ialloc_for(uint32_t parent_ino)
{
    InodeLoc loc;
    uint16_t chunk;
    uint8_t  slot;
    uint16_t mask;
    uint32_t ino;
    int bg;
    DiskINode din;

    if (!g_imap.loaded)
        return -1;

    if (imap_pop_free(&ino) == 0) {
        InodeLoc rloc;
        if (loc_lookup(ino, &rloc) == 0) {
            uint16_t rmask;
            if (chk_get(rloc.il_chunk, &rmask) == 0) {
                rmask &= (uint16_t)~(1U << rloc.il_slot);
                chk_set(rloc.il_chunk, rmask);
            }
        }
        memset(&din, 0, sizeof(din));
        if (inomap_write_disk_inode(ino, &din) != 0)
            return -1;
        g_imap.inode_count++;
        return (int)ino;
    }

    bg = -1;
    if (parent_ino > 0) {
        InodeLoc ploc;
        if (loc_lookup(parent_ino, &ploc) == 0)
            bg = bg_from_block((int)ploc.il_chunk);
    }

    if (chunk_find_free_slot(bg, &chunk, &slot) != 0) {
        if (chunk_alloc_new(parent_ino, &chunk, &slot) != 0)
            return -1;
    } else {
        if (chk_get(chunk, &mask) != 0)
            return -1;
        mask &= (uint16_t)~(1U << slot);
        if (chk_set(chunk, mask) != 0)
            return -1;
    }

    ino = g_imap.next_ino++;
    loc.il_ino = ino;
    loc.il_chunk = chunk;
    loc.il_slot = slot;
    loc.il_pad = 0;

    if (loc_insert(&loc) != 0)
        return -1;

    memset(&din, 0, sizeof(din));
    if (inomap_write_disk_inode(ino, &din) != 0)
        return -1;

    g_imap.inode_count++;
    return (int)ino;
}

int inomap_ifree(uint32_t ino)
{
    InodeLoc loc;
    uint16_t mask;
    DiskINode din;

    if (!g_imap.loaded || ino == 0 || ino == ROOT_INODE_NO)
        return -1;
    if (loc_lookup(ino, &loc) != 0)
        return -1;

    memset(&din, 0, sizeof(din));
    if (inomap_write_disk_inode(ino, &din) != 0)
        return -1;

    if (chk_get(loc.il_chunk, &mask) == 0) {
        mask |= (uint16_t)(1U << loc.il_slot);
        chk_set(loc.il_chunk, mask);
    }

    imap_push_free(ino);
    if (g_imap.inode_count > 0)
        g_imap.inode_count--;
    return 0;
}

int inomap_format_init(uint16_t root_dir_block)
{
    uint16_t chunk_blk;
    InodeLoc loc;
    DiskINode root;

    g_imap_formatting = 1;
    memset(&g_imap, 0, sizeof(g_imap));
    g_imap.next_ino = 2;
    g_imap.inode_count = 1;
    g_imap.inode_free_count = 0;
    g_imap.free_top = 0;
    g_imap.loc_root = 0;
    g_imap.chk_root = 0;
    g_imap.loc_level = 0;
    g_imap.chk_level = 0;

    if (chunk_alloc_new(0, &chunk_blk, &loc.il_slot) != 0) {
        g_imap_formatting = 0;
        return -1;
    }

    loc.il_ino = ROOT_INODE_NO;
    loc.il_chunk = chunk_blk;
    loc.il_pad = 0;
    if (loc_insert(&loc) != 0) {
        g_imap_formatting = 0;
        return -1;
    }

    memset(&root, 0, sizeof(root));
    root.d_mode = (uint16_t)(IFDIR | IREAD | IWRITE | IEXEC);
    root.d_nlink = 2;
    root.d_size = 2 * DIR_ENTRY_SIZE;
    root.d_extent.e_lblk = 0;
    root.d_extent.e_pblk = (uint16_t)root_dir_block;
    root.d_extent.e_len = 1;

    {
        char buf[BLOCK_SIZE];
        int off = (int)loc.il_slot * DISK_INODE_SIZE;

        if (disk_read_block((int)chunk_blk, buf) != 0 ||
            off + DISK_INODE_SIZE > BLOCK_SIZE) {
            g_imap_formatting = 0;
            return -1;
        }
        memcpy(buf + off, &root, (size_t)DISK_INODE_SIZE);
        if (disk_write_block((int)chunk_blk, buf) != 0) {
            g_imap_formatting = 0;
            return -1;
        }
    }

    g_imap.loaded = 1;
    g_imap_formatting = 0;
    return 0;
}

int inomap_load(const SuperBlock *sb)
{
    if (sb == NULL)
        return -1;

    g_imap.next_ino = sb->s_inode_next;
    g_imap.inode_count = sb->s_inode_total;
    g_imap.inode_free_count = sb->s_inode_free_count;
    g_imap.loc_root = sb->s_imap_loc_root;
    g_imap.loc_level = sb->s_imap_loc_level;
    g_imap.chk_root = sb->s_imap_chk_root;
    g_imap.chk_level = sb->s_imap_chk_level;
    g_imap.free_top = sb->s_imap_free_top;
    if (g_imap.free_top > IMAP_FREE_CAP)
        g_imap.free_top = 0;
    memcpy(g_imap.free_stack, sb->s_imap_free_stack,
           (size_t)g_imap.free_top * sizeof(uint32_t));
    g_imap.loaded = 1;
    return 0;
}

int inomap_sync(SuperBlock *sb)
{
    if (sb == NULL || !g_imap.loaded)
        return -1;

    sb->s_inode_next = g_imap.next_ino;
    sb->s_inode_total = g_imap.inode_count;
    sb->s_inode_free_count = g_imap.inode_free_count;
    sb->s_imap_loc_root = g_imap.loc_root;
    sb->s_imap_loc_level = g_imap.loc_level;
    sb->s_imap_chk_root = g_imap.chk_root;
    sb->s_imap_chk_level = g_imap.chk_level;
    sb->s_imap_free_top = g_imap.free_top;
    if (g_imap.free_top > 0)
        memcpy(sb->s_imap_free_stack, g_imap.free_stack,
               (size_t)g_imap.free_top * sizeof(uint32_t));
    return 0;
}

void inomap_debug_print(void)
{
    printf("  ── Dynamic Inode Map ────────────────────────────\n");
    printf("  Next inode:         %u\n", (unsigned)g_imap.next_ino);
    printf("  Inodes in use:      %u\n", (unsigned)g_imap.inode_count);
    printf("  Inodes free (pool): %u\n", (unsigned)g_imap.inode_free_count);
    printf("  Loc tree root:      %u (level %u)\n",
           (unsigned)g_imap.loc_root, (unsigned)g_imap.loc_level);
    printf("  Chunk tree root:    %u (level %u)\n",
           (unsigned)g_imap.chk_root, (unsigned)g_imap.chk_level);
    printf("  Max per chunk:      %d inodes\n", INODES_PER_CHUNK);
    printf("\n");
}
