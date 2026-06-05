/*
 * bg.c
 * 块组成组链接法：锚块维护空闲栈，按 inode 所在块组优先分配数据块。
 */// 块组管理模块实现
// 空闲块管理采用成组链接法：每个块组以锚点块为栈顶，栈满时通过空闲块本身链式串联下一组


#include "fs/bg.h"
#include "fs/inomap.h"
#include "fs/buf.h"
#include "fs/journal.h"
#include "fs/disk_io.h"

#include <stdio.h>
#include <string.h>

// 块组锚点魔数 BGAN
#define BG_ANCHOR_MAGIC     0x4247414EU

// 块组锚点块磁盘格式：每块组的第一个块存放该组空闲块管理信息
typedef struct BgAnchorDisk {
    uint32_t ba_magic;              // 魔数 0x4247414E ("BGAN")
    uint32_t ba_block_free;         // 该块组空闲块总数
    uint16_t ba_free_stack_count;   // 当前锚点块栈中有效空闲块数量
    uint16_t ba_free_chain;         // 下一个存有空闲块号的链块号（0表示末尾）
    uint16_t ba_free_stack[MAX_FREE_BLOCKS]; // 空闲块号栈（栈顶在栈底，出栈从末尾取）
    uint8_t  ba_pad[BLOCK_SIZE - 12 - MAX_FREE_BLOCKS * 2]; // 填充至512字节
} BgAnchorDisk;

// 块组运行时数据结构：磁盘数据的镜像 + 块组描述符
typedef struct BgRuntime {
    BlockGroupDesc desc;            // 块组描述符（元数据）
    uint32_t       block_free;      // 空闲块数量
    uint16_t       free_stack_count; // 空闲块栈高度
    uint16_t       free_chain;      // 空闲块链表头
    uint16_t       free_stack[MAX_FREE_BLOCKS]; // 空闲块栈
} BgRuntime;

//运行时与元数据共8个
static BgRuntime      g_bg[BG_COUNT];
static BlockGroupDesc g_bg_desc[BG_COUNT];

static int            g_bg_loaded; // 已加载

// 检查成组链接寄存块头中的计数是否合法
static int reg_header_valid(uint16_t m)
{
    return m > 0 && m <= MAX_FREE_BLOCKS;
}

// 根据物理块号计算所属块组编号
int bg_from_block(int blockno)
{
    int bg;
    if (blockno < BG_ZONE_START)
        return -1;
    bg = (blockno - BG_ZONE_START) / BG_BLOCKS_PER_GROUP;
    if (bg < 0 || bg >= BG_COUNT)
        return -1;
    return bg;
}

// 判断块号是否落在某块组的数据区内
int bg_block_in_data_zone(int blockno)
{
    int bg = bg_from_block(blockno);
    const BlockGroupDesc *d;
    if (bg < 0)
        return 0;
    d = &g_bg_desc[bg];
    return blockno >= d->bgd_data_start &&
           blockno < d->bgd_data_start + d->bgd_data_blocks;
}

// 判断块号是否为 inode 块
int bg_is_inode_disk_block(int blockno)
{
    return inomap_is_chunk_block(blockno);
}

// 判断块号是否为块组锚块或引导/超级块
int bg_is_anchor_block(int blockno)
{
    int i;
    for (i = 0; i < BG_COUNT; i++) {
        if ((int)g_bg_desc[i].bgd_anchor_block == blockno)
            return 1;
    }
    return blockno == SUPERBLOCK_BLOCKNO || blockno == BOOT_BLOCKNO;
}

// 计算 8 个块组的锚块与数据区起始布局
static void bg_build_layout(void)
{
    int bg;
    int cursor = BG_ZONE_START;

    memset(g_bg_desc, 0, sizeof(g_bg_desc));
    for (bg = 0; bg < BG_COUNT; bg++) {
        BlockGroupDesc *d = &g_bg_desc[bg];
        d->bgd_anchor_block = (uint16_t)cursor;
        cursor++;
        d->bgd_data_start = (uint16_t)cursor;
        d->bgd_data_blocks = (uint16_t)BG_DATA_BLOCKS_PER_GROUP;
        cursor += BG_DATA_BLOCKS_PER_GROUP;
        g_bg[bg].desc = *d;
    }
}

// 用成组链接法初始化块组的空闲块栈
static int bg_init_free_stack(BgRuntime *rt, uint16_t *blks, int cnt)
{
    int idx = 0;
    int left = cnt;
    int first;

    if (cnt <= 0)
        return 0;

    first = left > MAX_FREE_BLOCKS ? MAX_FREE_BLOCKS : left;
    rt->free_stack_count = (uint16_t)first;
    for (int i = 0; i < first; i++)
        rt->free_stack[i] = blks[idx++];
    left -= first;
    rt->free_chain = 0;
    rt->block_free = (uint32_t)cnt;

    while (left > 0) {
        int group_n = left > MAX_FREE_BLOCKS ? MAX_FREE_BLOCKS : left;
        uint16_t reg_blk = blks[idx + group_n - 1];
        uint16_t reg_buf[BLOCK_SIZE / (int)sizeof(uint16_t)];

        memset(reg_buf, 0, sizeof(reg_buf));
        reg_buf[0] = (uint16_t)(group_n - 1);
        for (int j = 0; j < group_n - 1; j++)
            reg_buf[j + 1] = blks[idx + j];
        reg_buf[group_n] = rt->free_chain;

        if (disk_write_block((int)reg_blk, reg_buf) != 0)
            return -1;

        rt->free_chain = reg_blk;
        idx += group_n;
        left -= group_n;
    }
    return 0;
}

// 把块组运行时状态写入锚块
static int bg_write_anchor(int bg_index)
{
    BgRuntime *rt = &g_bg[bg_index];
    char block_buf[BLOCK_SIZE];
    BgAnchorDisk *ad = (BgAnchorDisk *)block_buf;

    memset(block_buf, 0, sizeof(block_buf));
    ad->ba_magic = BG_ANCHOR_MAGIC;
    ad->ba_block_free = rt->block_free;
    ad->ba_free_stack_count = rt->free_stack_count;
    ad->ba_free_chain = rt->free_chain;
    memcpy(ad->ba_free_stack, rt->free_stack, sizeof(rt->free_stack));
    return disk_write_block((int)rt->desc.bgd_anchor_block, block_buf);
}

// 从锚块读入块组空闲栈与链表头
static int bg_read_anchor(int bg_index)
{
    BgRuntime *rt = &g_bg[bg_index];
    char block_buf[BLOCK_SIZE];
    const BgAnchorDisk *ad;

    if (disk_read_block((int)rt->desc.bgd_anchor_block, &block_buf) != 0)
        return -1;

    ad = (const BgAnchorDisk *)block_buf;
    if (ad->ba_magic != BG_ANCHOR_MAGIC)
        return -1;

    rt->block_free = ad->ba_block_free;
    rt->free_stack_count = ad->ba_free_stack_count;
    rt->free_chain = ad->ba_free_chain;
    memcpy(rt->free_stack, ad->ba_free_stack, sizeof(rt->free_stack));
    return 0;
}

// 格式化时初始化全部块组的空闲链表
// 格式化时初始化块组：构建布局、收集空闲块号、写入锚点块
int bg_format_init(void)
{
    uint16_t free_blks[BG_DATA_BLOCKS_PER_GROUP];

    bg_build_layout();

    for (int bg = 0; bg < BG_COUNT; bg++) {
        const BlockGroupDesc *d = &g_bg_desc[bg];
        int cnt = 0;

        memset(&g_bg[bg], 0, sizeof(g_bg[bg]));
        g_bg[bg].desc = *d;

        for (int i = 0; i < d->bgd_data_blocks; i++) {
            uint16_t blk = (uint16_t)(d->bgd_data_start + i);
            if (bg == 0 && blk == ROOT_DIR_BLOCK)
                continue;
            free_blks[cnt++] = blk;
        }

        if (bg_init_free_stack(&g_bg[bg], free_blks, cnt) != 0)
            return -1;
        if (bg_write_anchor(bg) != 0)
            return -1;
    }

    g_bg_loaded = 1;
    return 0;
}

// 挂载时从超级块和锚块恢复块组状态
// 挂载时从超级块恢复块组状态：加载描述符表，读取各块组锚点块
int bg_init_from_super(const SuperBlock *sb)
{
    if (sb == NULL)
        return -1;

    bg_build_layout();
    memcpy(g_bg_desc, sb->s_bg_table, sizeof(g_bg_desc));

    for (int bg = 0; bg < BG_COUNT; bg++) {
        g_bg[bg].desc = g_bg_desc[bg];
        if (bg_read_anchor(bg) != 0)
            return -1;
    }
    g_bg_loaded = 1;
    return 0;
}

// 汇总各块组空闲块数写入超级块
int bg_fill_super(SuperBlock *sb)
{
    uint32_t bfree = 0;

    if (sb == NULL)
        return -1;

    memcpy(sb->s_bg_table, g_bg_desc, sizeof(g_bg_desc));
    sb->s_bg_count = BG_COUNT;

    for (int bg = 0; bg < BG_COUNT; bg++)
        bfree += g_bg[bg].block_free;

    sb->s_block_free_count = bfree;
    sb->s_block_total = (uint32_t)FILE_DATA_BLOCKS;

    if (inomap_sync(sb) != 0)
        return -1;
    return 0;
}

// 把所有块组锚块写回磁盘
int bg_sync(void)
{
    if (!g_bg_loaded)
        return -1;
    for (int bg = 0; bg < BG_COUNT; bg++) {
        if (bg_write_anchor(bg) != 0)
            return -1;
    }
    return 0;
}

// 栈空时从寄存块加载下一组空闲块号
static int bg_reload_free_stack(BgRuntime *rt)
{
    if (rt->free_stack_count > 0)
        return 0;
    if (rt->free_chain == 0)
        return -1;

    uint16_t reg_buf[BLOCK_SIZE / (int)sizeof(uint16_t)];
    if (read_block((int)rt->free_chain, reg_buf) != 0)
        return -1;

    int m = (int)reg_buf[0];
    if (!reg_header_valid((uint16_t)m))
        return -1;

    for (int j = 0; j < m; j++)
        rt->free_stack[j] = reg_buf[j + 1];

    if (m < MAX_FREE_BLOCKS) {
        rt->free_stack[m] = rt->free_chain;
        rt->free_stack_count = (uint16_t)(m + 1);
    } else {
        rt->free_stack_count = (uint16_t)m;
    }
    rt->free_chain = reg_buf[m + 1];
    return 0;
}

// 根据 inode 号推算其所在块组，用于就近分配
static int bg_hint_to_group(uint16_t ino_hint)
{
    int blk;
    int off;

    if (ino_hint == 0)
        return 0;
    if (inomap_lookup((uint32_t)ino_hint, &blk, &off) == 0)
        return bg_from_block(blk);
    return 0;
}

// 按 inode 提示优先从同块组分配一个空闲数据块
int bg_balloc_for(uint16_t ino_hint)
{
    int start = bg_hint_to_group(ino_hint);
    int bg;
    BgRuntime *rt;
    uint16_t blk;

    for (int pass = 0; pass < BG_COUNT; pass++) {
        bg = (start + pass) % BG_COUNT;
        rt = &g_bg[bg];
        if (rt->block_free == 0)
            continue;
        if (rt->free_stack_count == 0) {
            if (bg_reload_free_stack(rt) != 0)
                continue;
        }
        rt->free_stack_count--;
        blk = rt->free_stack[rt->free_stack_count];
        rt->block_free--;
        return (int)blk;
    }
    return -1;
}

// 把数据块回收到所属块组的空闲栈
int bg_bfree(int blockno)
{
    int bg = bg_from_block(blockno);
    BgRuntime *rt;
    uint16_t b;

    if (bg < 0 || !bg_block_in_data_zone(blockno))
        return -1;
    if (inomap_is_chunk_block(blockno))
        return -1;

    rt = &g_bg[bg];
    b = (uint16_t)blockno;

    if (rt->free_stack_count >= MAX_FREE_BLOCKS) {
        uint16_t reg_buf[BLOCK_SIZE / (int)sizeof(uint16_t)];
        memset(reg_buf, 0, sizeof(reg_buf));
        reg_buf[0] = MAX_FREE_BLOCKS;
        for (int i = 0; i < MAX_FREE_BLOCKS; i++)
            reg_buf[i + 1] = rt->free_stack[i];
        reg_buf[MAX_FREE_BLOCKS + 1] = rt->free_chain;
        if (write_block((int)b, reg_buf) != 0)
            return -1;
        rt->free_chain = b;
        rt->free_stack_count = 1;
        rt->free_stack[0] = b;
    } else {
        rt->free_stack[rt->free_stack_count++] = b;
    }
    rt->block_free++;
    return 0;
}

// 打印各块组锚块、数据区与空闲块统计
void bg_debug_print(void)
{
    printf("\n");
    printf("  ── Block Groups (%d) ───────────────────────────\n", BG_COUNT);
    for (int bg = 0; bg < BG_COUNT; bg++) {
        const BlockGroupDesc *d = &g_bg_desc[bg];
        printf("  BG%-2d  anchor=%u  data[%u..%u)  free_blk=%u\n",
               bg,
               (unsigned)d->bgd_anchor_block,
               (unsigned)d->bgd_data_start,
               (unsigned)(d->bgd_data_start + d->bgd_data_blocks),
               (unsigned)g_bg[bg].block_free);
    }
    printf("\n");
}

// 返回指定块组当前空闲块数
uint32_t bg_group_free(int group)
{
    if (group < 0 || group >= BG_COUNT) return 0;
    return g_bg[group].block_free;
}
