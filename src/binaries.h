// binaries.h —— 预置演示程序二进制数据

#ifndef BINARIES_H
#define BINARIES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 二进制程序描述
typedef struct DemoBinary {
    const char     *name;       // 文件名，如 "/bin/hello"
    const uint8_t  *data;       // .upx 二进制数据
    size_t          size;       // 字节数
} DemoBinary;

// 获取所有预置程序
const DemoBinary *binaries_get_all(int *count);

// 根据名称获取单个程序
const DemoBinary *binaries_find(const char *name);

#ifdef __cplusplus
}
#endif

#endif // BINARIES_H
