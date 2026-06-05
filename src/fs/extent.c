/*
 * extent.c
 * 文件块映射：内联 Extent、单叶节点或 B+ 树，分配时合并相邻区段。
 */
#include "fs/extent.h"
#include "fs/allocator.h"
#include "fs/buf.h"
#include "fs/journal.h"

#include <string.h>

#define EXT_LEAF_MAGIC  0x58544C46U
#define EXT_IDX_MAGIC   0x58544958U

#define EXT_HDR_SIZE    8
#define EXT_MAX_LEAF    ((BLOCK_SIZE - EXT_HDR_SIZE) / (int)sizeof(Extent))
#define EXT_MAX_INDEX   ((BLOCK_SIZE - EXT_HDR_SIZE) / 8)

typedef struct ExtIndexEntry {
    uint32_t ie_key;
    uint16_t ie_child;
    uint16_t ie_pad;
} ExtIndexEntry;

// 判断逻辑块号是否落在某条 Extent 范围内
static int extent_in_range(const Extent *e, uint32_t lblk)
{
    if (e->e_len == 0)
        return 0;
    return lblk >= e->e_lblk && lblk < e->e_lblk + (uint32_t)e->e_len;
}

// 根据 Extent 和逻辑块号计算物理块号
static uint16_t extent_phys_at(const Extent *e, uint32_t lblk)
{
    return (uint16_t)(e->e_pblk + (lblk - e->e_lblk));
}

// 从 Extent 叶节点块读取映射数组
static int leaf_read(int blk, uint16_t *count_out, Extent *ents, int max_ents)
{
    char buf[BLOCK_SIZE];
    uint32_t magic;
    uint16_t count;

    if (read_block(blk, buf) != 0)
        return -1;

    memcpy(&magic, buf, sizeof(magic));
    memcpy(&count, buf + 4, sizeof(count));
    if (magic != EXT_LEAF_MAGIC)
        return -1;
    if (count > (uint16_t)max_ents)
        return -1;

    if (count > 0)
        memcpy(ents, buf + EXT_HDR_SIZE, (size_t)count * sizeof(Extent));
    if (count_out)
        *count_out = count;
    return 0;
}

// 把 Extent 映射数组写入叶节点块
static int leaf_write(int blk, uint16_t count, const Extent *ents)
{
    char buf[BLOCK_SIZE];

    if (count > (uint16_t)EXT_MAX_LEAF)
        return -1;

    memset(buf, 0, sizeof(buf));
    {
        uint32_t magic = EXT_LEAF_MAGIC;
        memcpy(buf, &magic, sizeof(magic));
        memcpy(buf + 4, &count, sizeof(count));
    }
    if (count > 0)
        memcpy(buf + EXT_HDR_SIZE, ents, (size_t)count * sizeof(Extent));
    return write_block(blk, buf);
}

static int index_read(int blk, uint16_t *count_out, uint16_t *level_out,
                      ExtIndexEntry *ents, int max_ents)
{
    char buf[BLOCK_SIZE];
    uint32_t magic;
    uint16_t count;
    uint16_t level;

    if (read_block(blk, buf) != 0)
        return -1;

    memcpy(&magic, buf, sizeof(magic));
    memcpy(&count, buf + 4, sizeof(count));
    memcpy(&level, buf + 6, sizeof(level));
    if (magic != EXT_IDX_MAGIC)
        return -1;
    if (count > (uint16_t)max_ents)
        return -1;

    if (count > 0)
        memcpy(ents, buf + EXT_HDR_SIZE, (size_t)count * sizeof(ExtIndexEntry));
    if (count_out)
        *count_out = count;
    if (level_out)
        *level_out = level;
    return 0;
}

static int index_write(int blk, uint16_t count, uint16_t level,
                       const ExtIndexEntry *ents)
{
    char buf[BLOCK_SIZE];

    if (count > (uint16_t)EXT_MAX_INDEX)
        return -1;

    memset(buf, 0, sizeof(buf));
    {
        uint32_t magic = EXT_IDX_MAGIC;
        memcpy(buf, &magic, sizeof(magic));
        memcpy(buf + 4, &count, sizeof(count));
        memcpy(buf + 6, &level, sizeof(level));
    }
    if (count > 0)
        memcpy(buf + EXT_HDR_SIZE, ents, (size_t)count * sizeof(ExtIndexEntry));
    return journal_write_dir_block(blk, buf);
}

// 沿 B+ 树找到覆盖逻辑块号的叶节点
static int find_leaf_for_lblk(const DiskINode *d, uint32_t lblk, int *leaf_out)
{
    ExtIndexEntry idx[EXT_MAX_INDEX];
    uint16_t count;
    uint16_t level;
    int blk;
    int i;

    if (d->d_tree_root == 0)
        return -1;

    blk = (int)d->d_tree_root;
    level = d->d_tree_level;

    while (level > 0) {
        if (index_read(blk, &count, &level, idx, EXT_MAX_INDEX) != 0)
            return -1;
        for (i = (int)count - 1; i >= 0; i--) {
            if (lblk <= idx[i].ie_key)
                break;
        }
        if (i < 0)
            return -1;
        blk = (int)idx[i].ie_child;
    }

    *leaf_out = blk;
    return 0;
}

// 在叶节点中查找逻辑块对应的物理块
static int leaf_lookup_blk(int leaf_blk, uint32_t lblk, uint16_t *phys_out)
{
    Extent ents[EXT_MAX_LEAF];
    uint16_t count;
    int i;

    if (leaf_read(leaf_blk, &count, ents, EXT_MAX_LEAF) != 0)
        return -1;

    for (i = (int)count - 1; i >= 0; i--) {
        if (extent_in_range(&ents[i], lblk)) {
            *phys_out = extent_phys_at(&ents[i], lblk);
            return 0;
        }
        if (ents[i].e_len > 0 && lblk > ents[i].e_lblk)
            break;
    }
    return -1;
}

// 查磁盘 inode 中逻辑块到物理块的映射
int extent_lookup(const DiskINode *d, uint32_t lblk, uint16_t *phys_out)
{
    int leaf;

    if (d == NULL || phys_out == NULL)
        return -1;

    if (extent_in_range(&d->d_extent, lblk)) {
        *phys_out = extent_phys_at(&d->d_extent, lblk);
        return 0;
    }

    if (d->d_tree_root == 0)
        return -1;

    if (d->d_tree_level == 0)
        return leaf_lookup_blk((int)d->d_tree_root, lblk, phys_out);

    if (find_leaf_for_lblk(d, lblk, &leaf) != 0)
        return -1;
    return leaf_lookup_blk(leaf, lblk, phys_out);
}

// 合并 Extent 数组中逻辑和物理均相邻的项
static void try_merge_adjacent(Extent *ents, int *count)
{
    int changed = 1;
    int i;

    while (changed) {
        changed = 0;
        for (i = 0; i < *count - 1; i++) {
            uint32_t end_l = ents[i].e_lblk + (uint32_t)ents[i].e_len;
            uint32_t end_p = (uint32_t)ents[i].e_pblk + (uint32_t)ents[i].e_len;

            if (end_l == ents[i + 1].e_lblk && end_p == (uint32_t)ents[i + 1].e_pblk) {
                ents[i].e_len = (uint16_t)(ents[i].e_len + ents[i + 1].e_len);
                memmove(&ents[i + 1], &ents[i + 2],
                        (size_t)(*count - i - 2) * sizeof(Extent));
                (*count)--;
                changed = 1;
                break;
            }
        }
    }
}

// 为 Extent B+ 树分配一个元数据块
static int alloc_metadata_block(MemINode *ip, uint16_t *out_blk)
{
    int blk;
    char zero[BLOCK_SIZE];

    blk = balloc_for(ip->m_inode_no);
    if (blk < 0)
        return -1;
    memset(zero, 0, sizeof(zero));
    if (write_block(blk, zero) != 0) {
        bfree(blk);
        return -1;
    }
    *out_blk = (uint16_t)blk;
    return 0;
}

// 向叶节点插入一条 Extent 并尝试合并
static int insert_into_leaf_blk(MemINode *ip, int leaf_blk, const Extent *new_e)
{
    Extent ents[EXT_MAX_LEAF];
    uint16_t count_u;
    int count;
    int i;
    int pos;

    if (leaf_read(leaf_blk, &count_u, ents, EXT_MAX_LEAF) != 0)
        return -1;

    count = (int)count_u;

    for (i = 0; i < count; i++) {
        if (extent_in_range(&ents[i], new_e->e_lblk))
            return 0;
    }

    if (count >= EXT_MAX_LEAF)
        return -1;

    pos = count;
    for (i = 0; i < count; i++) {
        if (new_e->e_lblk < ents[i].e_lblk) {
            pos = i;
            break;
        }
    }

    if (pos < count)
        memmove(&ents[pos + 1], &ents[pos], (size_t)(count - pos) * sizeof(Extent));
    ents[pos] = *new_e;
    count++;
    try_merge_adjacent(ents, &count);

    if (leaf_write(leaf_blk, (uint16_t)count, ents) != 0)
        return -1;

    if (pos == 0 && count > 0)
        ip->m_dinode.d_extent = ents[0];

    ip->m_flags |= MINODE_DIRTY;
    return 0;
}

// 向 inode 的 Extent 结构插入新映射，必要时建树
static int insert_extent(MemINode *ip, const Extent *new_e)
{
    DiskINode *d = &ip->m_dinode;
    Extent pair[2];
    int cnt;
    int leaf;

    if (d->d_tree_root == 0 && d->d_extent.e_len == 0) {
        d->d_extent = *new_e;
        ip->m_flags |= MINODE_DIRTY;
        return 0;
    }

    if (d->d_tree_root == 0) {
        pair[0] = d->d_extent;
        pair[1] = *new_e;
        if (new_e->e_lblk < pair[0].e_lblk) {
            pair[0] = *new_e;
            pair[1] = d->d_extent;
        }
        cnt = 2;
        try_merge_adjacent(pair, &cnt);
        if (cnt == 1) {
            d->d_extent = pair[0];
            ip->m_flags |= MINODE_DIRTY;
            return 0;
        }
        {
            uint16_t leaf_blk;
            if (alloc_metadata_block(ip, &leaf_blk) != 0)
                return -1;
            if (leaf_write((int)leaf_blk, (uint16_t)cnt, pair) != 0) {
                bfree((int)leaf_blk);
                return -1;
            }
            d->d_tree_root = leaf_blk;
            d->d_tree_level = 0;
            memset(&d->d_extent, 0, sizeof(d->d_extent));
            ip->m_flags |= MINODE_DIRTY;
            return 0;
        }
    }

    if (d->d_tree_level == 0)
        return insert_into_leaf_blk(ip, (int)d->d_tree_root, new_e);

    if (find_leaf_for_lblk(d, new_e->e_lblk, &leaf) != 0)
        return -1;
    return insert_into_leaf_blk(ip, leaf, new_e);
}

// 释放单个 Extent 叶节点占用的数据块
static void free_leaf_blocks(int leaf_blk)
{
    Extent ents[EXT_MAX_LEAF];
    uint16_t count;
    int i;
    uint16_t j;

    if (leaf_read(leaf_blk, &count, ents, EXT_MAX_LEAF) != 0)
        return;

    for (i = 0; i < (int)count; i++) {
        for (j = 0; j < ents[i].e_len; j++)
            bfree((int)ents[i].e_pblk + (int)j);
    }
    bfree(leaf_blk);
}

// 递归释放 Extent B+ 树占用的全部块
static void free_tree(int blk, uint16_t level)
{
    ExtIndexEntry idx[EXT_MAX_INDEX];
    uint16_t count;
    uint16_t child_level;
    int i;

    if (blk == 0)
        return;

    if (level == 0) {
        free_leaf_blocks(blk);
        return;
    }

    if (index_read(blk, &count, &child_level, idx, EXT_MAX_INDEX) != 0)
        return;

    for (i = 0; i < (int)count; i++)
        free_tree((int)idx[i].ie_child, (uint16_t)(level - 1));
    bfree(blk);
}

// 清空文件的 Extent 映射并释放树块
void extent_clear(MemINode *ip)
{
    if (ip == NULL)
        return;

    if (ip->m_dinode.d_tree_root != 0)
        free_tree((int)ip->m_dinode.d_tree_root, ip->m_dinode.d_tree_level);
    else if (ip->m_dinode.d_extent.e_len > 0) {
        uint16_t j;
        for (j = 0; j < ip->m_dinode.d_extent.e_len; j++)
            bfree((int)ip->m_dinode.d_extent.e_pblk + (int)j);
    }

    memset(&ip->m_dinode.d_extent, 0, sizeof(ip->m_dinode.d_extent));
    ip->m_dinode.d_tree_root = 0;
    ip->m_dinode.d_tree_level = 0;
    ip->m_dinode.d_size = 0;
    ip->m_flags |= MINODE_DIRTY;
}

// 把 inode 设为仅含一条内联 Extent
int extent_set_single(MemINode *ip, uint32_t lblk, uint16_t pblk, uint16_t len)
{
    if (ip == NULL || len == 0)
        return -1;

    if (ip->m_dinode.d_tree_root != 0)
        free_tree((int)ip->m_dinode.d_tree_root, ip->m_dinode.d_tree_level);

    memset(&ip->m_dinode.d_extent, 0, sizeof(ip->m_dinode.d_extent));
    ip->m_dinode.d_extent.e_lblk = lblk;
    ip->m_dinode.d_extent.e_pblk = pblk;
    ip->m_dinode.d_extent.e_len = len;
    ip->m_dinode.d_tree_root = 0;
    ip->m_dinode.d_tree_level = 0;
    ip->m_flags |= MINODE_DIRTY;
    return 0;
}

// 顺序写入时尝试延长末尾 Extent 而非新建
static int extent_try_extend_run(MemINode *ip, uint32_t lblk, uint16_t pblk)
{
    DiskINode *d;
    Extent    *e;
    uint32_t   end_lblk;
    uint32_t   end_pblk;

    if (ip == NULL || ip->m_dinode.d_tree_root != 0)
        return -1;

    d = &ip->m_dinode;
    e = &d->d_extent;
    if (e->e_len == 0)
        return -1;

    end_lblk = e->e_lblk + (uint32_t)e->e_len;
    end_pblk = (uint32_t)e->e_pblk + (uint32_t)e->e_len;
    if (lblk != end_lblk || pblk != (uint16_t)end_pblk)
        return -1;

    e->e_len++;
    ip->m_flags |= MINODE_DIRTY;
    return 0;
}

// 查或建逻辑块映射，create 为真时分配新物理块
int extent_bmap(MemINode *ip, uint32_t lblk, int create, uint16_t *phys_out)
{
    Extent new_e;
    int blk;

    if (ip == NULL || phys_out == NULL)
        return -1;

    if (extent_lookup(&ip->m_dinode, lblk, phys_out) == 0)
        return 0;

    if (!create)
        return -1;

    blk = balloc_for(ip->m_inode_no);
    if (blk < 0)
        return -1;

    if (extent_try_extend_run(ip, lblk, (uint16_t)blk) == 0) {
        *phys_out = (uint16_t)blk;
        return 0;
    }

    new_e.e_lblk = lblk;
    new_e.e_pblk = (uint16_t)blk;
    new_e.e_len = 1;

    if (insert_extent(ip, &new_e) != 0) {
        bfree(blk);
        return -1;
    }

    *phys_out = (uint16_t)blk;
    return 0;
}
