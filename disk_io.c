// disk_io.c —— 虚拟磁盘底层块读写与宿主机文件持久化

#include "disk_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// 全局虚拟盘内存模拟区指针；由 disk_create / disk_load 分配，disk_shutdown 释放
static uint8_t *g_disk_mem = NULL;

// 虚拟盘镜像在宿主机上的路径缓存（可选，供调试）
static char g_disk_path[512];

// 标记内存内容是否曾通过 write_block 修改（预留，供增量同步扩展）
static int g_disk_dirty = 0;

// 内部辅助：校验块号与缓冲区指针
static int disk_check_block(int block_no, const void *buf)
{
    if (g_disk_mem == NULL) {
        return -1;
    }
    if (buf == NULL) {
        return -1;
    }
    if (block_no < 0 || block_no >= TOTAL_DISK_BLOCKS) {
        return -1;
    }
    return 0;
}

int disk_create(void)
{
    if (g_disk_mem != NULL) {
        free(g_disk_mem);
        g_disk_mem = NULL;
    }

    // calloc 保证初始内容为全零，避免未初始化盘块被误读
    g_disk_mem = (uint8_t *)calloc((size_t)TOTAL_DISK_BLOCKS, (size_t)BLOCK_SIZE);
    if (g_disk_mem == NULL) {
        return -1;
    }

    g_disk_dirty = 0;
    g_disk_path[0] = '\0';
    return 0;
}

int disk_load(const char *disk_path)
{
    FILE *fp;
    size_t expect_size;
    size_t nread;

    if (disk_path == NULL || disk_path[0] == '\0') {
        return -1;
    }

    if (disk_create() != 0) {
        return -1;
    }

    fp = fopen(disk_path, "rb");
    if (fp == NULL) {
        disk_shutdown();
        return -1;
    }

    expect_size = (size_t)TOTAL_DISK_BLOCKS * (size_t)BLOCK_SIZE;
    nread = fread(g_disk_mem, 1, expect_size, fp);
    fclose(fp);

    if (nread != expect_size) {
        disk_shutdown();
        return -1;
    }

    strncpy(g_disk_path, disk_path, sizeof(g_disk_path) - 1);
    g_disk_path[sizeof(g_disk_path) - 1] = '\0';
    g_disk_dirty = 0;
    return 0;
}

int disk_save(const char *disk_path)
{
    FILE *fp;
    size_t total_size;
    size_t nwrite;

    if (g_disk_mem == NULL) {
        return -1;
    }
    if (disk_path == NULL || disk_path[0] == '\0') {
        return -1;
    }

    fp = fopen(disk_path, "wb");
    if (fp == NULL) {
        return -1;
    }

    total_size = (size_t)TOTAL_DISK_BLOCKS * (size_t)BLOCK_SIZE;
    nwrite = fwrite(g_disk_mem, 1, total_size, fp);
    if (nwrite != total_size) {
        fclose(fp);
        return -1;
    }

    if (fflush(fp) != 0) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    strncpy(g_disk_path, disk_path, sizeof(g_disk_path) - 1);
    g_disk_path[sizeof(g_disk_path) - 1] = '\0';
    g_disk_dirty = 0;
    return 0;
}

void disk_shutdown(void)
{
    if (g_disk_mem != NULL) {
        free(g_disk_mem);
        g_disk_mem = NULL;
    }
    g_disk_dirty = 0;
    g_disk_path[0] = '\0';
}

int read_block(int block_no, void *buf)
{
    size_t offset;

    if (disk_check_block(block_no, buf) != 0) {
        return -1;
    }

    // 块号到字节偏移的精确映射：offset = block_no × BLOCK_SIZE
    offset = (size_t)block_no * (size_t)BLOCK_SIZE;
    memcpy(buf, g_disk_mem + offset, (size_t)BLOCK_SIZE);
    return 0;
}

int write_block(int block_no, const void *buf)
{
    size_t offset;

    if (disk_check_block(block_no, buf) != 0) {
        return -1;
    }

    offset = (size_t)block_no * (size_t)BLOCK_SIZE;
    memcpy(g_disk_mem + offset, buf, (size_t)BLOCK_SIZE);
    g_disk_dirty = 1;
    return 0;
}

void *disk_memory(void)
{
    return g_disk_mem;
}

size_t disk_memory_size(void)
{
    return (size_t)TOTAL_DISK_BLOCKS * (size_t)BLOCK_SIZE;
}
