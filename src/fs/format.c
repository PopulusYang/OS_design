/*
 * format.c
 * 格式化流程：引导块、块组、日志、inode 树、超级块、根目录，最后保存镜像。
 */
#include "fs/format.h"
#include "fs/disk_io.h"
#include "fs/bg.h"
#include "fs/inomap.h"
#include "fs/journal.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#define FORMAT_LOG(fmt, ...) fprintf(stderr, "[format] " fmt "\n", ##__VA_ARGS__)

static const char BOOT_MAGIC[] = "UPFSBOOT";

// 写入 0 号引导块，记录超级块与根目录块号
static int init_boot_block(void)
{
    char boot_buf[BLOCK_SIZE];
    uint32_t *u32;

    memset(boot_buf, 0, sizeof(boot_buf));
    memcpy(boot_buf, BOOT_MAGIC, sizeof(BOOT_MAGIC) - 1);

    u32 = (uint32_t *)(boot_buf + BLOCK_SIZE - 2 * (int)sizeof(uint32_t));
    u32[0] = (uint32_t)SUPERBLOCK_BLOCKNO;
    u32[1] = (uint32_t)ROOT_DIR_BLOCK;

    return disk_write_block(BOOT_BLOCKNO, boot_buf);
}

// 在根目录数据块写入 . 和 .. 两个目录项
static int init_root_directory(void)
{
    char dir_block[BLOCK_SIZE];
    DirEntry *entries;
    int i;

    memset(dir_block, 0, sizeof(dir_block));
    entries = (DirEntry *)dir_block;

    entries[0].de_inode = (uint16_t)ROOT_INODE_NO;
    strncpy(entries[0].de_name, ".", MAX_FILENAME_LEN);

    entries[1].de_inode = (uint16_t)ROOT_INODE_NO;
    strncpy(entries[1].de_name, "..", MAX_FILENAME_LEN);

    for (i = 0; i < 2; i++) {
        entries[i].de_name[MAX_FILENAME_LEN - 1] = '\0';
    }

    return disk_write_block(ROOT_DIR_BLOCK, dir_block);
}

// 执行完整格式化并保存磁盘镜像
int format(const char *disk_path)
{
    SuperBlock sb;

    if (disk_path == NULL || disk_path[0] == '\0') {
        FORMAT_LOG("错误：磁盘路径为空");
        return -1;
    }

    if (disk_create() != 0) {
        FORMAT_LOG("错误：内存分配失败");
        return -1;
    }

    if (init_boot_block() != 0) {
        FORMAT_LOG("错误：引导块写入失败");
        disk_shutdown();
        return -1;
    }

    if (bg_format_init() != 0) {
        FORMAT_LOG("错误：块组初始化失败");
        disk_shutdown();
        return -1;
    }

    if (journal_init_format() != 0) {
        FORMAT_LOG("错误：日志区初始化失败");
        disk_shutdown();
        return -1;
    }

    if (inomap_format_init(ROOT_DIR_BLOCK) != 0) {
        FORMAT_LOG("错误：动态 inode 映射初始化失败");
        disk_shutdown();
        return -1;
    }

    memset(&sb, 0, sizeof(sb));
    sb.s_magic = VFS_MAGIC;
    if (bg_fill_super(&sb) != 0) {
        FORMAT_LOG("错误：超级块汇总失败");
        disk_shutdown();
        return -1;
    }

    // inode 树已占用部分数据块，须先同步锚块空闲栈再写超级块
    if (bg_sync() != 0) {
        FORMAT_LOG("错误：块组 anchor 同步失败");
        disk_shutdown();
        return -1;
    }

    if (disk_write_block(SUPERBLOCK_BLOCKNO, &sb) != 0) {
        FORMAT_LOG("错误：超级块写入失败");
        disk_shutdown();
        return -1;
    }

    if (init_root_directory() != 0) {
        FORMAT_LOG("错误：根目录初始化失败");
        disk_shutdown();
        return -1;
    }

    if (disk_save(disk_path) != 0) {
        FORMAT_LOG("错误：镜像文件保存失败 (path=%s, errno=%d: %s)",
                   disk_path, errno, strerror(errno));
        disk_shutdown();
        return -1;
    }

    disk_shutdown();
    return 0;
}
