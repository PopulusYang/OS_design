/*
 * editor.h
 * 终端全屏文本编辑器入口。
 */
#ifndef EDITOR_H
#define EDITOR_H

#ifdef __cplusplus
extern "C" {
#endif

// 打开文件并进入全屏编辑循环
int editor_open(const char *path);

#ifdef __cplusplus
}
#endif

#endif
