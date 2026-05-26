// serve.h —— UPFS TCP 服务端模式
//
// 监听指定端口，每连接 fork 一个 UPFS shell 会话。
// 用于 websrv 等前端通过 TCP 接入多终端。

#ifndef SERVE_H
#define SERVE_H

#ifdef __cplusplus
extern "C" {
#endif

// 启动 TCP 服务端，监听 port 端口。不返回（循环 accept）。
int serve_main(int port);

// 多终端共享磁盘选择：一个终端 mount/format 后，其他终端自动感知
const char *shared_disk_path(void);
void        shared_set_disk(const char *path);

#ifdef __cplusplus
}
#endif

#endif // SERVE_H
