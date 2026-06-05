/*
 * format.h
 * 格式化磁盘：初始化引导块、块组、日志、inode 映射与根目录。
 */
#ifndef FORMAT_H
#define FORMAT_H

#ifdef __cplusplus
extern "C" {
#endif

// 执行完整格式化并保存磁盘镜像
int format(const char *disk_path);

#ifdef __cplusplus
}
#endif

#endif
