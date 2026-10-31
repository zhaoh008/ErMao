#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 docs/ 下的 Markdown 生成一个「双击即开」的单文件 HTML 学习站。

特点：
  * 单文件：CSS/JS/图片全部内嵌，可以随便拷走；
  * 左侧章节树 + 顶部搜索 + 每章结构图 + 图片点击放大；
  * 深色/浅色主题、上一章/下一章、记住上次阅读位置。

用法：python tools/build_docs_site.py
产物：docs/site/ErMao-学习文档.html（并复制一份到工程根目录）
"""
from __future__ import annotations

import base64
import html
import io
import json
import os
import re
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
OUT_DIR = DOCS / "site"
OUT_FILE = OUT_DIR / "ErMao-学习文档.html"
ROOT_COPY = ROOT / "ErMao-学习文档.html"

# ---------------------------------------------------------------- 文档顺序
GUIDE = [
    ("../README.md", "总览"),
    ("00-ErMao引擎方案文档.md", "方案文档"),
    ("01-GAMES104-课程映射.md", "课程映射"),
    ("02-分章讲解.md", "分章讲解"),
    ("03-构建与运行.md", "构建与运行"),
    ("04-UI与ImGui编辑器.md", "UI 与编辑器"),
    ("05-第三人称关卡.md", "第三人称关卡"),
    ("06-审核修复记录.md", "审核修复记录"),
    ("07-v0.3-新功能.md", "v0.3 新功能"),
    ("08-学习指南.md", "学习指南"),
    ("09-打包与发布.md", "打包与发布"),
    ("assets-credits.md", "素材来源"),
    ("lectures/参考资料.md", "参考资料总表"),
]
LECTURES = sorted([p.name for p in (DOCS / "lectures").glob("L*.md")])


def doc_title(md: str, fallback: str) -> str:
    for line in md.splitlines():
        if line.startswith("# "):
            return line[2:].strip()
    return fallback


def slug_id(index: int) -> str:
    return f"doc-{index}"


# ---------------------------------------------------------------- 图片
_image_cache: dict[str, str] = {}


def image_data_uri(path: Path, max_width: int = 1180) -> str:
    key = str(path)
    if key in _image_cache:
        return _image_cache[key]
    try:
        img = Image.open(path)
    except Exception:
        return ""
    if img.mode in ("RGBA", "LA", "P"):
        img = img.convert("RGBA")
        # 有透明通道：保留 PNG，但缩一下尺寸
        if img.width > max_width:
            h = int(img.height * max_width / img.width)
            img = img.resize((max_width, h), Image.LANCZOS)
        buf = io.BytesIO()
        img.save(buf, format="PNG", optimize=True)
        mime = "image/png"
    else:
        img = img.convert("RGB")
        if img.width > max_width:
            h = int(img.height * max_width / img.width)
            img = img.resize((max_width, h), Image.LANCZOS)
        buf = io.BytesIO()
        img.save(buf, format="JPEG", quality=80, optimize=True, progressive=True)
        mime = "image/jpeg"
    data = base64.b64encode(buf.getvalue()).decode("ascii")
    uri = f"data:{mime};base64,{data}"
    _image_cache[key] = uri
    return uri


# ---------------------------------------------------------------- Markdown
FENCE_RE = re.compile(r"^```(\w*)\s*$")
TABLE_SEP_RE = re.compile(r"^\s*\|?[\s:|-]+\|[\s:|-]+\s*$")
HEADING_RE = re.compile(r"^(#{1,5})\s+(.*)$")
UL_RE = re.compile(r"^(\s*)[-*+]\s+(.*)$")
OL_RE = re.compile(r"^(\s*)(\d+)\.\s+(.*)$")


def inline(text: str) -> str:
    # 行内代码先占位，避免其它规则破坏它
    codes: list[str] = []

    def stash(m: re.Match) -> str:
        codes.append(m.group(1))
        return f"\x00{len(codes) - 1}\x00"

    text = re.sub(r"`([^`]+)`", stash, text)
    text = html.escape(text, quote=False)

    # 图片
    def img(m: re.Match) -> str:
        alt = m.group(1)
        src = m.group(2)
        return f'<img class="inline-img" src="{src}" alt="{html.escape(alt)}" loading="lazy">'

    text = re.sub(r'!\[([^\]]*)\]\(([^)]+)\)', img, text)
    # 链接
    text = re.sub(r'\[([^\]]+)\]\(([^)]+)\)',
                  lambda m: f'<a href="{m.group(2)}" target="_blank" rel="noopener">{m.group(1)}</a>', text)
    # 粗体 / 斜体 / 删除线
    text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)
    text = re.sub(r"(?<!\*)\*([^*\n]+)\*(?!\*)", r"<em>\1</em>", text)
    text = re.sub(r"~~([^~]+)~~", r"<del>\1</del>", text)

    def unstash(m: re.Match) -> str:
        return f"<code>{html.escape(codes[int(m.group(1))], quote=False)}</code>"

    return re.sub(r"\x00(\d+)\x00", unstash, text)


def convert_table(lines: list[str]) -> str:
    rows = []
    for line in lines:
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        rows.append(cells)
    if not rows:
        return ""
    head = rows[0]
    body = rows[2:] if len(rows) > 1 else []
    out = ['<div class="table-wrap"><table><thead><tr>']
    out += [f"<th>{inline(c)}</th>" for c in head]
    out.append("</tr></thead><tbody>")
    for row in body:
        out.append("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in row) + "</tr>")
    out.append("</tbody></table></div>")
    return "".join(out)


def markdown_to_html(md: str, doc_index: int, base_dir: Path) -> tuple[str, list[tuple[str, str, int]]]:
    """返回 (html, headings[(level, text, id)])"""
    lines = md.splitlines()
    out: list[str] = []
    headings: list[tuple[str, str, int]] = []
    heading_no = 0
    i = 0
    in_list = False
    list_stack: list[str] = []
    para: list[str] = []

    def flush_para() -> None:
        nonlocal para
        if not para:
            return
        text = " ".join(para).strip()
        para = []
        if not text:
            return
        # 整段只有一张图 → figure
        m = re.fullmatch(r"!\[([^\]]*)\]\(([^)]+)\)", text)
        if m:
            alt, src = m.group(1), m.group(2)
            out.append(
                f'<figure><img src="{src}" alt="{html.escape(alt)}" loading="lazy">'
                f'<figcaption>{inline(alt) or "图"}</figcaption></figure>')
        else:
            out.append(f"<p>{inline(text)}</p>")

    def close_list() -> None:
        nonlocal in_list, list_stack
        while list_stack:
            out.append(f"</{list_stack.pop()}>")
        in_list = False

    while i < len(lines):
        line = lines[i]

        # ``` 代码块
        m = FENCE_RE.match(line)
        if m:
            flush_para()
            close_list()
            lang = m.group(1) or "text"
            i += 1
            code: list[str] = []
            while i < len(lines) and not lines[i].startswith("```"):
                code.append(lines[i])
                i += 1
            i += 1
            body = html.escape("\n".join(code), quote=False)
            out.append(
                f'<div class="code-block" data-lang="{lang}">'
                f'<button class="copy-btn" type="button">复制</button>'
                f'<pre><code class="language-{lang}">{body}</code></pre></div>')
            continue

        # 标题
        m = HEADING_RE.match(line)
        if m:
            flush_para()
            close_list()
            level = len(m.group(1))
            text = m.group(2).strip()
            heading_no += 1
            hid = f"h{doc_index}-{heading_no}"
            headings.append((f"h{min(level + 0, 5)}", text, level))
            out.append(f'<h{level} id="{hid}">{inline(text)}<a class="anchor" href="#{hid}">#</a></h{level}>')
            i += 1
            continue

        # 表格
        if line.strip().startswith("|") and i + 1 < len(lines) and TABLE_SEP_RE.match(lines[i + 1]):
            flush_para()
            close_list()
            block = [line]
            i += 1
            while i < len(lines) and lines[i].strip().startswith("|"):
                block.append(lines[i])
                i += 1
            out.append(convert_table(block))
            continue

        # 分隔线
        if re.match(r"^\s*---+\s*$", line):
            flush_para()
            close_list()
            out.append("<hr>")
            i += 1
            continue

        # 引用
        if line.startswith(">"):
            flush_para()
            close_list()
            quote = []
            while i < len(lines) and lines[i].startswith(">"):
                quote.append(lines[i][1:].strip())
                i += 1
            out.append(f"<blockquote>{inline(' '.join(quote))}</blockquote>")
            continue

        # 列表
        m_ul = UL_RE.match(line)
        m_ol = OL_RE.match(line)
        if m_ul or m_ol:
            flush_para()
            indent = len((m_ul or m_ol).group(1).expandtabs(2))
            tag = "ul" if m_ul else "ol"
            content = (m_ul.group(2) if m_ul else m_ol.group(3)).strip()
            if not in_list:
                out.append(f"<{tag}>")
                list_stack.append(tag)
                in_list = True
            elif list_stack and list_stack[-1] != tag:
                out.append(f"</{list_stack.pop()}>")
                out.append(f"<{tag}>")
                list_stack.append(tag)
            cls = ' class="task"' if content.startswith("[ ]") or content.startswith("[x]") else ""
            if cls:
                done = content.startswith("[x]")
                content = content[3:].strip()
                out.append(f'<li class="task"><span class="checkbox">{"☑" if done else "☐"}</span>{inline(content)}</li>')
            else:
                out.append(f"<li>{inline(content)}</li>")
            i += 1
            continue

        # 空行
        if not line.strip():
            flush_para()
            close_list()
            i += 1
            continue

        para.append(line.strip())
        i += 1

    flush_para()
    close_list()
    return "\n".join(out), headings


# ---------------------------------------------------------------- 章节结构图（SVG）
def structure_svg(headings: list[tuple[str, str, int]], title: str) -> str:
    items = [h for h in headings if h[2] == 2][:9]
    if len(items) < 2:
        items = [h for h in headings if h[2] <= 3][:9]
    if not items:
        return ""
    cols = 3
    box_w, box_h, gap = 260, 54, 18
    rows = (len(items) + cols - 1) // cols
    width = cols * box_w + (cols - 1) * gap
    height = rows * (box_h + gap) + 26
    parts = [f'<svg class="struct-map" viewBox="0 0 {width} {height}" xmlns="http://www.w3.org/2000/svg" role="img">',
             f'<text x="0" y="14" class="map-title">{html.escape(title)} · 本章结构</text>']
    for idx, (_, text, _level) in enumerate(items):
        col = idx % cols
        row = idx // cols
        x = col * (box_w + gap)
        y = 26 + row * (box_h + gap)
        label = re.sub(r"【[^】]*】", "", text).strip()
        if len(label) > 18:
            label = label[:17] + "…"
        parts.append(f'<rect x="{x}" y="{y}" width="{box_w}" height="{box_h}" rx="10" class="map-box"/>')
        parts.append(f'<text x="{x + 16}" y="{y + 33}" class="map-text">{html.escape(label)}</text>')
        if col < cols - 1 and idx + 1 < len(items):
            parts.append(f'<path d="M {x + box_w} {y + box_h / 2} h {gap}" class="map-arrow"/>')
    parts.append("</svg>")
    return "".join(parts)



# ---------------------------------------------------------------- 讲义配图（原理图 + 截图）
LECTURE_STEPS = {
    "L00": ["引擎全景", "分层依赖", "学习路线", "怎么跑起来"],
    "L01": ["输入 Poll", "Timer::Tick", "固定步长 Update", "BuildRenderScene", "Renderer::Render", "ImGui + Present"],
    "L02": ["core 基础", "rhi 图形接口", "renderer 渲染", "scene 世界", "gameplay 玩法", "editor + runtime"],
    "L03": ["本地 TRS", "父级矩阵", "世界矩阵", "视图矩阵", "投影矩阵", "NDC"],
    "L04": ["Shadow", "Sky", "Opaque + GPU 剔除", "Skinned", "Particles", "Post → Backbuffer"],
    "L05": ["N / L / V 向量", "D 法线分布", "G 几何遮蔽", "F 菲涅尔", "BRDF 求和", "ACES + Gamma"],
    "L06": ["光源视口", "深度图 2048²", "PCF 3×3", "阴影因子", "PBR 直接光"],
    "L07": ["HDR RGBA16F", "Bloom", "ACES Tonemap", "Gamma", "输出"],
    "L08": ["绑定姿势 B", "逆绑定 B⁻¹", "P·B⁻¹", "权重混合 Σw", "蒙皮网格"],
    "L09": ["Clip A", "Clip B", "时间对齐", "Lerp / Slerp", "状态机"],
    "L10": ["力 → 加速度", "速度积分", "位置积分", "碰撞检测", "位置修正"],
    "L11": ["AABB 粗测", "SAT 细测", "接触点", "冲量", "分离"],
    "L12": ["发射", "速度/重力", "寿命衰减", "排序/混合", "billboard"],
    "L13": ["Dock 布局", "视口 + Gizmo", "大纲", "检视器", "资源 / 控制台"],
    "L14": ["TypeInfo", "JSON 写", "JSON 读", "工厂重建", "字节一致"],
    "L15": ["磁盘 assets", "导入", "缓存", "引用", "释放"],
    "L16": ["主线程", "Job 队列", "工作线程", "完成计数", "合并结果"],
    "L17": ["CPU 提交", "GPU 执行", "等待", "16.6ms 预算", "优化点"],
    "L18": ["客户端输入", "服务器权威", "快照", "插值", "校正"],
    "L19": ["状态同步", "增量", "预测", "回滚"],
    "L20": ["CMake", "MSVC 编译", "链接", "复制 assets", "运行"],
    "L21": ["环境光", "SH 9 系数", "卷积", "重建", "PBR 环境项"],
    "L22": ["实例缓冲", "Compute 剔除", "压缩列表", "间接参数", "DrawIndexedIndirect"],
    "L23": ["正交相机", "quad", "UV 序列帧", "按纹理合批", "HUD"],
    "L24": ["OBJ 解析", "顶点去重", "GPU 上传", "材质", "场景"],
    "L25": ["WAV / PCM", "XAudio2 voice", "混音", "主音量", "输出"],
    "L26": ["Instance", "Device", "Swapchain", "Pipeline", "Present / 截图"],
}
LECTURE_SHOTS = {
    "L05": ["figures/fig-material-grid.png"],
    "L06": ["figures/fig-shadow-compare.png"],
    "L08": ["screenshots/skinning.png"],
    "L12": ["screenshots/particles.png"],
    "L13": ["screenshots/editor.png", "screenshots/gizmo-rotate.png"],
    "L22": ["screenshots/gpu-cull.png"],
    "L23": ["screenshots/survivors.png"],
    "L24": ["screenshots/survivors3d.png"],
}


def flow_svg(steps: list[str], title: str, per_row: int = 4) -> str:
    box_w, box_h, gap_x, gap_y = 190, 58, 34, 26
    rows = (len(steps) + per_row - 1) // per_row
    width = per_row * box_w + (per_row - 1) * gap_x
    height = rows * box_h + (rows - 1) * gap_y + 30
    parts = [f'<svg class="flow-map" viewBox="0 0 {width} {height}" xmlns="http://www.w3.org/2000/svg">',
             f'<text x="0" y="12" class="map-title">{html.escape(title)}</text>']
    for idx, step in enumerate(steps):
        col, row = idx % per_row, idx // per_row
        x = col * (box_w + gap_x)
        y = 26 + row * (box_h + gap_y)
        label = step if len(step) <= 12 else step[:11] + "…"
        parts.append(f'<rect x="{x}" y="{y}" width="{box_w}" height="{box_h}" rx="10" class="map-box"/>')
        parts.append(f'<text x="{x + box_w / 2}" y="{y + box_h / 2 + 5}" text-anchor="middle" class="map-text">{html.escape(label)}</text>')
        if col < per_row - 1 and idx + 1 < len(steps) and (idx + 1) // per_row == row:
            parts.append(f'<path d="M {x + box_w} {y + box_h / 2} h {gap_x}" class="map-arrow"/>')
        if col == per_row - 1 and idx + 1 < len(steps):
            parts.append(f'<path d="M {x + box_w / 2} {y + box_h} v {gap_y}" class="map-arrow"/>')
    parts.append("</svg>")
    return "".join(parts)


def figures_html(paths: list[str]) -> str:
    out = []
    for rel in paths:
        p = DOCS / rel
        if not p.exists():
            continue
        uri = image_data_uri(p)
        if not uri:
            continue
        caption = p.stem.replace("fig-", "").replace("-", " ")
        out.append(f'<figure><img src="{uri}" alt="{html.escape(caption)}" loading="lazy">'
                   f'<figcaption>{html.escape(caption)}</figcaption></figure>')
    return "".join(out)


DOC_SHOTS = {
    "00-ErMao引擎方案文档": ["screenshots/editor.png", "screenshots/demo-wave.png"],
    "07-v0.3-新功能": ["screenshots/demo-play.png", "screenshots/demo-wave.png"],
    "09-打包与发布": ["screenshots/survivors3d-menu.png"],
}
def lecture_extras(path: Path) -> str:
    stem = path.stem
    key = stem.split("-")[0]
    extra = []
    if key in LECTURE_STEPS:
        extra.append(flow_svg(LECTURE_STEPS[key], f"{stem} · 数据流"))
    if key in LECTURE_SHOTS:
        extra.append(figures_html(LECTURE_SHOTS[key]))
    return "".join(extra)

# ---------------------------------------------------------------- 主流程
def main() -> int:
    docs: list[dict] = []
    entries: list[tuple[Path, str, str]] = []  # (path, group, title)

    for name, title in GUIDE:
        p = DOCS / name
        if p.exists():
            entries.append((p, "入门与工程", title))
    lect_readme = DOCS / "lectures" / "README.md"
    if lect_readme.exists():
        entries.append((lect_readme, "GAMES104 讲义", "讲义索引"))
    for name in LECTURES:
        p = DOCS / "lectures" / name
        md = p.read_text(encoding="utf-8")
        entries.append((p, "GAMES104 讲义", doc_title(md, p.stem)))

    doc_ids = {p.resolve(): slug_id(i) for i, (p, _g, _t) in enumerate(entries)}

    index = 0
    for path, group, fallback_title in entries:
        md = path.read_text(encoding="utf-8")
        title = doc_title(md, fallback_title)
        body, headings = markdown_to_html(md, index, path.parent)
        # 图片解析：把相对路径替换成 data URI
        def repl(m: re.Match) -> str:
            src = m.group(1)
            if src.startswith("http") or src.startswith("data:"):
                return m.group(0)
            target = (path.parent / src).resolve()
            if target.exists():
                uri = image_data_uri(target)
                if uri:
                    return m.group(0).replace(src, uri)
            return m.group(0)

        body = re.sub(r'src="([^"]+)"', repl, body)

        # 把 markdown 之间的 .md 链接改成锚点，保证点击能跳章
        def link_repl(m):
            target = (path.parent / m.group(1)).resolve()
            doc_id = doc_ids.get(target)
            return f'href="#{doc_id}"' if doc_id else m.group(0)

        body = re.sub(r'href="([^"]+\.md)(#[^"]*)?"', link_repl, body)
        extra = lecture_extras(path) if "lectures" in str(path) else ""
        if not extra:
            for key, shots in DOC_SHOTS.items():
                if key in path.name:
                    extra = figures_html(shots)
                    break
        map_svg = structure_svg(headings, title) + extra
        docs.append({
            "id": slug_id(index),
            "index": index,
            "group": group,
            "title": title,
            "path": str(path.relative_to(ROOT)).replace("\\", "/"),
            "html": (map_svg + body) if map_svg else body,
            "h2": [h[1] for h in headings if h[2] == 2][:12],
        })
        index += 1

    # 搜索索引（纯文本）
    for d in docs:
        text = re.sub(r"<[^>]+>", " ", d["html"])
        text = re.sub(r"\s+", " ", html.unescape(text))
        d["text"] = text[:20000]

    payload = json.dumps([{
        "id": d["id"], "group": d["group"], "title": d["title"],
        "path": d["path"], "h2": d["h2"], "text": d["text"],
    } for d in docs], ensure_ascii=False)

    # 侧边栏
    nav_parts: list[str] = []
    current_group = None
    for d in docs:
        if d["group"] != current_group:
            if current_group is not None:
                nav_parts.append("</div>")
            nav_parts.append(f'<div class="nav-group"><div class="nav-group-title">{html.escape(d["group"])}</div>')
            current_group = d["group"]
        nav_parts.append(
            f'<a class="nav-item" data-id="{d["id"]}" href="#{d["id"]}">'
            f'<span class="nav-title">{html.escape(d["title"])}</span>'
            f'<span class="nav-path">{html.escape(d["path"].split("/")[-1])}</span></a>')
    nav_parts.append("</div>")

    sections = "\n".join(
        f'<section class="doc" id="{d["id"]}" data-title="{html.escape(d["title"])}">'
        f'<div class="doc-meta">{html.escape(d["path"])}</div>'
        f'<article>{d["html"]}</article></section>'
        for d in docs)

    html_doc = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ErMao 引擎 · 学习文档</title>
<style>
:root {{
  --bg: #0f1115; --bg-soft: #151922; --panel: #1a1f2b; --border: #2a3140;
  --text: #dfe4ee; --muted: #98a2b3; --accent: #f0b429; --accent-2: #5aa9ff;
  --code-bg: #11151c; --table-stripe: #171c26;
}}
html[data-theme="light"] {{
  --bg: #f6f7fb; --bg-soft: #ffffff; --panel: #ffffff; --border: #e2e6ef;
  --text: #1f2430; --muted: #5b6472; --accent: #b45309; --accent-2: #1d4ed8;
  --code-bg: #f3f5f9; --table-stripe: #f7f9fc;
}}
* {{ box-sizing: border-box; }}
html, body {{ margin: 0; height: 100%; }}
body {{
  background: var(--bg); color: var(--text);
  font: 16px/1.75 "Segoe UI", "Microsoft YaHei", system-ui, sans-serif;
  display: flex; flex-direction: column;
}}
a {{ color: var(--accent-2); text-decoration: none; }}
a:hover {{ text-decoration: underline; }}
#progress {{ position: fixed; top: 0; left: 0; height: 3px; width: 0; background: var(--accent); z-index: 50; }}
header {{
  display: flex; align-items: center; gap: 14px; padding: 10px 18px;
  background: var(--bg-soft); border-bottom: 1px solid var(--border); z-index: 20;
}}
.brand {{ font-weight: 700; font-size: 17px; white-space: nowrap; }}
.brand small {{ color: var(--muted); font-weight: 400; margin-left: 8px; font-size: 12px; }}
#search {{
  flex: 1; max-width: 460px; padding: 9px 12px; border-radius: 9px;
  border: 1px solid var(--border); background: var(--panel); color: var(--text); font-size: 14px;
}}
#search:focus {{ outline: 2px solid var(--accent-2); border-color: transparent; }}
.header-actions {{ margin-left: auto; display: flex; gap: 8px; }}
.icon-btn {{
  padding: 7px 12px; border-radius: 8px; border: 1px solid var(--border);
  background: var(--panel); color: var(--text); cursor: pointer; font-size: 13px;
}}
.icon-btn:hover {{ border-color: var(--accent); }}
.shell {{ display: flex; flex: 1; min-height: 0; }}
aside {{
  width: 300px; min-width: 300px; overflow-y: auto; padding: 14px 10px 40px;
  background: var(--bg-soft); border-right: 1px solid var(--border);
}}
.nav-group-title {{ color: var(--muted); font-size: 12px; letter-spacing: .12em; margin: 16px 10px 6px; }}
.nav-item {{
  display: flex; flex-direction: column; padding: 7px 10px; border-radius: 8px;
  color: var(--text); border: 1px solid transparent;
}}
.nav-item:hover {{ background: var(--panel); text-decoration: none; }}
.nav-item.active {{ background: var(--panel); border-color: var(--accent); }}
.nav-title {{ font-size: 14px; }}
.nav-path {{ color: var(--muted); font-size: 11px; }}
main {{ flex: 1; overflow-y: auto; scroll-behavior: smooth; padding: 28px 40px 120px; }}
#content {{ max-width: 980px; margin: 0 auto; }}
.doc {{ display: none; }}
.doc.active {{ display: block; animation: fade .18s ease; }}
@keyframes fade {{ from {{ opacity: 0; transform: translateY(4px); }} to {{ opacity: 1; transform: none; }} }}
.doc-meta {{ color: var(--muted); font-size: 12px; margin-bottom: 8px; font-family: Consolas, monospace; }}
article h1 {{ font-size: 30px; line-height: 1.35; margin: 6px 0 18px; }}
article h2 {{ font-size: 22px; margin: 36px 0 12px; padding-bottom: 6px; border-bottom: 1px solid var(--border); }}
article h3 {{ font-size: 18px; margin: 26px 0 10px; }}
article h4 {{ font-size: 16px; margin: 20px 0 8px; color: var(--accent); }}
article p {{ margin: 10px 0; }}
article ul, article ol {{ margin: 10px 0 10px 24px; padding: 0; }}
article li {{ margin: 4px 0; }}
article blockquote {{
  margin: 14px 0; padding: 10px 16px; border-left: 4px solid var(--accent);
  background: var(--panel); color: var(--text); border-radius: 0 8px 8px 0;
}}
article hr {{ border: 0; border-top: 1px dashed var(--border); margin: 26px 0; }}
article img {{
  max-width: 100%; border-radius: 10px; border: 1px solid var(--border);
  background: #000; cursor: zoom-in; display: block;
}}
figure {{ margin: 18px 0; }}
figcaption {{ color: var(--muted); font-size: 13px; text-align: center; margin-top: 6px; }}
.table-wrap {{ overflow-x: auto; margin: 14px 0; }}
table {{ border-collapse: collapse; width: 100%; font-size: 14px; }}
th, td {{ border: 1px solid var(--border); padding: 8px 10px; text-align: left; vertical-align: top; }}
th {{ background: var(--panel); }}
tbody tr:nth-child(even) {{ background: var(--table-stripe); }}
.code-block {{ position: relative; margin: 14px 0; }}
.code-block pre {{
  background: var(--code-bg); border: 1px solid var(--border); border-radius: 10px;
  padding: 14px 16px; overflow-x: auto; font: 13px/1.6 Consolas, "Cascadia Mono", monospace;
}}
.code-block .copy-btn {{
  position: absolute; right: 10px; top: 8px; opacity: .5; font-size: 12px;
  background: var(--panel); color: var(--text); border: 1px solid var(--border);
  border-radius: 6px; padding: 3px 9px; cursor: pointer;
}}
.code-block:hover .copy-btn {{ opacity: 1; }}
article code {{ background: var(--code-bg); padding: 1px 6px; border-radius: 5px; font-family: Consolas, monospace; font-size: 13px; }}
.anchor {{ margin-left: 8px; color: var(--muted); opacity: 0; font-size: 15px; }}
h1:hover .anchor, h2:hover .anchor, h3:hover .anchor {{ opacity: 1; }}
.struct-map {{ width: 100%; height: auto; margin: 6px 0 24px; }}
.map-title {{ fill: var(--muted); font-size: 13px; }}
.map-box {{ fill: var(--panel); stroke: var(--border); }}
.map-text {{ fill: var(--text); font-size: 13px; }}
.map-arrow {{ stroke: var(--border); stroke-width: 2; }}
.doc-nav {{ display: flex; justify-content: space-between; gap: 12px; margin-top: 48px; }}
.doc-nav a {{
  flex: 1; padding: 12px 16px; border: 1px solid var(--border); border-radius: 10px;
  background: var(--panel); color: var(--text);
}}
.doc-nav a:hover {{ border-color: var(--accent); text-decoration: none; }}
.doc-nav small {{ display: block; color: var(--muted); font-size: 12px; }}
#results {{
  position: absolute; top: 54px; left: 50%; transform: translateX(-50%);
  width: min(680px, 90vw); background: var(--panel); border: 1px solid var(--border);
  border-radius: 12px; box-shadow: 0 18px 50px rgba(0,0,0,.45); display: none; z-index: 40;
}}
#results.show {{ display: block; }}
.result {{ display: block; padding: 10px 14px; border-bottom: 1px solid var(--border); color: var(--text); }}
.result:last-child {{ border-bottom: 0; }}
.result:hover {{ background: var(--bg-soft); text-decoration: none; }}
.result .r-title {{ font-weight: 600; }}
.result .r-snip {{ color: var(--muted); font-size: 12px; }}
#lightbox {{
  position: fixed; inset: 0; background: rgba(0,0,0,.88); display: none;
  align-items: center; justify-content: center; z-index: 100; cursor: zoom-out;
}}
#lightbox.show {{ display: flex; }}
#lightbox img {{ max-width: 96vw; max-height: 94vh; border-radius: 6px; }}
#toast {{
  position: fixed; bottom: 26px; left: 50%; transform: translateX(-50%);
  background: var(--panel); border: 1px solid var(--accent); padding: 8px 16px;
  border-radius: 20px; font-size: 13px; opacity: 0; transition: opacity .2s; z-index: 60;
}}
#toast.show {{ opacity: 1; }}
@media (max-width: 900px) {{
  aside {{ display: none; }}
  main {{ padding: 18px; }}
}}
</style>
</head>
<body>
<div id="progress"></div>
<header>
  <div class="brand">ErMao 引擎<small>学习文档 · GAMES104</small></div>
  <input id="search" type="search" placeholder="搜索章节 / 概念（按 / 聚焦）">
  <div class="header-actions">
    <button class="icon-btn" id="themeBtn" title="切换主题">🌗 主题</button>
    <button class="icon-btn" id="tocBtn" title="回到顶部">↑ 顶部</button>
  </div>
</header>
<div id="results"></div>
<div class="shell">
  <aside id="nav">{''.join(nav_parts)}</aside>
  <main id="main"><div id="content">{sections}</div></main>
</div>
<div id="lightbox"><img alt=""></div>
<div id="toast"></div>

<script>
const DOCS = {payload};
const content = document.getElementById('content');
const main = document.getElementById('main');
const nav = document.getElementById('nav');
const progress = document.getElementById('progress');
const search = document.getElementById('search');
const results = document.getElementById('results');
const toast = document.getElementById('toast');

function showToast(text) {{
  toast.textContent = text; toast.classList.add('show');
  clearTimeout(showToast._t);
  showToast._t = setTimeout(() => toast.classList.remove('show'), 1200);
}}

let currentId = null;
function showDoc(id, pushHash = true) {{
  const doc = DOCS.find(d => d.id === id) || DOCS[0];
  currentId = doc.id;
  document.querySelectorAll('.doc').forEach(s => s.classList.toggle('active', s.id === doc.id));
  document.querySelectorAll('.nav-item').forEach(a => a.classList.toggle('active', a.dataset.id === doc.id));
  if (pushHash && location.hash !== '#' + doc.id) history.replaceState(null, '', '#' + doc.id);
  main.scrollTop = 0;
  localStorage.setItem('ermao-doc', doc.id);
  // 上一章 / 下一章
  const idx = DOCS.findIndex(d => d.id === doc.id);
  const prev = DOCS[idx - 1], next = DOCS[idx + 1];
  let navHtml = '';
  if (prev) navHtml += `<a href="#${{prev.id}}"><small>← 上一章</small>${{prev.title}}</a>`;
  else navHtml += '<span></span>';
  if (next) navHtml += `<a href="#${{next.id}}" style="text-align:right"><small>下一章 →</small>${{next.title}}</a>`;
  const old = document.querySelector('.doc-nav');
  if (old) old.remove();
  const box = document.createElement('div');
  box.className = 'doc-nav';
  box.innerHTML = navHtml;
  document.querySelector(`#${{doc.id}}`).appendChild(box);
}}

document.addEventListener('click', (e) => {{
  const a = e.target.closest('a[href^="#"]');
  if (a) {{
    const id = a.getAttribute('href').slice(1);
    if (DOCS.some(d => d.id === id)) {{
      e.preventDefault(); showDoc(id); return;
    }}
  }}
  const img = e.target.closest('article img');
  if (img) {{
    const lb = document.getElementById('lightbox');
    lb.querySelector('img').src = img.src;
    lb.classList.add('show');
  }}
  if (e.target.id === 'lightbox') e.target.classList.remove('show');
}});

document.querySelectorAll('.copy-btn').forEach(btn => {{
  btn.addEventListener('click', async () => {{
    const code = btn.parentElement.querySelector('code');
    try {{ await navigator.clipboard.writeText(code.innerText); showToast('已复制'); }}
    catch {{ showToast('复制失败，请手动选择'); }}
  }});
}});

search.addEventListener('input', () => {{
  const q = search.value.trim().toLowerCase();
  if (!q) {{ results.classList.remove('show'); return; }}
  const hits = DOCS.filter(d => (d.title + ' ' + d.path + ' ' + d.text).toLowerCase().includes(q)).slice(0, 8);
  results.innerHTML = hits.map(d => {{
    const t = d.text.toLowerCase();
    const pos = t.indexOf(q);
    const snip = pos >= 0 ? d.text.slice(Math.max(0, pos - 40), pos + 80) : '';
    return `<a class="result" href="#${{d.id}}"><div class="r-title">${{d.title}}</div>
      <div class="r-snip">${{d.path}}${{snip ? ' · …' + snip + '…' : ''}}</div></a>`;
  }}).join('') || '<div class="result"><div class="r-snip">没有找到，试试其它关键词</div></div>';
  results.classList.add('show');
}});

search.addEventListener('keydown', (e) => {{
  if (e.key === 'Escape') {{ search.value = ''; results.classList.remove('show'); search.blur(); }}
  if (e.key === 'Enter') {{
    const first = results.querySelector('.result');
    if (first) {{ const id = first.getAttribute('href').slice(1); showDoc(id); results.classList.remove('show'); }}
  }}
}});

document.addEventListener('keydown', (e) => {{
  if (e.target === search) return;
  if (e.key === '/') {{ e.preventDefault(); search.focus(); }}
  const idx = DOCS.findIndex(d => d.id === currentId);
  if (e.key === 'ArrowRight' && idx + 1 < DOCS.length) showDoc(DOCS[idx + 1].id);
  if (e.key === 'ArrowLeft' && idx > 0) showDoc(DOCS[idx - 1].id);
}});

main.addEventListener('scroll', () => {{
  const h = main.scrollHeight - main.clientHeight;
  progress.style.width = (h > 0 ? (main.scrollTop / h) * 100 : 0) + '%';
}});

document.getElementById('themeBtn').addEventListener('click', () => {{
  const cur = document.documentElement.getAttribute('data-theme') === 'light' ? 'dark' : 'light';
  document.documentElement.setAttribute('data-theme', cur);
  localStorage.setItem('ermao-theme', cur);
}});
document.getElementById('tocBtn').addEventListener('click', () => main.scrollTo({{ top: 0, behavior: 'smooth' }}));

document.documentElement.setAttribute('data-theme', localStorage.getItem('ermao-theme') || 'dark');
showDoc(location.hash ? location.hash.slice(1) : (localStorage.getItem('ermao-doc') || DOCS[0].id), false);
</script>
</body>
</html>
"""

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    OUT_FILE.write_text(html_doc, encoding="utf-8")
    ROOT_COPY.write_text(html_doc, encoding="utf-8")
    size_mb = OUT_FILE.stat().st_size / 1024 / 1024
    print(f"生成 {OUT_FILE}（{size_mb:.1f} MB，{len(docs)} 篇）")
    print(f"复制到 {ROOT_COPY}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
