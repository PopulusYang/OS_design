// format.h —— 虚拟盘格式化（mkfs）接口

#ifndef FORMAT_H
#define FORMAT_H

#ifdef __cplusplus
extern "C" {
#endif

// 格式化虚拟盘：初始化引导块、超级块、空闲块成组链接、根目录，并写入 disk_path
int format(const char *disk_path);

#ifdef __cplusplus
}
#endif

#endif // FORMAT_H
