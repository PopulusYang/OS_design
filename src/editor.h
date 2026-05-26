// editor.h —— 简易全屏文本编辑器（vim-like）
//
// 作为 shell 内建命令 vim / edit 的后端。
// 直接操作宿主机文件系统。

#ifndef EDITOR_H
#define EDITOR_H

#ifdef __cplusplus
extern "C" {
#endif

// 启动编辑器打开 path。成功返回 0，失败返回 -1。
int editor_open(const char *path);

#ifdef __cplusplus
}
#endif

#endif // EDITOR_H
