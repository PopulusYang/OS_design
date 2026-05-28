






#include "serve.h"
#include "kernel_shared.h"

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

extern int upfs_session(int in_fd, int out_fd);



static struct {
    volatile int ready;
    char path[512];
} *g_shared;

const char *shared_disk_path(void) {
    if (!g_shared || !g_shared->ready) return NULL;
    return g_shared->path;
}

void shared_set_disk(const char *path) {
    if (!g_shared) return;
    strncpy(g_shared->path, path, sizeof(g_shared->path) - 1);
    g_shared->path[sizeof(g_shared->path) - 1] = '\0';
    __sync_synchronize();
    g_shared->ready = 1;
}



#define HTML_PORT     8080
#define TERM_PORT     4096
#define MAX_CONN      64
#define BUF_SIZE      8192
#define WS_GUID       "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


enum { T_HTTP, T_WS, T_RAW };


typedef struct {
    int fd;          
    int type;        
    int term_fd;     
    int term_pid;    
    char rbuf[BUF_SIZE]; 
    int  rlen;
} Conn;

static struct pollfd g_pfds[MAX_CONN];
static Conn         g_conn[MAX_CONN];
static int          g_nfds;



typedef struct { uint32_t s[5], c[2]; uint8_t b[64]; } SHA1;

static void sha1_t(uint32_t st[5], const uint8_t b[64]) {
    uint32_t w[80], a=st[0], e1=st[1], c=st[2], d=st[3], e=st[4], t; int i;
    for(i=0;i<16;i++) w[i]=((uint32_t)b[i*4]<<24)|((uint32_t)b[i*4+1]<<16)|((uint32_t)b[i*4+2]<<8)|(uint32_t)b[i*4+3];
    for(i=16;i<80;i++) w[i]=((w[i-3]^w[i-8]^w[i-14]^w[i-16])<<1)|((w[i-3]^w[i-8]^w[i-14]^w[i-16])>>31);
    for(i=0;i<80;i++){
        if(i<20) t=(e1&c)|((~e1)&d); else if(i<40) t=e1^c^d; else if(i<60) t=(e1&c)|(e1&d)|(c&d); else t=e1^c^d;
        t+=((a<<5)|(a>>27))+e+w[i];
        if(i<20) t+=0x5A827999; else if(i<40) t+=0x6ED9EBA1; else if(i<60) t+=0x8F1BBCDC; else t+=0xCA62C1D6;
        e=d;d=c;c=(e1<<30)|(e1>>2);e1=a;a=t;
    }
    st[0]+=a;st[1]+=e1;st[2]+=c;st[3]+=d;st[4]+=e;
}
static void sha1_u(SHA1 *c, const void *d, size_t n) {
    const uint8_t *p=(const uint8_t*)d; size_t i=0,j=(c->c[0]>>3)&63;
    if((c->c[0]+=(uint32_t)(n<<3))<(uint32_t)(n<<3))c->c[1]++;
    c->c[1]+=(uint32_t)(n>>29);
    if(j+n>=64){memcpy(c->b+j,p,64-j);sha1_t(c->s,c->b);for(i=64-j;i+63<n;i+=64)sha1_t(c->s,p+i);j=0;}else i=0;
    memcpy(c->b+j,p+i,n-i);
}
static void sha1_f(uint8_t dg[20], SHA1 *c) {
    uint8_t fc[8]; int i;
    for(i=0;i<8;i++)fc[i]=(uint8_t)((c->c[(i>=4)?0:1]>>((3-(i&3))*8))&255);
    uint8_t pad=0x80; sha1_u(c,&pad,1);
    while((c->c[0]&504)!=448){pad=0;sha1_u(c,&pad,1);}
    sha1_u(c,fc,8);
    for(i=0;i<20;i++)dg[i]=(uint8_t)((c->s[i>>2]>>((3-(i&3))*8))&255);
}
static void sha1_hash(const uint8_t *d, size_t n, uint8_t dg[20]) {
    SHA1 c; memset(&c,0,sizeof(c));
    c.s[0]=0x67452301;c.s[1]=0xEFCDAB89;c.s[2]=0x98BADCFE;c.s[3]=0x10325476;c.s[4]=0xC3D2E1F0;
    sha1_u(&c,d,n);sha1_f(dg,&c);
}
static const char B64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void b64enc(const uint8_t *in, int len, char *out) {
    int i,o=0;
    for(i=0;i<len;i+=3){
        uint32_t v=((uint32_t)(i<len?in[i]:0)<<16)|((uint32_t)(i+1<len?in[i+1]:0)<<8)|(uint32_t)(i+2<len?in[i+2]:0);
        out[o++]=B64[(v>>18)&63];out[o++]=B64[(v>>12)&63];
        out[o++]=(i+1<len)?B64[(v>>6)&63]:'=';out[o++]=(i+2<len)?B64[v&63]:'=';
    }
    out[o]='\0';
}



static int nb_write(int fd, const void *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, (const char *)buf + off, len - off);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd p = {.fd = fd, .events = POLLOUT};
                int ret = poll(&p, 1, 5000);
                if (ret <= 0) return -1;
                continue;
            }
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}



static void ws_send(int fd, const void *msg, int len) {
    uint8_t f[14]; int pos=0;
    f[pos++]=0x81;
    if(len<126){f[pos++]=(uint8_t)len;}
    else if(len<65536){f[pos++]=126;f[pos++]=(uint8_t)(len>>8);f[pos++]=(uint8_t)(len);}
    else{f[pos++]=127;memset(f+pos,0,4);pos+=4;
         f[pos++]=(uint8_t)(len>>24);f[pos++]=(uint8_t)(len>>16);
         f[pos++]=(uint8_t)(len>>8);f[pos++]=(uint8_t)(len);}
    if(nb_write(fd, f, pos) < 0) return;
    if(len > 0) nb_write(fd, msg, (size_t)len);
}

static int ws_recv(Conn *c, uint8_t *out, int max) {
    int fd = c->fd;

    
    int room = BUF_SIZE - c->rlen - 1;
    if (room > 0) {
        int n = (int)read(fd, (uint8_t *)c->rbuf + c->rlen, (size_t)room);
        if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) return -1;
        } else if (n == 0) {
            return -1;
        } else {
            c->rlen += n;
        }
    }

    if (c->rlen < 2) return 0;

    uint8_t *buf = (uint8_t *)c->rbuf;
    int op = buf[0] & 0x0F;
    if (op == 0x8) return -1;
    if (op == 0x9) {  
        uint8_t pong[2] = {0x8A, 0x00};
        nb_write(fd, pong, 2);
        
        int rem = c->rlen - 2;
        if (rem > 0) memmove(buf, buf + 2, (size_t)rem);
        c->rlen = rem;
        return 0;
    }

    int masked = buf[1] & 0x80;
    uint64_t plen = buf[1] & 0x7F;
    int hdr_len = 2;

    if (plen == 126) {
        if (c->rlen < 4) return 0;
        plen = ((uint64_t)buf[2] << 8) | buf[3];
        hdr_len = 4;
    } else if (plen == 127) {
        if (c->rlen < 10) return 0;
        plen = 0;
        for (int i = 0; i < 8; i++) plen = (plen << 8) | buf[2 + i];
        hdr_len = 10;
    }

    if (masked) {
        if (c->rlen < hdr_len + 4) return 0;
        hdr_len += 4;
    }

    if (plen > (uint64_t)max) return -1;

    int frame_len = hdr_len + (int)plen;
    if (c->rlen < frame_len) return 0;

    
    uint8_t *payload = buf + hdr_len;
    if (masked) {
        uint8_t *mk = buf + hdr_len - 4;
        for (uint64_t i = 0; i < plen; i++) payload[i] ^= mk[i & 3];
    }
    memcpy(out, payload, (size_t)plen);

    
    int remaining = c->rlen - frame_len;
    if (remaining > 0) memmove(buf, buf + frame_len, (size_t)remaining);
    c->rlen = remaining;

    return (int)plen;
}



static void http_send(int fd, int code, const char *ctype, const char *body, int blen, const char *extra) {
    char h[1024];
    
    if (code == 101) {
        int n = snprintf(h, sizeof(h),
            "HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\n%s\r\n",
            extra ? extra : "");
        nb_write(fd, h, (size_t)n);
        return;
    }
    int n=snprintf(h,sizeof(h),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n%s\r\n",
        code,code==200?"OK":"Error",
        ctype?ctype:"text/plain",blen,extra?extra:"");
    if(nb_write(fd,h,(size_t)n)<0) return;
    if(body&&blen>0) nb_write(fd,body,(size_t)blen);
}



static int term_spawn(void) {
    int sv[2];
    if(socketpair(AF_UNIX,SOCK_STREAM,0,sv)<0) return -1;
    pid_t p=fork();
    if(p<0){close(sv[0]);close(sv[1]);return -1;}
    if(p==0){
        close(sv[0]);
        dup2(sv[1],0);dup2(sv[1],1);dup2(sv[1],2);
        close(sv[1]);
        _exit(upfs_session(0,1));
    }
    close(sv[1]);
    fcntl(sv[0],F_SETFL,fcntl(sv[0],F_GETFL,0)|O_NONBLOCK);
    return sv[0];
}



static int tcp_listen(int port) {
    int fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0) return -1;
    int opt=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    struct sockaddr_in a;memset(&a,0,sizeof(a));
    a.sin_family=AF_INET;a.sin_addr.s_addr=INADDR_ANY;a.sin_port=htons((uint16_t)port);
    if(bind(fd,(struct sockaddr*)&a,sizeof(a))<0){close(fd);return -1;}
    if(listen(fd,16)<0){close(fd);return -1;}
    return fd;
}



static void conn_close(int i) {
    if(g_conn[i].fd>=0){close(g_conn[i].fd);g_conn[i].fd=-1;}
    
    if(g_conn[i].term_fd>=0){close(g_conn[i].term_fd);g_conn[i].term_fd=-1;}
    g_conn[i].type=0;g_conn[i].rlen=0;
    g_pfds[i].fd=-1;g_pfds[i].events=0;
}


static int conn_alloc(int fd, int type) {
    for(int i=2;i<MAX_CONN;i++){
        if(g_pfds[i].fd<0){
            memset(&g_conn[i],0,sizeof(Conn));
            g_conn[i].fd=fd;g_conn[i].type=type;
            g_pfds[i].fd=fd;g_pfds[i].events=POLLIN;
            if(i>=g_nfds)g_nfds=i+1;
            return i;
        }
    }
    return -1;
}



static const char PAGE[] =
"<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"UTF-8\">\n"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">\n"
"<title>UPFS term</title>\n<style>\n"
"*{margin:0;padding:0;box-sizing:border-box}\n"
"body{background:#000;color:#ccc;font:14px/1.4 'Courier New',monospace;height:100vh;display:flex;flex-direction:column}\n"
"#tabs{display:flex;background:#111;border-bottom:1px solid #333;flex-shrink:0;overflow-x:auto}\n"
".tab{padding:6px 14px;cursor:pointer;color:#888;border-right:1px solid #333;font-size:12px;white-space:nowrap;display:flex;align-items:center}\n"
".tab.on{background:#000;color:#fff;font-weight:bold}\n"
".tab:hover:not(.on){background:#1a1a1a;color:#aaa}\n"
".tab .x{margin-left:8px;color:#555;cursor:pointer;font-size:14px}\n"
".tab .x:hover{color:#f66}\n"
"#add{padding:6px 14px;cursor:pointer;color:#666;font-size:14px;border:none;background:none;flex-shrink:0}\n"
"#add:hover{color:#fff}\n"
"#terms{flex:1;overflow:hidden}\n"
".t{display:none;height:100%;padding:8px;overflow-y:auto;white-space:pre-wrap;word-break:break-all}\n"
".t.on{display:block}\n"
"#inp{display:flex;border-top:1px solid #333;background:#111;flex-shrink:0}\n"
"#pr{color:#888;padding:8px 0 8px 12px;font:14px 'Courier New',monospace}\n"
"#cmd{flex:1;background:none;border:none;color:#fff;font:14px 'Courier New',monospace;outline:none;padding:8px 8px 8px 0;caret-color:#fff}\n"
"::-webkit-scrollbar{width:6px}::-webkit-scrollbar-track{background:#000}::-webkit-scrollbar-thumb{background:#333}\n"
"</style>\n</head>\n<body>\n"
"<div id=\"tabs\"><button id=\"add\">+</button></div>\n<div id=\"terms\"></div>\n"
"<div id=\"inp\"><span id=\"pr\">$ </span><input id=\"cmd\" autofocus autocomplete=\"off\" spellcheck=\"false\"></div>\n"
"<script>\n"
"var M=8,T=[],A=0;\n"
"function H(s){"
"var o='',i=0,c='',f=null,b=null,bd=!1,dm=!1,sp=!1;"
"function sc(){var a=[];"
"if(bd)a.push('font-weight:bold');"
"if(dm)a.push('opacity:0.6');"
"if(f)a.push('color:rgb('+f.join(',')+')');"
"if(b)a.push('background:rgb('+b.join(',')+')');"
"return a.join(';');}"
"while(i<s.length){"
"if(s.charCodeAt(i)===27&&s[i+1]==='['){"
"var j=i+2;while(j<s.length&&s[j]!=='m')j++;if(j>=s.length)break;"
"var t=s.substring(i+2,j).split(';');var k=0;"
"while(k<t.length){var v=parseInt(t[k])||0;"
"if(v===0){f=null;b=null;bd=!1;dm=!1;k++;}"
"else if(v===1){bd=!0;k++;}"
"else if(v===2){dm=!0;k++;}"
"else if(v===38&&k+3<t.length&&parseInt(t[k+1])===2){f=[parseInt(t[k+2])||0,parseInt(t[k+3])||0,parseInt(t[k+4])||0];k+=5;}"
"else if(v===48&&k+3<t.length&&parseInt(t[k+1])===2){b=[parseInt(t[k+2])||0,parseInt(t[k+3])||0,parseInt(t[k+4])||0];k+=5;}"
"else{k++;}}"
"var nc=sc();if(nc!==c){if(sp){o+='</span>';sp=!1;}c=nc;if(c){o+='<span style=\"'+c+'\">';sp=!0;}}"
"i=j+1;"
"}else{"
"var ch=s[i];"
"if(ch==='&')o+='&amp;';else if(ch==='<')o+='&lt;';else if(ch==='>')o+='&gt;';else o+=ch;"
"i++;}"
"}"
"if(sp)o+='</span>';return o;}\n"
"function N(){\n"
" if(T.length>=M)return;\n"
" var i=T.length;\n"
" var w=new WebSocket('ws://'+location.host+'/ws/'+i);\n"
" w.binaryType='arraybuffer';\n"
" var d=document.createElement('div');d.className='t';\n"
" var o={id:i,ws:w,el:d,ok:0};\n"
" w.onopen=function(){o.ok=1};\n"
" w.onmessage=function(e){\n"
"  var s=typeof e.data=='string'?e.data:new TextDecoder().decode(e.data);\n"
"  d.innerHTML+=H(s);\n"
"  d.scrollTop=d.scrollHeight;\n"
" };\n"
" w.onclose=function(){o.ok=0;d.innerHTML+='\\n-- closed --\\n'};\n"
" T.push(o);document.getElementById('terms').appendChild(d);\n"
" B(i);if(T.length===1)S(0);\n"
"}\n"
"function B(i){\n"
" var b=document.createElement('span');b.className='tab';\n"
" var s=document.createElement('span');s.textContent='t'+i;\n"
" s.onclick=function(){S(i)};\n"
" b.appendChild(s);\n"
" var x=document.createElement('span');x.className='x';x.textContent=' x';\n"
" x.onclick=function(e){e.stopPropagation();C(i)};\n"
" b.appendChild(x);\n"
" document.getElementById('tabs').insertBefore(b,document.getElementById('add'));\n"
"}\n"
"function S(i){A=i;\n"
" var ts=document.querySelectorAll('.tab');\n"
" for(var j=0;j<ts.length;j++)ts[j].classList.toggle('on',j===i);\n"
" var es=document.querySelectorAll('.t');\n"
" for(var j=0;j<es.length;j++)es[j].classList.toggle('on',j===i);\n"
" document.getElementById('cmd').focus();\n"
"}\n"
"function C(i){\n"
" if(T.length<=1)return;\n"
" var t=T[i];if(t.ws.readyState===1)t.ws.close();\n"
" t.el.remove();\n"
" var ts=document.querySelectorAll('.tab');if(ts[i])ts[i].remove();\n"
" T.splice(i,1);\n"
" for(var j=i;j<T.length;j++)T[j].id=j;\n"
" var ts2=document.querySelectorAll('.tab');\n"
" for(var j=0;j<T.length;j++){\n"
"  var s=ts2[j].querySelector('span');if(s){s.textContent='t'+j;s.onclick=(function(k){return function(){S(k)}})(j)}\n"
"  var x=ts2[j].querySelector('.x');if(x){x.onclick=(function(k){return function(e){e.stopPropagation();C(k)}})(j)}\n"
" }\n"
" if(A>=T.length)A=T.length-1;S(A);\n"
"}\n"
"document.getElementById('add').onclick=N;\n"
"document.getElementById('cmd').onkeydown=function(e){\n"
" if(e.key==='Enter'){\n"
"  var t=T[A];if(t&&t.ok)t.ws.send(document.getElementById('cmd').value+'\\n');\n"
"  document.getElementById('cmd').value='';\n"
" }\n"
"};\n"
"N();\n</script>\n</body>\n</html>\n";



int serve_main(int term_port) {
    int l_http, l_term;
    if(term_port<=0) term_port=TERM_PORT;

    signal(SIGPIPE,SIG_IGN);
    signal(SIGCHLD,SIG_IGN);

    
    g_shared = mmap(NULL, sizeof(*g_shared), PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (g_shared == MAP_FAILED) { fprintf(stderr, "mmap failed\n"); return 1; }
    memset(g_shared, 0, sizeof(*g_shared));

    
    if (kernel_shared_create() == NULL) {
        fprintf(stderr, "kernel_shared_create failed\n"); return 1;
    }

    
    memset(g_pfds,0,sizeof(g_pfds));
    memset(g_conn,0,sizeof(g_conn));
    for(int i=0;i<MAX_CONN;i++){g_pfds[i].fd=-1;g_conn[i].fd=-1;}

    l_http=tcp_listen(HTML_PORT);
    l_term=tcp_listen(term_port);
    if(l_http<0||l_term<0){fprintf(stderr,"bind failed\n");return 1;}

    g_pfds[0].fd=l_http;g_pfds[0].events=POLLIN;
    g_pfds[1].fd=l_term;g_pfds[1].events=POLLIN;
    g_nfds=2;

    printf("[upfsd] http :%d | term :%d\n",HTML_PORT,term_port);
    fflush(stdout);

    for(;;){
        int rc=poll(g_pfds,(nfds_t)g_nfds,50);
        if(rc<0&&errno!=EINTR) break;

        
        for(int i=2;i<g_nfds;i++){
            if(g_pfds[i].fd<0) continue;
            if(g_pfds[i].revents&(POLLERR|POLLHUP)){
                fprintf(stderr,"[srv] slot=%d err/hup revents=0x%x, closing\n",i,g_pfds[i].revents);
                conn_close(i);
            }
        }

        
        for(int li=0;li<2;li++){
            if(!(g_pfds[li].revents&POLLIN)) continue;
            struct sockaddr_in ca;socklen_t cl=sizeof(ca);
            int cfd=accept(g_pfds[li].fd,(struct sockaddr*)&ca,&cl);
            if(cfd<0) continue;
            fcntl(cfd,F_SETFL,fcntl(cfd,F_GETFL,0)|O_NONBLOCK);
            int init_type=(li==0)?T_HTTP:T_RAW;
            int slot=conn_alloc(cfd,init_type);
            if(slot<0){close(cfd);continue;}
            
            if(init_type==T_RAW){
                int tfd=term_spawn();
                if(tfd>=0) g_conn[slot].term_fd=tfd;
            }
        }

        
        for(int i=2;i<g_nfds;i++){
            int fd=g_pfds[i].fd;
            if(fd<0) continue;
            Conn *c=&g_conn[i];

            
            if(c->type==T_HTTP&&(g_pfds[i].revents&POLLIN)){
                int room=BUF_SIZE-1-c->rlen;
                if(room<=0){conn_close(i);continue;}
                int n=(int)read(fd,c->rbuf+c->rlen,(size_t)room);
                if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK)){}
                else if(n<=0){conn_close(i);continue;}
                else {
                c->rlen+=n;c->rbuf[c->rlen]='\0';
                char *end=strstr(c->rbuf,"\r\n\r\n");
                if(!end){fprintf(stderr,"[http] waiting for header end (have %d bytes)\n",c->rlen);continue;}

                
                char *s1=strchr(c->rbuf,' ');
                char *s2=s1?strchr(s1+1,' '):NULL;
                if(!s1||!s2){fprintf(stderr,"[http] bad request line\n");conn_close(i);continue;}
                int plen=(int)(s2-s1-1);
                char *path=s1+1;
                fprintf(stderr,"[http] path=%.*s\n",plen,path);

                
                char *wsk=NULL;
                {   char *ln=s2+1;while(*ln==' ')ln++;
                    char *nxt=strstr(ln,"\r\n");if(nxt)nxt+=2;else nxt=ln;
                    while(nxt<end&&*nxt!='\r'){
                        char *eol=strstr(nxt,"\r\n");
                        if(!eol||eol>=end)break;
                        if(strncasecmp(nxt,"Sec-WebSocket-Key:",18)==0){
                            wsk=nxt+18;while(*wsk==' ')wsk++;*eol='\0';break;
                        }
                        nxt=eol+2;
                    }
                }
                fprintf(stderr,"[http] wsk=%s\n",wsk?wsk:"(null)");

                if(wsk&&plen>=4&&strncmp(path,"/ws/",4)==0){
                    
                    uint8_t hash[20];char akey[64],ext[256],comb[256];
                    snprintf(comb,sizeof(comb),"%s%s",wsk,WS_GUID);
                    sha1_hash((uint8_t*)comb,(int)strlen(comb),hash);
                    b64enc(hash,20,akey);
                    snprintf(ext,sizeof(ext),"Upgrade: websocket\r\nSec-WebSocket-Accept: %s\r\n",akey);
                    http_send(fd,101,NULL,NULL,0,ext);
                    c->type=T_WS;c->rlen=0;
                    fprintf(stderr,"[ws] slot=%d upgraded, akey=%s\n",i,akey);
                    g_pfds[i].revents=0;  
                    
                    int tfd=term_spawn();
                    fprintf(stderr,"[ws] slot=%d term_spawn → tfd=%d pid=%d\n",i,tfd,g_conn[i].term_pid);
                    if(tfd>=0) c->term_fd=tfd;
                }else if((plen==1&&path[0]=='/')||(plen>=10&&strncmp(path,"/index.html",11)==0)){
                    http_send(fd,200,"text/html; charset=utf-8",PAGE,(int)strlen(PAGE),NULL);
                    conn_close(i);
                }else{
                    const char *nope="not found\n";
                    http_send(fd,404,"text/plain",nope,(int)strlen(nope),NULL);
                    conn_close(i);
                }
                } 
            }

            
            if(c->type==T_WS&&(g_pfds[i].revents&POLLIN)){
                uint8_t payload[BUF_SIZE];
                int plen=ws_recv(c,payload,BUF_SIZE-1);
                if(plen<0){fprintf(stderr,"[ws] slot=%d recv err, closing\n",i);conn_close(i);}
                else if(plen>0&&c->term_fd>=0){
                    fprintf(stderr,"[ws] slot=%d recv %d bytes → term\n",i,plen);
                    if(nb_write(c->term_fd,payload,(size_t)plen)<0) conn_close(i);
                }
            }

            
            if(c->type==T_RAW&&(g_pfds[i].revents&POLLIN)){
                char buf[4096];int n=(int)read(fd,buf,sizeof(buf));
                if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK)){}
                else if(n<=0){conn_close(i);}
                else if(n>0&&c->term_fd>=0){
                    if(nb_write(c->term_fd,buf,(size_t)n)<0) conn_close(i);
                }
            }
        }

        
        for(int i=2;i<g_nfds;i++){
            if(g_pfds[i].fd<0) continue;
            Conn *c=&g_conn[i];
            if(c->term_fd<0) continue;

            struct pollfd pfd; pfd.fd=c->term_fd; pfd.events=POLLIN;
            if(poll(&pfd,1,0)<=0) continue;
            if(!(pfd.revents&POLLIN)) continue;

            char buf[4096];int n=(int)read(c->term_fd,buf,sizeof(buf));
            if(n<=0){
                fprintf(stderr,"[srv] slot=%d term_fd closed (n=%d, errno=%d)\n",i,n,errno);
                close(c->term_fd);c->term_fd=-1;continue;
            }

            if(c->type==T_WS) {
                fprintf(stderr,"[ws] slot=%d send %d bytes → client\n",i,n);
                ws_send(c->fd,buf,n);
            }
            else if(c->type==T_RAW){if(nb_write(c->fd,buf,(size_t)n)<0) conn_close(i);}
        }
    }

    close(l_http);close(l_term);
    return 0;
}
