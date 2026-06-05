#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 doc/汇报课件.html — 仅「完成功能点」罗列"""
import os
import html as htmlmod

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "doc", "汇报课件.html")

# 实色搭配：Tea Green · Celadon · Light Blue · Steel Blue · Rich Cerulean
C = {
    "tea": "#D0EFB1",
    "celadon": "#B3D89C",
    "light": "#9DC3C2",
    "steel": "#77A6B6",
    "cerulean": "#4D7298",
}

def esc(s):
    return htmlmod.escape(s)

def chunk_list(items, n):
    for i in range(0, len(items), n):
        yield items[i : i + n]

# (编号, 功能名, 说明) — 说明纯中文、简短
FEATURES_FS = [
    (1, "虚拟磁盘", "内存模拟磁盘，镜像持久保存与加载"),
    (2, "块组管理", "八组块区，锚块记录成组链接空闲块"),
    (3, "动态节点", "节点按块存放，双查找树定位与分配"),
    (4, "区段映射", "文件逻辑块映射物理块，相邻区段合并"),
    (5, "块缓存", "哈希加最近使用淘汰，六十四槽，可延迟写"),
    (6, "元数据日志", "修改先入日志，挂载时回放保证一致"),
    (7, "虚拟文件系统", "操作表统一接口，可挂载本文件系统"),
    (8, "打开文件表", "用户二十项、系统四十项、节点内存缓存"),
    (9, "路径解析", "支持绝对相对路径、当前目录与上级目录"),
    (10, "目录操作", "定长目录项，自动维护点与点点条目"),
    (11, "文件操作", "创建、打开、读写、关闭、删除、复制、链接"),
    (12, "权限控制", "属主、组、其他读写执行位，超级用户免检"),
]

FEATURES_KERNEL = [
    (13, "物理内存", "一百二十八兆，四千字节页，位图分配"),
    (14, "指令虚拟机", "三十二位精简指令，取指译码执行循环"),
    (15, "进程管理", "最多六十四个进程，复制、加载、等待、退出"),
    (16, "时间片调度", "每进程执行固定条数指令后轮转"),
    (17, "系统调用", "文件、进程、环境、堆扩展等二十余项"),
    (18, "管道通信", "匿名管道与命名管道，壳层支持多级串联"),
    (19, "进程间通信", "信号量、消息队列、共享内存、信号"),
]

FEATURES_USER = [
    (20, "多用户账户", "用户编号与组，口令文件存储"),
    (21, "口令安全", "随机盐值、万次迭代哈希存储"),
    (22, "用户切换", "登录、登出、切换用户、会话隔离"),
    (23, "环境变量", "系统级与用户级配置，写入磁盘持久化"),
]

FEATURES_TOOL = [
    (24, "汇编器", "汇编源码两遍扫描，生成可执行包"),
    (25, "语言编译器", "类 C 源码经词法语法生成汇编再汇编"),
    (26, "文本编辑器", "壳层内仿 vi 编辑磁盘文件"),
]

FEATURES_NET = [
    (27, "网页服务", "固定端口提供内嵌桌面页面"),
    (28, "网页终端", "多标签交互终端，子进程独立会话"),
    (29, "网页数据接口", "结构化请求列目录、读写文件、监控"),
    (30, "原始终端端口", "可用网络工具直连壳层"),
]

FEATURES_WEBUI = [
    (31, "文件浏览窗口", "树形目录、右键新建删除查看"),
    (32, "终端窗口", "多标签、彩色输出、自动滚动"),
    (33, "监视窗口", "块组、进程、内存使用情况定时刷新"),
]

FEATURES_EXTRA = [
    (34, "壳层命令集", "格式化、挂载、目录文件、用户、进程、调试等"),
    (35, "演示程序注入", "格式化时向系统目录写入示例可执行文件"),
    (36, "启动与桌面", "网页模式过渡动画、菜单栏、停靠栏、主题切换"),
    (37, "数据持久化", "卸载后重启挂载，文件与用户数据完整保留"),
    (38, "并发与锁", "内存节点读写锁，多进程服务共享内核状态"),
]

SHELL_GROUPS = [
    ("系统", "格式化、挂载、卸载"),
    ("目录", "建目录、进入目录、显示路径、列目录"),
    ("文件", "创建、写入、查看、删除、复制、链接、属性、改权限"),
    ("用户", "添加用户、登录、登出、切换、当前用户、改口令、列用户"),
    ("进程", "汇编、编译、运行、列进程、结束进程、建管道、管道串联"),
    ("环境", "显示环境、导出变量、删除变量"),
    ("编辑与调试", "打开编辑器、内部状态查看、帮助、清屏、退出"),
]

SLIDES = []  # (bg, title, subtitle, items)  items: list of (no, name, desc) or (cat, desc)


def add_feature_slide(bg, title, subtitle, items, cont=False):
    SLIDES.append({
        "bg": bg,
        "title": title + ("（续）" if cont else ""),
        "subtitle": subtitle,
        "items": items,
        "kind": "feat",
    })


def add_shell_slide(bg, title, groups, cont=False):
    SLIDES.append({
        "bg": bg,
        "title": title + ("（续）" if cont else ""),
        "subtitle": "",
        "groups": groups,
        "kind": "shell",
    })


# 封面
SLIDES.append({
    "kind": "cover",
    "bg": C["cerulean"],
    "title": "UPFS 已完成功能点",
    "subtitle": "文件系统 · 内核 · 用户 · 工具链 · 网络与界面",
})

# 总览
SLIDES.append({
    "kind": "overview",
    "bg": C["light"],
    "title": "功能总览",
    "lines": [
        "六大子系统，三十三项核心能力",
        "另含壳层命令、网页桌面与持久化等配套能力",
        "以下分页逐项罗列",
    ],
})

BG_CYCLE = [C["tea"], C["celadon"], C["light"]]

def emit_features(title, subtitle, features, per=5):
    for i, batch in enumerate(chunk_list(features, per)):
        bg = BG_CYCLE[i % len(BG_CYCLE)]
        add_feature_slide(bg, title, subtitle, batch, cont=(i > 0))


emit_features("文件系统层", "十二项", FEATURES_FS, 6)
emit_features("内核层", "七项", FEATURES_KERNEL, 4)
emit_features("用户管理层", "四项", FEATURES_USER, 4)
emit_features("开发工具链", "三项", FEATURES_TOOL, 3)
emit_features("网络服务层", "四项", FEATURES_NET, 4)
emit_features("网页界面", "三项", FEATURES_WEBUI, 3)
emit_features("配套能力", "五项", FEATURES_EXTRA, 5)

for i, batch in enumerate(chunk_list(SHELL_GROUPS, 4)):
    add_shell_slide(BG_CYCLE[i % 3], "壳层命令分类", batch, cont=(i > 0))

# 结束页
SLIDES.append({
    "kind": "cover",
    "bg": C["steel"],
    "title": "功能点罗列完毕",
    "subtitle": "后续模块将分步补充",
})

CSS = f"""
*{{box-sizing:border-box;margin:0;padding:0}}
html,body{{
  height:100%;overflow:hidden;
  font-family:"KaiTi","STKaiti","楷体","AR PL UKai CN",serif;
}}
#deck{{height:100%;width:100%;position:relative}}
.slide{{
  position:absolute;inset:0;
  padding:5vh 6vw 4vh;
  display:none;flex-direction:column;
  overflow:hidden;
}}
.slide.active{{display:flex}}
.head{{
  flex-shrink:0;
  padding-bottom:1.2vh;
  border-bottom:3px solid {C["cerulean"]};
  margin-bottom:1.5vh;
}}
.slide.on-dark .head{{border-bottom-color:rgba(255,255,255,.5)}}
h1{{
  font-size:clamp(2rem,4.5vh,3.2rem);
  font-weight:700;
  line-height:1.2;
  color:{C["cerulean"]};
}}
.slide.on-dark h1,.slide.on-dark .sub{{color:#fff}}
h2.subtitle{{
  font-size:clamp(1.1rem,2.2vh,1.45rem);
  color:{C["steel"]};
  margin-top:.35em;
  font-weight:400;
}}
.slide.on-dark .subtitle{{color:rgba(255,255,255,.9)}}
.body{{
  flex:1;min-height:0;
  overflow:hidden;
  display:flex;flex-direction:column;
  justify-content:flex-start;
  gap:0.6vh;
}}
.feat-row{{
  display:grid;
  grid-template-columns:2.2em 5.5em 1fr;
  gap:0.4em 0.8em;
  align-items:start;
  font-size:clamp(1.05rem,2.05vh,1.45rem);
  line-height:1.45;
  color:#1a1a1a;
  word-break:break-all;
  overflow-wrap:anywhere;
}}
.feat-row .no{{
  color:{C["cerulean"]};
  font-weight:700;
  text-align:right;
}}
.feat-row .name{{font-weight:700;color:#1a1a1a}}
.feat-row .desc{{color:#333}}
.shell-row{{
  font-size:clamp(1.05rem,2.05vh,1.45rem);
  line-height:1.5;
  color:#1a1a1a;
  word-break:break-all;
}}
.shell-row .cat{{
  color:{C["cerulean"]};
  font-weight:700;
  margin-right:.5em;
}}
.overview-line{{
  font-size:clamp(1.2rem,2.4vh,1.65rem);
  line-height:1.55;
  color:#1a1a1a;
  padding:.35em 0;
}}
.slide.cover-page{{
  justify-content:center;align-items:center;text-align:center;
}}
.slide.cover-page .head{{
  border:none;margin:0;padding:0;
}}
.slide.cover-page h1{{font-size:clamp(2.4rem,5.5vh,3.6rem)}}
.slide.cover-page .sub{{
  font-size:clamp(1.15rem,2.3vh,1.55rem);
  margin-top:1em;
  line-height:1.5;
}}
#progress{{
  position:fixed;top:0;left:0;height:5px;
  background:{C["cerulean"]};z-index:101;
}}
#num{{
  position:fixed;top:1.5vh;right:4vw;
  font-size:1rem;color:{C["cerulean"]};z-index:101;
}}
.slide.on-dark ~ #num{{color:#fff}}
#exit{{
  position:fixed;top:1.5vh;left:4vw;z-index:102;
  padding:.4em 1em;
  background:{C["cerulean"]};color:#fff;
  border:none;border-radius:4px;
  font-family:inherit;font-size:1rem;cursor:pointer;
}}
.hint{{
  position:fixed;bottom:1.5vh;right:4vw;
  font-size:.95rem;color:{C["steel"]};z-index:100;
}}
"""

def render_slide(s, i):
    bg = s.get("bg", C["tea"])
    on_dark = bg in (C["cerulean"], C["steel"])
    cls = "slide on-dark" if on_dark else "slide"
    if s["kind"] == "cover":
        cls += " cover-page"
        return (
            f'<section class="{cls}" data-i="{i}" style="background:{bg}">'
            f'<div class="head"><h1>{esc(s["title"])}</h1>'
            f'<p class="sub">{esc(s.get("subtitle",""))}</p></div></section>'
        )
    if s["kind"] == "overview":
        lines = "".join(f'<p class="overview-line">· {esc(x)}</p>' for x in s["lines"])
        return (
            f'<section class="{cls}" data-i="{i}" style="background:{bg}">'
            f'<div class="head"><h1>{esc(s["title"])}</h1></div>'
            f'<div class="body">{lines}</div></section>'
        )
    if s["kind"] == "feat":
        rows = ""
        for no, name, desc in s["items"]:
            rows += (
                f'<div class="feat-row">'
                f'<span class="no">{no}</span>'
                f'<span class="name">{esc(name)}</span>'
                f'<span class="desc">{esc(desc)}</span></div>'
            )
        sub = f'<h2 class="subtitle">{esc(s["subtitle"])}</h2>' if s.get("subtitle") else ""
        return (
            f'<section class="{cls}" data-i="{i}" style="background:{bg}">'
            f'<div class="head"><h1>{esc(s["title"])}</h1>{sub}</div>'
            f'<div class="body">{rows}</div></section>'
        )
    if s["kind"] == "shell":
        rows = ""
        for cat, desc in s["groups"]:
            rows += f'<p class="shell-row"><span class="cat">{esc(cat)}</span>{esc(desc)}</p>'
        return (
            f'<section class="{cls}" data-i="{i}" style="background:{bg}">'
            f'<div class="head"><h1>{esc(s["title"])}</h1></div>'
            f'<div class="body">{rows}</div></section>'
        )
    return ""

body = "".join(render_slide(s, i) for i, s in enumerate(SLIDES))

html_out = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>UPFS 功能点</title>
<style>{CSS}</style>
</head>
<body>
<div id="progress"></div>
<div id="num"></div>
<button id="exit" type="button">退出全屏</button>
<p class="hint">← → 空格翻页 · F 全屏</p>
<div id="deck">{body}</div>
<script>
(function(){{
  const slides=[...document.querySelectorAll('.slide')];
  let idx=0;
  const prog=document.getElementById('progress');
  const num=document.getElementById('num');
  function show(i){{
    idx=Math.max(0,Math.min(i,slides.length-1));
    slides.forEach((s,j)=>s.classList.toggle('active',j===idx));
    prog.style.width=((idx+1)/slides.length*100)+'%';
    num.textContent=(idx+1)+' / '+slides.length;
    const dark=slides[idx].classList.contains('on-dark');
    num.style.color=dark?'#fff':'{C["cerulean"]}';
  }}
  document.addEventListener('keydown',e=>{{
    if(e.key==='ArrowRight'||e.key===' '||e.key==='PageDown'){{e.preventDefault();show(idx+1);}}
    if(e.key==='ArrowLeft'||e.key==='PageUp'){{e.preventDefault();show(idx-1);}}
    if(e.key==='f'||e.key==='F'){{
      if(!document.fullscreenElement) document.documentElement.requestFullscreen().catch(()=>{{}});
      else document.exitFullscreen();
    }}
  }});
  document.getElementById('deck').addEventListener('click',e=>{{
    if(e.target.closest('#exit'))return;
    if(e.clientX>innerWidth*0.6) show(idx+1);
    else if(e.clientX<innerWidth*0.4) show(idx-1);
  }});
  document.getElementById('exit').onclick=()=>document.fullscreenElement&&document.exitFullscreen();
  show(0);
}})();
</script>
</body>
</html>"""

with open(OUT, "w", encoding="utf-8") as f:
    f.write(html_out)

print(f"已写入 {OUT}")
print(f"共 {len(SLIDES)} 页")
