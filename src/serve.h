/*
 * serve.h
 * TCP/HTTP/WebSocket 多终端服务声明。
 */
#ifndef SERVE_H
#define SERVE_H

#ifdef __cplusplus
extern "C" {
#endif

// 启动 HTTP 与 TCP 监听并处理连接
int serve_main(int port);

// API 子进程主循环：读 JSON 请求并写响应
int upfs_api_session(int in_fd, int out_fd);

// 返回多进程共享的磁盘镜像路径
const char *shared_disk_path(void);
// 设置共享磁盘路径供子进程挂载
void        shared_set_disk(const char *path);

#ifdef __cplusplus
}
#endif

#endif
