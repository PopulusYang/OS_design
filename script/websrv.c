// websrv.c —— WebSocket ↔ UPFS TCP 桥接 + 终端网页
//
// 浏览器 WebSocket → websrv → TCP :4096 → UPFS daemon
// 监听 :8080，GET / 返回多终端网页，WS /ws 桥接到 UPFS TCP。

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <time.h>

// ---------- 常量 ----------

#define MAX_CLIENTS   32
#define BUF_SIZE      8192
#define PORT          8080
#define UPFS_PORT     4096
#define WS_GUID       "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

// ---------- 桥接会话 ----------

typedef struct {
    int ws_fd;       // 浏览器 WebSocket fd
    int upfs_fd;     // UPFS TCP fd
    int active;
} Bridge;

static Bridge g_bridges[MAX_CLIENTS];

// ---------- SHA1 ----------

typedef struct {
    uint32_t state[5], count[2];
    uint8_t  buf[64];
} SHA1_CTX;

#define SHA1_ROL(v,n) (((v) << (n)) | ((v) >> (32 - (n))))

static void sha1_transform(uint32_t st[5], const uint8_t buf[64])
{
    uint32_t w[80], a, b, c, d, e, t;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)buf[i*4]<<24)|((uint32_t)buf[i*4+1]<<16)|
               ((uint32_t)buf[i*4+2]<<8)|(uint32_t)buf[i*4+3];
    for (i = 16; i < 80; i++)
        w[i] = SHA1_ROL(w[i-3]^w[i-8]^w[i-14]^w[i-16], 1);
    a = st[0]; b = st[1]; c = st[2]; d = st[3]; e = st[4];
    for (i = 0; i < 80; i++) {
        if (i < 20)       t = (b & c) | ((~b) & d);
        else if (i < 40)  t = b ^ c ^ d;
        else if (i < 60)  t = (b & c) | (b & d) | (c & d);
        else              t = b ^ c ^ d;
        t += SHA1_ROL(a,5) + e + w[i];
        if (i < 20)       t += 0x5A827999;
        else if (i < 40)  t += 0x6ED9EBA1;
        else if (i < 60)  t += 0x8F1BBCDC;
        else              t += 0xCA62C1D6;
        e = d; d = c; c = SHA1_ROL(b,30); b = a; a = t;
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d; st[4] += e;
}

static void sha1_update(SHA1_CTX *c, const void *d, size_t len)
{
    const uint8_t *p = (const uint8_t *)d;
    size_t i = 0, j = (c->count[0]>>3) & 63;
    if ((c->count[0] += (uint32_t)(len<<3)) < (uint32_t)(len<<3)) c->count[1]++;
    c->count[1] += (uint32_t)(len>>29);
    if (j + len >= 64) {
        memcpy(c->buf+j, p, 64-j); sha1_transform(c->state, c->buf);
        for (i=64-j; i+63<len; i+=64) sha1_transform(c->state, p+i);
        j = 0;
    } else i = 0;
    memcpy(c->buf+j, p+i, len-i);
}

static void sha1_final(uint8_t dg[20], SHA1_CTX *c)
{
    uint8_t fc[8]; int i;
    for (i=0;i<8;i++) fc[i]=(uint8_t)((c->count[(i>=4)?1:0]>>((3-(i&3))*8))&255);
    uint8_t pad=0x80; sha1_update(c,&pad,1);
    while((c->count[0]&504)!=448){pad=0;sha1_update(c,&pad,1);}
    sha1_update(c,fc,8);
    for(i=0;i<20;i++) dg[i]=(uint8_t)((c->state[i>>2]>>((3-(i&3))*8))&255);
}

static void sha1(const uint8_t *d, size_t len, uint8_t dg[20])
{
    SHA1_CTX c; memset(&c,0,sizeof(c));
    c.state[0]=0x67452301; c.state[1]=0xEFCDAB89;
    c.state[2]=0x98BADCFE; c.state[3]=0x10325476;
    c.state[4]=0xC3D2E1F0;
    sha1_update(&c,d,len); sha1_final(dg,&c);
}

// ---------- Base64 ----------

static const char b64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int b64_encode(const uint8_t *in, int len, char *out)
{
    int i,o=0;
    for(i=0;i<len;i+=3){
        uint32_t v=((uint32_t)(i<len?in[i]:0)<<16)|((uint32_t)(i+1<len?in[i+1]:0)<<8)|(uint32_t)(i+2<len?in[i+2]:0);
        out[o++]=b64[(v>>18)&63]; out[o++]=b64[(v>>12)&63];
        out[o++]=(i+1<len)?b64[(v>>6)&63]:'='; out[o++]=(i+2<len)?b64[v&63]:'=';
    }
    out[o]='\0'; return o;
}

// ---------- WebSocket ----------

static void ws_send(int fd, const char *msg, int len)
{
    uint8_t f[14]; int pos=0;
    f[pos++]=0x81;
    if(len<126){f[pos++]=(uint8_t)len;}
    else if(len<65536){f[pos++]=126;f[pos++]=(uint8_t)(len>>8);f[pos++]=(uint8_t)(len&0xFF);}
    else{f[pos++]=127;memset(f+pos,0,4);pos+=4;
         f[pos++]=(uint8_t)(len>>24);f[pos++]=(uint8_t)((len>>16)&0xFF);
         f[pos++]=(uint8_t)((len>>8)&0xFF);f[pos++]=(uint8_t)(len&0xFF);}
    write(fd,f,pos); if(len>0) write(fd,msg,len);
}

static int ws_recv(int fd, uint8_t *payload, int max_len)
{
    uint8_t hdr[2];
    int n=(int)read(fd,hdr,2); if(n<=0) return -1;
    if((hdr[0]&0x0F)==0x8) return -1;
    uint64_t plen=hdr[1]&0x7F; uint8_t masked=hdr[1]&0x80;
    if(plen==126){uint8_t e[2];read(fd,e,2);plen=((uint64_t)e[0]<<8)|e[1];}
    else if(plen==127){uint8_t e[8];read(fd,e,8);plen=0;for(int i=0;i<8;i++)plen=(plen<<8)|e[i];}
    uint8_t mask[4]={0}; if(masked) read(fd,mask,4);
    if(plen>(uint64_t)max_len) plen=(uint64_t)max_len;
    int r=(int)read(fd,payload,(size_t)plen); if(r<=0) return -1;
    if(masked) for(int i=0;i<r;i++) payload[i]^=mask[i&3];
    return r;
}

static void ws_handshake_accept(const char *key, char *out)
{
    char c[256]; uint8_t h[20];
    snprintf(c,sizeof(c),"%s%s",key,WS_GUID);
    sha1((const uint8_t*)c,(int)strlen(c),h); b64_encode(h,20,out);
}

// ---------- HTTP ----------

static void http_send(int fd, int code, const char *ctype, const char *body, int blen, const char *extra)
{
    char h[1024];
    int n=snprintf(h,sizeof(h),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: %s\r\n%s\r\n",
        code, code==200?"OK":(code==101?"Switching Protocols":"Error"),
        ctype, blen, code==101?"Upgrade":"close", extra?extra:"");
    write(fd,h,(size_t)n);
    if(body&&blen>0) write(fd,body,(size_t)blen);
}

// ---------- 嵌入 HTML ----------

static const char g_html[] =
"<!DOCTYPE html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"<meta charset=\"UTF-8\">\n"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"<title>UPFS terminal</title>\n"
"<style>\n"
"*{margin:0;padding:0;box-sizing:border-box}\n"
"body{background:#000;color:#ccc;font:14px/1.4 'Courier New',monospace;height:100vh;display:flex;flex-direction:column}\n"
"#tabs{display:flex;background:#111;border-bottom:1px solid #333;flex-shrink:0;overflow-x:auto}\n"
".tab{padding:6px 14px;cursor:pointer;color:#888;border-right:1px solid #333;font-size:12px;white-space:nowrap;user-select:none;display:flex;align-items:center}\n"
".tab.active{background:#000;color:#fff;font-weight:bold}\n"
".tab:hover:not(.active){background:#1a1a1a;color:#aaa}\n"
".tab .close-btn{margin-left:8px;color:#555;cursor:pointer;font-size:14px}\n"
".tab .close-btn:hover{color:#f66}\n"
"#add-tab{padding:6px 14px;cursor:pointer;color:#666;font-size:14px;border:none;background:none;flex-shrink:0}\n"
"#add-tab:hover{color:#fff}\n"
"#terms{flex:1;overflow:hidden}\n"
".term{display:none;height:100%;padding:8px;overflow-y:auto;white-space:pre-wrap;word-break:break-all}\n"
".term.active{display:block}\n"
"#input-line{display:flex;border-top:1px solid #333;background:#111;flex-shrink:0}\n"
"#prompt{color:#888;padding:8px 0 8px 12px;white-space:nowrap;font:14px 'Courier New',monospace}\n"
"#cmd{flex:1;background:none;border:none;color:#fff;font:14px 'Courier New',monospace;outline:none;padding:8px 8px 8px 0;caret-color:#fff}\n"
"::-webkit-scrollbar{width:6px}\n"
"::-webkit-scrollbar-track{background:#000}\n"
"::-webkit-scrollbar-thumb{background:#333}\n"
"</style>\n"
"</head>\n"
"<body>\n"
"<div id=\"tabs\"><button id=\"add-tab\" title=\"New terminal\">+</button></div>\n"
"<div id=\"terms\"></div>\n"
"<div id=\"input-line\"><span id=\"prompt\">$ </span><input id=\"cmd\" autofocus autocomplete=\"off\" spellcheck=\"false\"></div>\n"
"<script>\n"
"const MAX_TERM=8;\n"
"let terms=[],activeIdx=0;\n"
"function esc(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')}\n"
"function newTab(){\n"
" if(terms.length>=MAX_TERM)return;\n"
" let i=terms.length;\n"
" let ws=new WebSocket('ws://'+location.host+'/ws/'+i);\n"
" ws.binaryType='arraybuffer';\n"
" let el=document.createElement('div');el.className='term';\n"
" let t={id:i,ws:ws,el:el,connected:false};\n"
" ws.onopen=function(){t.connected=true;};\n"
" ws.onmessage=function(e){\n"
"  let s=typeof e.data==='string'?e.data:new TextDecoder().decode(e.data);\n"
"  el.innerHTML+=esc(s);el.scrollTop=el.scrollHeight;\n"
" };\n"
" ws.onclose=function(){t.connected=false;el.innerHTML+='\\n-- closed --\\n';};\n"
" terms.push(t);\n"
" document.getElementById('terms').appendChild(el);\n"
" addTabBtn(i);\n"
" if(terms.length===1)switchTab(0);\n"
"}\n"
"function addTabBtn(i){\n"
" let b=document.createElement('span');b.className='tab';\n"
" let s=document.createElement('span');s.textContent='term '+i;\n"
" s.onclick=function(){switchTab(i)};\n"
" b.appendChild(s);\n"
" let x=document.createElement('span');x.className='close-btn';\n"
" x.textContent=' x';x.onclick=function(e){e.stopPropagation();closeTab(i)};\n"
" b.appendChild(x);\n"
" document.getElementById('tabs').insertBefore(b,document.getElementById('add-tab'));\n"
"}\n"
"function switchTab(i){\n"
" activeIdx=i;\n"
" let tabs=document.querySelectorAll('.tab');\n"
" tabs.forEach(function(t,j){t.classList.toggle('active',j===i)});\n"
" let els=document.querySelectorAll('.term');\n"
" els.forEach(function(e,j){e.classList.toggle('active',j===i)});\n"
" document.getElementById('cmd').focus();\n"
"}\n"
"function closeTab(i){\n"
" if(terms.length<=1)return;\n"
" let t=terms[i];\n"
" if(t.ws.readyState===WebSocket.OPEN)t.ws.close();\n"
" t.el.remove();\n"
" let tabs=document.querySelectorAll('.tab');\n"
" if(tabs[i])tabs[i].remove();\n"
" terms.splice(i,1);\n"
" for(let j=i;j<terms.length;j++){terms[j].id=j;}\n"
" let tabs2=document.querySelectorAll('.tab');\n"
" for(let j=0;j<terms.length;j++){\n"
"  let s=tabs2[j].querySelector('span');\n"
"  if(s){s.textContent='term '+j;s.onclick=(function(k){return function(){switchTab(k)}})(j);}\n"
"  let x=tabs2[j].querySelector('.close-btn');\n"
"  if(x){x.onclick=(function(k){return function(e){e.stopPropagation();closeTab(k)}})(j);}\n"
" }\n"
" if(activeIdx>=terms.length)activeIdx=terms.length-1;\n"
" switchTab(activeIdx);\n"
"}\n"
"document.getElementById('add-tab').onclick=newTab;\n"
"document.getElementById('cmd').onkeydown=function(e){\n"
" if(e.key==='Enter'){\n"
"  let inp=document.getElementById('cmd');\n"
"  let t=terms[activeIdx];\n"
"  if(t&&t.connected){t.ws.send(inp.value+'\\n');}\n"
"  inp.value='';\n"
" }\n"
"};\n"
"newTab();\n"
"</script>\n"
"</body>\n"
"</html>\n";

// ---------- 连接到 UPFS TCP ----------

static int upfs_connect(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(UPFS_PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    // 非阻塞模式
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    return fd;
}

// ---------- 主循环 ----------

int main(void)
{
    int listen_fd;
    struct sockaddr_in addr;
    struct pollfd fds[MAX_CLIENTS * 2 + 2];
    int nfds, i;

    signal(SIGPIPE, SIG_IGN);
    memset(g_bridges, 0, sizeof(g_bridges));

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORT);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(listen_fd, 16) < 0) { perror("listen"); return 1; }

    printf("listening on http://localhost:%d  (UPFS bridge to :%d)\n", PORT, UPFS_PORT);
    fflush(stdout);

    // poll 数组: [0]=listener, 其余按需填充
    memset(fds, 0, sizeof(fds));
    fds[0].fd     = listen_fd;
    fds[0].events = POLLIN;
    nfds = 1;

    // 每个 poll 槽位的类型: 0=listener, 1=HTTP client, 2=bridge ws, 3=bridge upfs
    int  slot_type[MAX_CLIENTS * 2 + 2];
    int  slot_bridge_idx[MAX_CLIENTS * 2 + 2]; // 桥接索引
    char http_buf[MAX_CLIENTS * 2 + 2][BUF_SIZE];
    int  http_buflen[MAX_CLIENTS * 2 + 2];

    memset(slot_type, 0, sizeof(slot_type));
    memset(slot_bridge_idx, -1, sizeof(slot_bridge_idx));
    memset(http_buflen, 0, sizeof(http_buflen));

    for (;;) {
        int rc = poll(fds, (nfds_t)nfds, 50);
        if (rc < 0 && errno != EINTR) { perror("poll"); break; }

        // 清理断开的连接
        for (i = 1; i < nfds; i++) {
            if (fds[i].fd < 0) continue;
            if (fds[i].revents & (POLLERR | POLLHUP)) {
                int bi = slot_bridge_idx[i];
                if (bi >= 0 && g_bridges[bi].active) {
                    // 断开桥接两端
                    if (g_bridges[bi].ws_fd >= 0)
                        close(g_bridges[bi].ws_fd);
                    if (g_bridges[bi].upfs_fd >= 0)
                        close(g_bridges[bi].upfs_fd);
                    // 清理对应的 poll 槽位
                    for (int k = 1; k < nfds; k++) {
                        if (slot_bridge_idx[k] == bi) { fds[k].fd = -1; slot_bridge_idx[k] = -1; }
                    }
                    g_bridges[bi].active = 0;
                }
                close(fds[i].fd);
                fds[i].fd = -1;
                slot_type[i] = 0;
            }
        }

        // 新 HTTP 连接
        if (fds[0].revents & POLLIN) {
            struct sockaddr_in ca; socklen_t cl = sizeof(ca);
            int cfd = accept(listen_fd, (struct sockaddr *)&ca, &cl);
            if (cfd >= 0 && nfds < (int)(sizeof(fds)/sizeof(fds[0]))) {
                fcntl(cfd, F_SETFL, fcntl(cfd, F_GETFL, 0) | O_NONBLOCK);
                fds[nfds].fd = cfd; fds[nfds].events = POLLIN;
                slot_type[nfds] = 1;
                slot_bridge_idx[nfds] = -1;
                http_buflen[nfds] = 0;
                nfds++;
            } else if (cfd >= 0) { close(cfd); }
        }

        // 处理 I/O
        for (i = 1; i < nfds; i++) {
            int fd = fds[i].fd;
            if (fd < 0) continue;

            if (slot_type[i] == 1 && (fds[i].revents & POLLIN)) {
                // ---- HTTP 模式 ----
                char *b  = http_buf[i];
                int  *bl = &http_buflen[i];
                int  room = BUF_SIZE - 1 - *bl;
                if (room <= 0) { close(fd); fds[i].fd = -1; continue; }
                int n = (int)read(fd, b + *bl, (size_t)room);
                if (n <= 0) { close(fd); fds[i].fd = -1; continue; }
                *bl += n; b[*bl] = '\0';

                char *end = strstr(b, "\r\n\r\n");
                if (!end) continue;

                // 解析路径
                char *path = strchr(b, ' ');
                if (!path) { close(fd); fds[i].fd = -1; continue; }
                char *pe = strchr(path + 1, ' ');
                if (!pe) { close(fd); fds[i].fd = -1; continue; }
                int plen = (int)(pe - path - 1);

                // 查找 WS key
                char *ws_key = NULL;
                {
                    char *hdr = pe + 1;
                    while (*hdr == ' ') hdr++;
                    char *line = strstr(hdr, "\r\n");
                    if (line) line += 2;
                    else line = hdr;
                    while (line < end && *line != '\r') {
                        char *eol = strstr(line, "\r\n");
                        if (!eol || eol >= end) break;
                        if (strncmp(line, "Sec-WebSocket-Key:", 18) == 0) {
                            ws_key = line + 18;
                            while (*ws_key == ' ') ws_key++;
                            *eol = '\0'; break;
                        }
                        line = eol + 2;
                    }
                }

                if (ws_key && plen > 4 && strncmp(path + 1, "/ws/", 4) == 0) {
                    // ---- WS 握手 + 建立 UPFS 桥接 ----
                    char akey[64]; ws_handshake_accept(ws_key, akey);
                    char extra[256];
                    snprintf(extra, sizeof(extra),
                             "Upgrade: websocket\r\nSec-WebSocket-Accept: %s\r\n", akey);
                    http_send(fd, 101, NULL, NULL, 0, extra);

                    // 连接到 UPFS
                    int ufd = upfs_connect();
                    // 分配桥接槽位
                    int bi = -1;
                    for (int j = 0; j < MAX_CLIENTS; j++) {
                        if (!g_bridges[j].active) { bi = j; break; }
                    }
                    if (bi >= 0 && ufd >= 0) {
                        g_bridges[bi].ws_fd   = fd;
                        g_bridges[bi].upfs_fd = ufd;
                        g_bridges[bi].active  = 1;
                        // WS 端
                        slot_type[i] = 2;
                        slot_bridge_idx[i] = bi;
                        fds[i].events = POLLIN;
                        // UPFS 端：加入 poll
                        if (nfds < (int)(sizeof(fds)/sizeof(fds[0]))) {
                            fds[nfds].fd = ufd;
                            fds[nfds].events = POLLIN;
                            slot_type[nfds] = 3;
                            slot_bridge_idx[nfds] = bi;
                            nfds++;
                        }
                    } else if (ufd >= 0) {
                        close(ufd);
                        close(fd);
                        fds[i].fd = -1;
                    } else {
                        // UPFS 未启动，关闭连接
                        ws_send(fd, "\r\nUPFS daemon not running on :4096\r\n", 40);
                        close(fd);
                        fds[i].fd = -1;
                    }
                } else if ((plen == 1 && path[1] == '/') ||
                           (plen == 11 && strncmp(path+1, "/index.html", 11) == 0)) {
                    http_send(fd, 200, "text/html; charset=utf-8", g_html, (int)strlen(g_html), NULL);
                    close(fd); fds[i].fd = -1;
                } else {
                    const char *nope = "not found\n";
                    http_send(fd, 404, "text/plain", nope, (int)strlen(nope), NULL);
                    close(fd); fds[i].fd = -1;
                }
            }

            // ---- 桥接数据转发 ----
            if ((slot_type[i] == 2 || slot_type[i] == 3) && (fds[i].revents & POLLIN)) {
                int bi = slot_bridge_idx[i];
                if (bi < 0 || !g_bridges[bi].active) continue;

                if (slot_type[i] == 2) {
                    // WS → UPFS
                    uint8_t payload[BUF_SIZE];
                    int plen = ws_recv(fd, payload, BUF_SIZE - 1);
                    if (plen < 0) {
                        close(g_bridges[bi].upfs_fd);
                        g_bridges[bi].upfs_fd = -1;
                        close(fd); fds[i].fd = -1;
                        g_bridges[bi].active = 0;
                    } else if (plen > 0 && g_bridges[bi].upfs_fd >= 0) {
                        write(g_bridges[bi].upfs_fd, payload, (size_t)plen);
                    }
                } else {
                    // UPFS → WS
                    char buf[4096];
                    int n = (int)read(fd, buf, sizeof(buf));
                    if (n <= 0) {
                        close(g_bridges[bi].ws_fd);
                        g_bridges[bi].ws_fd = -1;
                        close(fd); fds[i].fd = -1;
                        g_bridges[bi].active = 0;
                    } else if (n > 0 && g_bridges[bi].ws_fd >= 0) {
                        ws_send(g_bridges[bi].ws_fd, buf, n);
                    }
                }
            }
        }
    }

    close(listen_fd);
    return 0;
}
