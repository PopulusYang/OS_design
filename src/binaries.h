/*
 * binaries.h
 * 格式化时注入 /bin 的预置 UPX 演示程序。
 */
#ifndef BINARIES_H
#define BINARIES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DemoBinary {
    const char     *name;
    const uint8_t  *data;
    size_t          size;
} DemoBinary;

// 返回全部演示程序及数量
const DemoBinary *binaries_get_all(int *count);

// 按路径名查找演示程序
const DemoBinary *binaries_find(const char *name);

#ifdef __cplusplus
}
#endif

#endif
