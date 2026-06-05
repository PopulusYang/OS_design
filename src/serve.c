/*
 * serve.c
 * TCP 多终端服务器：HTTP(8080) + WebSocket + 原生 TCP(4096)。
 *
 * ===== 整体架构 =====
 *   serve_main() 是主进程入口，在两个端口上监听：
 *     - 8080: HTTP + WebSocket（浏览器 Web 桌面）
 *     - 4096: 原生 TCP（telnet/nc 直连终端）
 *
 *   每接受一个连接，分配一个 Conn 槽位。对于终端类连接（/ws/* 或原生TCP），
 *   fork 一个子进程运行 upfs_session()（即 main.c 的 Shell），父子通过
 *   Unix domain socketpair 通信——父进程转发网络数据和子进程的终端 I/O。
 *
 * ===== 关键数据流 =====
 *   浏览器 → HTTP GET / → 返回内嵌的 Web 桌面 HTML（web_page.h）
 *   浏览器 → WS /ws/N  → WebSocket 升级 → fork 终端子进程 → 双向转发
 *   浏览器 → WS /api    → WebSocket 升级 → fork API 子进程 → JSON 问答
 *   telnet → TCP 4096   → fork 终端子进程 → 双向转发
 *
 * ===== 多进程共享磁盘 =====
 *   父进程和所有子进程通过 mmap(MAP_SHARED|MAP_ANONYMOUS) 共享 g_shared，
 *   传递磁盘镜像路径；另外通过 kernel_shared_create() 共享内核状态（进程表等）。
 */
#include "serve.h"
#include "kernel_shared.h"
#include "web_page.h"
#include "fs/disk_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

/* ---- 外部声明 ---- */

// main.c 提供的终端 Shell 会话入口：读 in_fd 的命令，写结果到 out_fd
extern int upfs_session(int in_fd, int out_fd);
// web_api.c 提供的 API 会话入口：读 JSON 请求，写 JSON 响应
extern int upfs_api_session(int in_fd, int out_fd);

/* ---- 多进程共享内存 ---- */

// fork 后父子进程共享的匿名 mmap 区域，用于传递磁盘镜像路径
// ready=1 表示 path 已由父进程写入，子进程可以安全读取
static struct {
    volatile int ready;      // 内存屏障保护的就绪标志
    char         path[512];  // 磁盘镜像文件的绝对路径
} *g_shared;

/*
 * shared_disk_path - 子进程调用，获取父进程设置好的磁盘路径
 *
 * 只有在父进程通过 shared_set_disk() 写入并置 ready=1 后才返回有效值。
 * 配合 fs_reload_super() 使用：每个子进程在处理命令前重新加载超级块，
 * 确保看到其他进程的最新修改。
 */
const char *shared_disk_path(void)
{
    if (!g_shared || !g_shared->ready) return NULL;
    return g_shared->path;
}

/*
 * shared_set_disk - 父进程在 fork 前调用，设置磁盘路径
 *
 * 使用 __sync_synchronize() 内存屏障保证 path 的写入在 ready=1 之前
 * 对所有 CPU 可见，避免子进程读到半写的数据。
 */
void shared_set_disk(const char *path)
{
    if (!g_shared) return;
    strncpy(g_shared->path, path, sizeof(g_shared->path) - 1);
    g_shared->path[sizeof(g_shared->path) - 1] = '\0';
    __sync_synchronize();   // 写屏障：确保 path 写入先于 ready 置位
    g_shared->ready = 1;
}

/* ---- 常量和全局状态 ---- */

#define HTML_PORT     8080   // HTTP / WebSocket 服务端口（浏览器连接此端口）
#define TERM_PORT     4096   // 原生 TCP 终端端口（telnet/nc 连接此端口）
#define MAX_CONN      64     // 最大并发连接数（含两个监听 socket）
#define BUF_SIZE      65536  // 每个连接的读写缓冲区大小（64KB）
#define WS_GUID       "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"  // WebSocket 协议规定的魔数 GUID

// 连接类型枚举
enum {
    T_HTTP,  // HTTP 请求（尚未升级到 WebSocket）
    T_WS,    // WebSocket 终端会话（/ws/N）
    T_RAW,   // 原生 TCP 终端会话（端口 4096）
    T_API,   // WebSocket API 会话（/api）
};

/*
 * Conn —— 单个连接的完整状态
 *
 * fd:       客户端网络 socket 的文件描述符
 * type:     连接类型（T_HTTP / T_WS / T_RAW / T_API）
 * term_fd:  与终端子进程通信的 Unix domain socket fd（socketpair 的一端）
 * term_pid: 终端子进程的 PID
 * rbuf:     接收缓冲区（从网络读入的原始字节）
 * rlen:     rbuf 中当前有效数据的长度
 * abuf:     API 会话的行累积缓冲区（用于按行分割 JSON 消息）
 * alen:     abuf 中当前累积的字节数
 */
typedef struct {
    int  fd;           // 客户端 socket fd
    int  type;         // 连接类型
    int  term_fd;      // 到子进程的 socketpair fd
    int  term_pid;     // 子进程 PID
    char rbuf[BUF_SIZE]; // 网络接收缓冲
    int  rlen;          // rbuf 有效数据长度
    char abuf[BUF_SIZE]; // API 行累积缓冲
    int  alen;          // abuf 有效数据长度
} Conn;

static struct pollfd g_pfds[MAX_CONN];  // poll 监听的文件描述符数组
static Conn          g_conn[MAX_CONN];  // 每个 pollfd 对应的连接状态
static int           g_nfds;            // g_pfds 中有效元素个数（索引 0..g_nfds-1）
                                         // 0=HTTP监听, 1=TCP监听, 2..=客户端连接

/* ================================================================
 *  SHA1 哈希实现（用于 WebSocket 握手 Sec-WebSocket-Accept 计算）
 *
 *  WebSocket 协议要求：服务器收到 Sec-WebSocket-Key 后，
 *  拼接 WS_GUID，计算 SHA1，再 Base64 编码返回给客户端。
 *  这里内联实现避免引入外部依赖（OpenSSL/libcrypto）。
 * ================================================================ */

// SHA1 上下文：5 个 32 位状态字 + 2 个 64 位计数（拆成 2×uint32）+ 64B 块缓冲
typedef struct {
    uint32_t s[5];   // 哈希状态：H0..H4
    uint32_t c[2];   // 消息总位数计数（64-bit，拆为两个 32 位）
    uint8_t  b[64];  // 未处理的剩余字节块
} SHA1;

/*
 * sha1_t - SHA1 压缩函数，处理一个 512 位（64 字节）消息块
 *
 * 步骤：
 *   1. 将 64 字节扩展为 80 个 32 位字 w[0..79]
 *   2. 80 轮迭代，每 20 轮使用不同的非线性函数和常量
 *   3. 将结果累加到状态字 s[0..4]
 */
static void sha1_t(uint32_t st[5], const uint8_t b[64])
{
    uint32_t w[80];                       // 消息扩展表（80 个 32 位字）
    uint32_t a = st[0], e1 = st[1],       // 工作变量（注意 e1 即是标准的 b）
             c = st[2], d = st[3], e = st[4];
    uint32_t t;
    int i;

    // 步骤1：前 16 个字直接从输入块按大端序拼装
    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)b[i*4]   << 24) |
               ((uint32_t)b[i*4+1] << 16) |
               ((uint32_t)b[i*4+2] << 8)  |
                (uint32_t)b[i*4+3];

    // 步骤2：扩展为 80 字，w[i] = (w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16]) 左循环 1 位
    for (i = 16; i < 80; i++)
        w[i] = ((w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16]) << 1) |
               ((w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16]) >> 31);

    // 步骤3：80 轮迭代，分 4 组每 20 轮
    for (i = 0; i < 80; i++) {
        // 非线性函数 f(t, b, c, d)——按轮次分组
        if      (i < 20) t = (e1 & c) | ((~e1) & d);          // Ch:   (b ∧ c) ∨ (¬b ∧ d)
        else if (i < 40) t = e1 ^ c ^ d;                      // Parity: b ⊕ c ⊕ d
        else if (i < 60) t = (e1 & c) | (e1 & d) | (c & d);   // Maj:  (b ∧ c) ∨ (b ∧ d) ∨ (c ∧ d)
        else             t = e1 ^ c ^ d;                      // Parity
        t += ((a << 5) | (a >> 27)) + e + w[i];               // + 左循环 5 的 a
        // 加轮常量 K
        if      (i < 20) t += 0x5A827999;
        else if (i < 40) t += 0x6ED9EBA1;
        else if (i < 60) t += 0x8F1BBCDC;
        else             t += 0xCA62C1D6;
        // 寄存器移位
        e = d; d = c; c = (e1 << 30) | (e1 >> 2); e1 = a; a = t;
    }
    // 累加回状态
    st[0] += a; st[1] += e1; st[2] += c; st[3] += d; st[4] += e;
}

/*
 * sha1_u - 向 SHA1 上下文追加数据（Update）
 *
 * 将输入数据按 64 字节分块送入压缩函数，剩余不足一块的暂存到 c->b 中。
 * 同时更新 64 位的消息总位计数器 c->c。
 */
static void sha1_u(SHA1 *c, const void *d, size_t n)
{
    const uint8_t *p = (const uint8_t *)d;
    size_t  i = 0;
    size_t  j = (c->c[0] >> 3) & 63;   // 当前块内偏移（总位数/8 mod 64）

    // 更新 64 位位计数器 c->c = c->c + n*8（上溢进位到 c[1]）
    if ((c->c[0] += (uint32_t)(n << 3)) < (uint32_t)(n << 3))
        c->c[1]++;                     // 低 32 位溢出则进位
    c->c[1] += (uint32_t)(n >> 29);    // 高 32 位加上 n 的高位

    // 如果当前缓冲中有残余数据 + 新数据能凑满 64 字节，先填满处理
    if (j + n >= 64) {
        memcpy(c->b + j, p, 64 - j);       // 补齐 64 字节
        sha1_t(c->s, c->b);                // 压缩这一块
        for (i = 64 - j; i + 63 < n; i += 64)
            sha1_t(c->s, p + i);           // 连续处理完整的中间块
        j = 0;                             // 重置块内偏移
    } else {
        i = 0;
    }
    // 剩余不足 64 字节的数据暂存
    memcpy(c->b + j, p + i, n - i);
}

/*
 * sha1_f - 结束 SHA1 计算并输出 160 位摘要（Final）
 *
 * 按 FIPS 180-4 规定：
 *   1. 追加 bit '1'（字节 0x80）
 *   2. 填充 0 直到消息长度 ≡ 448 (mod 512)
 *   3. 追加 64 位原始消息长度（大端序）
 *   4. 将 5 个状态字按大端序输出为 20 字节
 */
static void sha1_f(uint8_t dg[20], SHA1 *c)
{
    uint8_t fc[8];  // 64 位消息总位数的 8 字节大端表示
    int i;

    // 将 64 位计数器转为大端序字节数组
    for (i = 0; i < 8; i++)
        fc[i] = (uint8_t)((c->c[(i >= 4) ? 0 : 1] >> ((3 - (i & 3)) * 8)) & 255);

    // 追加 bit '1'
    uint8_t pad = 0x80;
    sha1_u(c, &pad, 1);

    // 填充 0 字节直到 (总位数 mod 512) == 448（即块内偏移 56）
    while ((c->c[0] & 504) != 448) {
        pad = 0;
        sha1_u(c, &pad, 1);
    }

    // 追加 64 位长度，完成最后一块
    sha1_u(c, fc, 8);

    // 输出 20 字节摘要（大端序）
    for (i = 0; i < 20; i++)
        dg[i] = (uint8_t)((c->s[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
}

/*
 * sha1_hash - 一次性计算数据的 SHA1 摘要（Init + Update + Final 三合一）
 *
 * 用于 WebSocket 握手：sha1_hash(key+GUID, len, out) → 20 字节 hash
 */
static void sha1_hash(const uint8_t *d, size_t n, uint8_t dg[20])
{
    SHA1 c;
    memset(&c, 0, sizeof(c));
    // 初始化向量（FIPS 180-4 标准值）
    c.s[0] = 0x67452301;
    c.s[1] = 0xEFCDAB89;
    c.s[2] = 0x98BADCFE;
    c.s[3] = 0x10325476;
    c.s[4] = 0xC3D2E1F0;
    sha1_u(&c, d, n);
    sha1_f(dg, &c);
}
/* ================================================================
 *  Base64 编码（用于 WebSocket 握手 Accept 值的编码）
 *
 *  标准 Base64：每 3 字节编码为 4 个 ASCII 字符（6 位一组查表）。
 *  输入不足 3 的倍数时用 '=' 填充。
 * ================================================================ */

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/*
 * b64enc - 字节序列 → Base64 字符串
 *
 * @in:   原始字节
 * @len:  字节数
 * @out:  输出的 Base64 字符串（调用者确保空间：至少 (len+2)/3*4 + 1）
 */
static void b64enc(const uint8_t *in, int len, char *out)
{
    int i, o = 0;
    for (i = 0; i < len; i += 3) {
        // 取 24 位（三字节拼装，不足的位填 0）
        uint32_t v = ((uint32_t)(i     < len ? in[i]   : 0) << 16) |
                     ((uint32_t)(i + 1 < len ? in[i+1] : 0) << 8)  |
                      (uint32_t)(i + 2 < len ? in[i+2] : 0);
        out[o++] = B64[(v >> 18) & 63];          // 最高 6 位必然有效
        out[o++] = B64[(v >> 12) & 63];          // 次高 6 位也必然有效
        out[o++] = (i + 1 < len) ? B64[(v >> 6) & 63] : '=';  // 第三字节有效则编码，否则 '='
        out[o++] = (i + 2 < len) ? B64[v & 63]       : '=';  // 第三字节有效则编码，否则 '='
    }
    out[o] = '\0';
}

/* ================================================================
 *  网络 I/O 辅助函数
 * ================================================================ */

/*
 * nb_write - 非阻塞安全的"全部写入"
 *
 * 在循环中调用 write()，如果遇到 EAGAIN 则 poll 等待 socket 可写（最多 5 秒）。
 * 保证要么写完全部 len 字节返回 0，要么出错返回 -1。
 * 这是必需的，因为所有客户端 socket 都已设为 O_NONBLOCK。
 */
static int nb_write(int fd, const void *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, (const char *)buf + off, len - off);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 内核发送缓冲区满，等待可写
                struct pollfd p = { .fd = fd, .events = POLLOUT };
                int ret = poll(&p, 1, 5000);   // 最多等 5 秒
                if (ret <= 0) return -1;
                continue;
            }
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

/* ================================================================
 *  WebSocket 帧处理（RFC 6455）
 *
 *  帧格式:
 *    0                   1                   2                   3
 *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *   +-+-+-+-+-------+-+-------------+-------------------------------+
 *   |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
 *   |I|S|S|S|  (4)  |A|     (7)     |          (16/64)              |
 *   |N|V|V|V|       |S|             |  (if payload len==126/127)    |
 *   | |1|2|3|       |K|             |                               |
 *   +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
 *   |     Masking-key (if MASK set) |          Payload Data         |
 *   +-------------------------------+ - - - - - - - - - - - - - - - +
 * ================================================================ */

/*
 * ws_send - 发送一条 WebSocket 文本帧（opcode=0x1，FIN=1）
 *
 * 只发送文本帧（opcode 0x81），不带掩码（服务器→客户端方向不需要掩码）。
 * 根据 payload 长度自动选择 7/16/64 位长度字段编码。
 */
static void ws_send(int fd, const void *msg, int len)
{
    uint8_t f[14];        // 帧头最大 14 字节（2 + 8 长度 + 4 掩码，但我们不用掩码所以最多 10）
    int     pos = 0;

    // 字节 0: FIN=1, RSV=0, opcode=0x1（文本帧）
    f[pos++] = 0x81;

    // 字节 1-9: 掩码位=0 + 载荷长度（7/16/64 位三种编码）
    if (len < 126) {
        f[pos++] = (uint8_t)len;                            // 7 位长度
    } else if (len < 65536) {
        f[pos++] = 126;                                     // 126 表示后续 2 字节为长度
        f[pos++] = (uint8_t)(len >> 8);
        f[pos++] = (uint8_t)(len);
    } else {
        f[pos++] = 127;                                     // 127 表示后续 8 字节为长度
        memset(f + pos, 0, 4); pos += 4;                    // 高 32 位填 0
        f[pos++] = (uint8_t)(len >> 24);
        f[pos++] = (uint8_t)(len >> 16);
        f[pos++] = (uint8_t)(len >> 8);
        f[pos++] = (uint8_t)(len);
    }

    // 发送帧头
    if (nb_write(fd, f, pos) < 0) return;

    // 发送载荷数据
    if (len > 0) nb_write(fd, msg, (size_t)len);
}

/*
 * ws_recv - 从连接缓冲区解析一条完整的 WebSocket 帧
 *
 * 处理流程:
 *   1. 尝试从 fd 读更多数据到 c->rbuf
 *   2. 检查帧头完整性（至少 2 字节）
 *   3. 处理控制帧: opcode 0x8 (Close) → 返回 -1
 *                  opcode 0x9 (Ping)  → 自动回复 Pong 并继续
 *   4. 解析扩展长度字段（16 位或 64 位）
 *   5. 如果有 MASK 位，读取 4 字节掩码密钥
 *   6. 检查完整帧是否已到齐，是则解掩码并输出 payload
 *   7. 已消费的数据从 rbuf 中移除
 *
 * 返回值: >0 payload 字节数, 0 帧未完整还需更多数据, -1 出错或连接关闭
 */
static int ws_recv(Conn *c, uint8_t *out, int max)
{
    int fd = c->fd;

    // ---- 第 1 步: 尽可能多地读入数据到 rbuf ----
    {
        int room = BUF_SIZE - c->rlen - 1;          // 保留 1 字节给 '\0'
        if (room > 0) {
            int n = (int)read(fd, (uint8_t *)c->rbuf + c->rlen, (size_t)room);
            if (n < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) return -1;  // 真正错误
            } else if (n == 0) {
                return -1;                           // 对端关闭
            } else {
                c->rlen += n;
            }
        }
    }

    // ---- 第 2 步: 检查最小帧头（2 字节）----
    if (c->rlen < 2) return 0;

    uint8_t *buf = (uint8_t *)c->rbuf;

    // ---- 第 3 步: 处理控制帧 ----
    int op = buf[0] & 0x0F;
    if (op == 0x8) return -1;                        // Close 帧 → 关闭连接

    if (op == 0x9) {                                 // Ping 帧 → 回复 Pong
        uint8_t pong[2] = { 0x8A, 0x00 };            // FIN + Pong opcode, 长度 0
        nb_write(fd, pong, 2);

        int rem = c->rlen - 2;                       // 移除已消费的 2 字节
        if (rem > 0) memmove(buf, buf + 2, (size_t)rem);
        c->rlen = rem;
        return 0;
    }

    // ---- 第 4 步: 解析载荷长度 ----
    int      masked = buf[1] & 0x80;                  // MASK 位（客户端→服务器必须为 1）
    uint64_t plen   = buf[1] & 0x7F;                  // 基础 7 位长度
    int      hdr_len = 2;

    if (plen == 126) {                               // 16 位扩展长度
        if (c->rlen < 4) return 0;                   // 数据不够，等下次
        plen = ((uint64_t)buf[2] << 8) | buf[3];
        hdr_len = 4;
    } else if (plen == 127) {                        // 64 位扩展长度
        if (c->rlen < 10) return 0;
        plen = 0;
        for (int i = 0; i < 8; i++) plen = (plen << 8) | buf[2 + i];
        hdr_len = 10;
    }

    // ---- 第 5 步: 掩码密钥 ----
    if (masked) {
        if (c->rlen < hdr_len + 4) return 0;         // 掩码密钥还没到齐
        hdr_len += 4;                                // 掩码密钥占 4 字节
    }

    // ---- 第 6 步: 检查完整帧是否到齐 ----
    if (plen > (uint64_t)max) return -1;             // payload 太大，拒绝

    int frame_len = hdr_len + (int)plen;
    if (c->rlen < frame_len) return 0;               // 数据未到齐

    // ---- 第 7 步: 解掩码，提取 payload ----
    uint8_t *payload = buf + hdr_len;
    if (masked) {
        uint8_t *mk = buf + hdr_len - 4;             // 掩码密钥在 payload 前 4 字节
        for (uint64_t i = 0; i < plen; i++)
            payload[i] ^= mk[i & 3];                 // 逐字节 XOR，密钥循环使用
    }
    memcpy(out, payload, (size_t)plen);

    // ---- 第 8 步: 从 rbuf 中移除已消费的帧 ----
    int remaining = c->rlen - frame_len;
    if (remaining > 0)
        memmove(buf, buf + frame_len, (size_t)remaining);
    c->rlen = remaining;

    return (int)plen;
}

/* ================================================================
 *  HTTP 响应
 * ================================================================ */

/*
 * http_send - 构造并发送 HTTP 响应
 *
 * @code:   HTTP 状态码（200 或 101 或 404 等）
 * @ctype:  Content-Type（如 "text/html; charset=utf-8"）
 * @body:   响应正文
 * @blen:   正文长度
 * @extra:  额外的响应头行（如 Upgrade + Sec-WebSocket-Accept）
 *
 * code==101 是 WebSocket 升级响应，格式特殊：无 Content-Type/Content-Length。
 */
static void http_send(int fd, int code, const char *ctype,
                      const char *body, int blen, const char *extra)
{
    char h[1024];

    // HTTP 101 切换协议（WebSocket 升级专用）
    if (code == 101) {
        int n = snprintf(h, sizeof(h),
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Connection: Upgrade\r\n"
            "%s\r\n",                              // extra 包含 Upgrade 和 Accept 头
            extra ? extra : "");
        nb_write(fd, h, (size_t)n);
        return;
    }

    // 常规 HTTP 响应
    int n = snprintf(h, sizeof(h),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "%s\r\n",                                  // 可选的额外头（通常为空）
        code, code == 200 ? "OK" : "Error",
        ctype ? ctype : "text/plain",
        blen,
        extra ? extra : "");
    if (nb_write(fd, h, (size_t)n) < 0) return;
    if (body && blen > 0) nb_write(fd, body, (size_t)blen);
}

/* ================================================================
 *  子进程管理（终端 / API 会话）
 * ================================================================ */

/*
 * term_spawn - fork 一个终端 Shell 子进程
 *
 * 使用 socketpair 创建父子通信通道，子进程把 socket 端 dup2 到
 * stdin/stdout/stderr 后 exec upfs_session()。
 * 返回父进程端的 fd（已设为非阻塞），父进程通过它转发用户输入和终端输出。
 */
static int term_spawn(void)
{
    int sv[2];                                       // sv[0]=父进程端, sv[1]=子进程端
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) return -1;

    pid_t p = fork();
    if (p < 0) {
        close(sv[0]); close(sv[1]);
        return -1;
    }
    if (p == 0) {
        // ---- 子进程 ----
        close(sv[0]);                                // 关闭父进程端
        dup2(sv[1], 0);  dup2(sv[1], 1);  dup2(sv[1], 2);  // 重定向 stdin/out/err
        close(sv[1]);
        _exit(upfs_session(0, 1));                   // 运行 Shell（不会返回）
    }
    // ---- 父进程 ----
    close(sv[1]);                                    // 关闭子进程端
    fcntl(sv[0], F_SETFL,
          fcntl(sv[0], F_GETFL, 0) | O_NONBLOCK);    // 设非阻塞模式
    return sv[0];                                    // 返回父进程端的 fd
}

/*
 * api_spawn - fork 一个 Web API 子进程
 *
 * 与 term_spawn 类似，但子进程运行 upfs_api_session()，
 * 该函数读取 JSON 请求，通过 VFS/内核执行操作，返回 JSON 响应。
 */
static int api_spawn(void)
{
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) return -1;

    pid_t p = fork();
    if (p < 0) {
        close(sv[0]); close(sv[1]);
        return -1;
    }
    if (p == 0) {
        // ---- 子进程 ----
        close(sv[0]);
        _exit(upfs_api_session(sv[1], sv[1]));       // 同一 fd 读写
    }
    // ---- 父进程 ----
    close(sv[1]);
    fcntl(sv[0], F_SETFL,
          fcntl(sv[0], F_GETFL, 0) | O_NONBLOCK);
    return sv[0];
}

/* ================================================================
 *  连接管理
 * ================================================================ */

/*
 * tcp_listen - 在指定端口创建 TCP 监听 socket
 *
 * 设置 SO_REUSEADDR 允许快速重启（避免 TIME_WAIT 占用端口）。
 * 返回监听 fd，失败返回 -1。
 */
static int tcp_listen(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;                  // 监听所有网络接口
    a.sin_port        = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
        close(fd); return -1;
    }
    if (listen(fd, 16) < 0) {                        // 内核 backlog=16
        close(fd); return -1;
    }
    return fd;
}

/*
 * conn_close - 关闭连接并释放 poll 槽位
 *
 * 关闭客户端 fd 和子进程通信 fd，重置所有状态。
 * g_pfds[i].fd 设为 -1 表示槽位空闲。
 */
static void conn_close(int i)
{
    if (g_conn[i].fd >= 0) {
        close(g_conn[i].fd);
        g_conn[i].fd = -1;
    }
    // 关闭子进程通信端，子进程读到 EOF 后会自行退出
    if (g_conn[i].term_fd >= 0) {
        close(g_conn[i].term_fd);
        g_conn[i].term_fd = -1;
    }
    g_conn[i].type = 0;
    g_conn[i].rlen = 0;
    g_conn[i].alen = 0;
    g_pfds[i].fd   = -1;
    g_pfds[i].events = 0;
}

/*
 * conn_alloc - 为新连接分配 Conn 与 pollfd 槽位
 *
 * 从索引 2 开始扫描（0=HTTP监听, 1=TCP监听被预留），
 * 找到第一个空闲槽位，初始化 Conn 并设置 poll 监听 POLLIN。
 * 返回槽位索引，-1 表示已满。
 */
static int conn_alloc(int fd, int type)
{
    for (int i = 2; i < MAX_CONN; i++) {             // 跳过两个监听 fd
        if (g_pfds[i].fd < 0) {                      // 空闲槽位
            memset(&g_conn[i], 0, sizeof(Conn));
            g_conn[i].fd   = fd;
            g_conn[i].type = type;
            g_conn[i].alen = 0;
            g_pfds[i].fd     = fd;
            g_pfds[i].events = POLLIN;               // 只监听可读事件
            if (i >= g_nfds) g_nfds = i + 1;         // 更新 poll 数组有效长度
            return i;
        }
    }
    return -1;                                       // 连接数已满
}



/* ================================================================
 *  serve_main - 主事件循环（HTTP + WebSocket + TCP 多终端服务器）
 *
 *  这是 --serve 模式的入口。函数永不返回（除非出错或信号中断）。
 *
 *  架构概览:
 *    poll 数组布局:
 *      g_pfds[0]  = HTTP 监听 socket（端口 8080）
 *      g_pfds[1]  = TCP 监听 socket（端口 4096 或自定义）
 *      g_pfds[2..]= 客户端连接
 *
 *    每轮循环:
 *      1. poll() 等待事件（超时 10ms，用于定期检查子进程输出）
 *      2. 清理异常连接（POLLERR/POLLHUP）
 *      3. 接受新连接（HTTP → T_HTTP, TCP → T_RAW）
 *      4. 处理已有连接的网络输入:
 *         - T_HTTP: 读 HTTP 请求，解析路径，决定返回页面或升级 WS
 *         - T_WS/T_API: 读 WS 帧，转发给子进程
 *         - T_RAW: 读原始 TCP 数据，转发给子进程
 *      5. 转发子进程输出到网络（T_WS→ws_send, T_API→按行分帧, T_RAW→直写）
 * ================================================================ */
int serve_main(int term_port)
{
    int l_http, l_term;

    if (term_port <= 0) term_port = TERM_PORT;       // 未指定端口则用默认 4096

    // 忽略 SIGPIPE（写已关闭 socket 时返回 EPIPE 而非杀死进程）
    // 忽略 SIGCHLD（子进程退出时自动回收，不产生僵尸进程）
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);

    /* ---- 初始化共享内存 ---- */
    // 匿名 mmap: 仅父子进程间共享（fork 后子进程继承该映射）
    g_shared = mmap(NULL, sizeof(*g_shared), PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (g_shared == MAP_FAILED) {
        fprintf(stderr, "mmap failed\n"); return 1;
    }
    memset(g_shared, 0, sizeof(*g_shared));

    // 内核共享内存: 进程表、调度器状态等（多个终端子进程需要看到同一组进程）
    if (kernel_shared_create() == NULL) {
        fprintf(stderr, "kernel_shared_create failed\n"); return 1;
    }

    /* ---- 自动探测磁盘镜像文件 ---- */
    // 从当前目录、父目录、祖父目录逐级搜索 DEFAULT_DISK_PATH
    {
        const char *dirs[] = { ".", "..", "../.." };
        for (size_t di = 0; di < sizeof(dirs) / sizeof(dirs[0]); di++) {
            char candidate[512];
            char resolved[512];
            snprintf(candidate, sizeof(candidate), "%s/%s",
                     dirs[di], DEFAULT_DISK_PATH);
            FILE *fp = fopen(candidate, "rb");
            if (fp) {
                fclose(fp);
                // 优先使用 realpath 解析的绝对路径，便于子进程在任何 cwd 下找到
                if (realpath(candidate, resolved) != NULL)
                    shared_set_disk(resolved);
                else
                    shared_set_disk(candidate);
                break;
            }
        }
    }

    /* ---- 初始化 poll 数组 ---- */
    memset(g_pfds, 0, sizeof(g_pfds));
    memset(g_conn, 0, sizeof(g_conn));
    for (int i = 0; i < MAX_CONN; i++) {
        g_pfds[i].fd = -1;
        g_conn[i].fd = -1;
    }

    // 启动两个监听 socket
    l_http = tcp_listen(HTML_PORT);                  // 8080: Web 桌面
    l_term = tcp_listen(term_port);                  // 4096: 原生 TCP 终端
    if (l_http < 0 || l_term < 0) {
        fprintf(stderr, "bind failed\n"); return 1;
    }

    // 前两个 pollfd 槽预留给监听 socket
    g_pfds[0].fd     = l_http;
    g_pfds[0].events = POLLIN;
    g_pfds[1].fd     = l_term;
    g_pfds[1].events = POLLIN;
    g_nfds = 2;

    printf("[upfsd] http :%d | term :%d\n", HTML_PORT, term_port);
    fflush(stdout);

    /* =================================================================
     *  主事件循环
     * ================================================================= */
    for (;;) {
        // ---- 等待事件（超时 10ms）----
        // 短超时确保即使没有网络事件，也能定期检查子进程输出
        int rc = poll(g_pfds, (nfds_t)g_nfds, 10);
        if (rc < 0 && errno != EINTR) break;         // 非信号中断则退出

        /* ---- 阶段 A: 清理异常连接 ---- */
        // 先处理 POLLERR/POLLHUP（不含 POLLIN 的——如果同时有 POLLIN
        // 则先读数据再处理，避免丢失最后的数据）
        for (int i = 2; i < g_nfds; i++) {
            if (g_pfds[i].fd < 0) continue;
            if ((g_pfds[i].revents & (POLLERR | POLLHUP)) &&
                !(g_pfds[i].revents & POLLIN)) {
                conn_close(i);
            }
        }

        /* ---- 阶段 B: 接受新连接 ---- */
        // li=0: HTTP 监听端口, li=1: TCP 终端监听端口
        for (int li = 0; li < 2; li++) {
            if (!(g_pfds[li].revents & POLLIN)) continue;

            struct sockaddr_in ca;
            socklen_t cl = sizeof(ca);
            int cfd = accept(g_pfds[li].fd, (struct sockaddr *)&ca, &cl);
            if (cfd < 0) continue;

            // 设置非阻塞模式（所有客户端 socket 均为非阻塞）
            fcntl(cfd, F_SETFL, fcntl(cfd, F_GETFL, 0) | O_NONBLOCK);

            int init_type = (li == 0) ? T_HTTP : T_RAW;  // HTTP 端口→T_HTTP, TCP 端口→T_RAW
            int slot = conn_alloc(cfd, init_type);
            if (slot < 0) {
                close(cfd);                          // 连接数已满，拒绝
                continue;
            }

            // 原生 TCP 连接立即 fork 终端子进程（不需要 HTTP 握手）
            if (init_type == T_RAW) {
                int tfd = term_spawn();
                if (tfd >= 0) g_conn[slot].term_fd = tfd;
            }
        }

        /* ---- 阶段 C: 处理客户端网络输入 ---- */
        for (int i = 2; i < g_nfds; i++) {
            int   fd = g_pfds[i].fd;
            if (fd < 0) continue;
            Conn *c  = &g_conn[i];

            /* ==== C1: HTTP 请求处理 ==== */
            // T_HTTP 连接首次收到数据，读取并解析 HTTP 请求
            if (c->type == T_HTTP && (g_pfds[i].revents & POLLIN)) {
                int room = BUF_SIZE - 1 - c->rlen;
                if (room <= 0) {
                    conn_close(i);                   // 缓冲区满，异常
                    continue;
                }
                int n = (int)read(fd, c->rbuf + c->rlen, (size_t)room);
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    // 非阻塞模式下暂时无数据，忽略
                }
                else if (n <= 0) {
                    conn_close(i);                   // 读出错或对端关闭
                    continue;
                }
                else {
                    c->rlen += n;
                    c->rbuf[c->rlen] = '\0';

                    // 检查 HTTP 请求头是否完整（以 \r\n\r\n 结束）
                    char *end = strstr(c->rbuf, "\r\n\r\n");
                    if (!end) continue;              // 头部未收完，等下次

                    // 解析请求行: "METHOD /path HTTP/1.1"
                    char *s1 = strchr(c->rbuf, ' ');
                    char *s2 = s1 ? strchr(s1 + 1, ' ') : NULL;
                    if (!s1 || !s2) {
                        conn_close(i);               // 格式错误
                        continue;
                    }
                    int    plen = (int)(s2 - s1 - 1);
                    char  *path = s1 + 1;            // 请求路径（不含首尾空格）

                    // 提取 Sec-WebSocket-Key 头（如果存在）
                    char *wsk = NULL;
                    {
                        char *ln   = s2 + 1; while (*ln == ' ') ln++;
                        char *nxt  = strstr(ln, "\r\n");
                        if (nxt) nxt += 2; else nxt = ln;

                        while (nxt < end && *nxt != '\r') {
                            char *eol = strstr(nxt, "\r\n");
                            if (!eol || eol >= end) break;
                            if (strncasecmp(nxt, "Sec-WebSocket-Key:", 18) == 0) {
                                wsk = nxt + 18;
                                while (*wsk == ' ') wsk++;     // 跳过空白
                                *eol = '\0';                   // 截断行尾便于字符串操作
                                break;
                            }
                            nxt = eol + 2;                     // 跳到下一行
                        }
                    }

                    /* ---- 路由分发 ---- */
                    if (wsk && plen >= 4 && strncmp(path, "/ws/", 4) == 0) {
                        // ===== WebSocket 终端升级 (/ws/N) =====
                        uint8_t hash[20];
                        char    akey[64], ext[256], comb[256];

                        // 1. 计算 Sec-WebSocket-Accept = Base64(SHA1(Key + GUID))
                        snprintf(comb, sizeof(comb), "%s%s", wsk, WS_GUID);
                        sha1_hash((uint8_t *)comb, (int)strlen(comb), hash);
                        b64enc(hash, 20, akey);

                        // 2. 发送 101 升级响应
                        snprintf(ext, sizeof(ext),
                                 "Upgrade: websocket\r\n"
                                 "Sec-WebSocket-Accept: %s\r\n", akey);
                        http_send(fd, 101, NULL, NULL, 0, ext);

                        // 3. 切换连接类型并 fork 终端子进程
                        c->type = T_WS;
                        c->rlen = 0;
                        g_pfds[i].revents = 0;       // 清除 revents 避免下一阶段误判

                        int tfd = term_spawn();
                        if (tfd >= 0) c->term_fd = tfd;

                    } else if (wsk && plen >= 4 && strncmp(path, "/api", 4) == 0) {
                        // ===== WebSocket API 升级 (/api) =====
                        uint8_t hash[20];
                        char    akey[64], ext[256], comb[256];

                        snprintf(comb, sizeof(comb), "%s%s", wsk, WS_GUID);
                        sha1_hash((uint8_t *)comb, (int)strlen(comb), hash);
                        b64enc(hash, 20, akey);
                        snprintf(ext, sizeof(ext),
                                 "Upgrade: websocket\r\n"
                                 "Sec-WebSocket-Accept: %s\r\n", akey);
                        http_send(fd, 101, NULL, NULL, 0, ext);

                        c->type = T_API;
                        c->rlen = 0;
                        g_pfds[i].revents = 0;

                        int tfd = api_spawn();       // 注意: 和终端不同，fork API 会话
                        if (tfd >= 0) c->term_fd = tfd;

                    } else if ((plen == 1 && path[0] == '/') ||
                               (plen >= 10 && strncmp(path, "/index.html", 11) == 0)) {
                        // ===== 普通 HTTP: 返回 Web 桌面 HTML =====
                        // WEB_PAGE 定义在 web_page.h 中，是内嵌的完整单页应用
                        http_send(fd, 200, "text/html; charset=utf-8",
                                  WEB_PAGE, (int)strlen(WEB_PAGE), NULL);
                        conn_close(i);               // HTTP/1.0 风格: 响应后关闭

                    } else {
                        // ===== 404 =====
                        const char *nope = "not found\n";
                        http_send(fd, 404, "text/plain", nope, (int)strlen(nope), NULL);
                        conn_close(i);
                    }
                }
            }

            /* ==== C2: WebSocket 帧处理（终端和 API）==== */
            // 从 WebSocket 连接读帧，转发到子进程的 stdin
            if ((c->type == T_WS || c->type == T_API) &&
                (g_pfds[i].revents & POLLIN)) {

                int loop_guard = 0;
                while (loop_guard < 64) {            // 防饥饿：最多连续处理 64 帧
                    uint8_t payload[BUF_SIZE];
                    int     plen;

                    // 预读更多数据到 rbuf
                    {
                        int room = BUF_SIZE - c->rlen - 1;
                        if (room > 0) {
                            int n = (int)read(c->fd,
                                    (uint8_t *)c->rbuf + c->rlen, (size_t)room);
                            if (n > 0) c->rlen += n;
                        }
                    }

                    // 尝试解析一帧
                    plen = ws_recv(c, payload, BUF_SIZE - 1);
                    if (plen < 0) {
                        conn_close(i);               // 帧错误或连接关闭
                        break;
                    }
                    if (plen == 0) break;            // 没有完整帧，等下次数据

                    // 转发到子进程
                    if (c->term_fd >= 0) {
                        payload[plen] = '\0';
                        // API 会话: 保证消息以换行结尾（JSON 行协议）
                        if (c->type == T_API) {
                            if (payload[plen - 1] != '\n') {
                                payload[plen]     = '\n';
                                plen++;
                            }
                        }
                        if (nb_write(c->term_fd, payload, (size_t)plen) < 0) {
                            conn_close(i);
                            break;
                        }
                    }
                    loop_guard++;
                }
                g_pfds[i].revents &= ~POLLIN;        // 清除已处理的 POLLIN 标志
            }

            /* ==== C3: 原生 TCP 数据处理 ==== */
            // 从 TCP 连接读原始字节，直接转发到终端子进程
            if (c->type == T_RAW && (g_pfds[i].revents & POLLIN)) {
                char buf[4096];
                int  n = (int)read(fd, buf, sizeof(buf));
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    // 暂无数据
                }
                else if (n <= 0) {
                    conn_close(i);
                }
                else if (n > 0 && c->term_fd >= 0) {
                    if (nb_write(c->term_fd, buf, (size_t)n) < 0)
                        conn_close(i);
                }
            }
        }

        /* ---- 阶段 D: 转发子进程输出到网络 ---- */
        // 用非阻塞 poll(0) 检查每个连接的 term_fd 是否有数据可读
        for (int i = 2; i < g_nfds; i++) {
            if (g_pfds[i].fd < 0) continue;
            Conn *c = &g_conn[i];
            if (c->term_fd < 0) continue;

            // 快速检查子进程通信 fd 是否可读
            struct pollfd pfd;
            pfd.fd     = c->term_fd;
            pfd.events = POLLIN;
            if (poll(&pfd, 1, 0) <= 0) continue;     // poll(0) 不阻塞
            if (!(pfd.revents & POLLIN)) continue;

            char buf[4096];
            int  n = (int)read(c->term_fd, buf, sizeof(buf));
            if (n <= 0) {
                // 子进程退出或出错，关闭通信端
                close(c->term_fd);
                c->term_fd = -1;
                continue;
            }

            // 根据连接类型选择不同的转发方式
            if (c->type == T_WS) {
                // WebSocket 终端: 直接作为一帧发送
                ws_send(c->fd, buf, n);
            } else if (c->type == T_API) {
                // WebSocket API: 按换行分割，每行作为独立的一帧
                // （JSON 行协议: 每个 JSON 对象一行）
                for (int bi = 0; bi < n; bi++) {
                    if (c->alen < BUF_SIZE - 1)
                        c->abuf[c->alen++] = buf[bi];
                    if (buf[bi] == '\n' || c->alen >= BUF_SIZE - 1) {
                        ws_send(c->fd, c->abuf, c->alen);
                        c->alen = 0;
                    }
                }
            } else if (c->type == T_RAW) {
                // 原生 TCP: 直接透传原始字节
                if (nb_write(c->fd, buf, (size_t)n) < 0)
                    conn_close(i);
            }
        }
    }

    close(l_http);
    close(l_term);
    return 0;
}
