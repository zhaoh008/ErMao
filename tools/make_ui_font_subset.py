#!/usr/bin/env python3
"""Regenerate the Apache-2.0 WenQuanYi Micro Hei subset used by the 2D survivors UI.

The game renders Chinese through a small font subset (engine/assets/fonts/ErMaoUI.ttf).
Whenever UI text changes, run this script so glyphs are not dropped on Web/WeChat builds:

    python3 tools/make_ui_font_subset.py --font /path/to/wqy-microhei.ttc

The source font is the Debian package `fonts-wqy-microhei`
(https://deb.debian.org/debian/pool/main/f/fonts-wqy-microhei/), Apache-2.0.
The character set is the union of:
  * the kSurvivorsUIGlyphs literal in engine/src/editor/ImGuiLayer.cpp
  * every CJK/punctuation character in engine/src/game/Survivors*.cpp/.h
plus printable ASCII.
"""

import argparse
import os
import subprocess
import sys
import tempfile

try:
    from fontTools.ttLib import TTFont
except ImportError:
    sys.exit("需要 fonttools：pip install fonttools")

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
GLYPH_SOURCE = os.path.join(ROOT, "engine", "src", "editor", "ImGuiLayer.cpp")
GAME_SOURCES = [
    os.path.join(ROOT, "engine", "src", "game", "Survivors.cpp"),
    os.path.join(ROOT, "engine", "src", "game", "Survivors.h"),
    os.path.join(ROOT, "engine", "src", "game", "SurvivorsUI.cpp"),
    os.path.join(ROOT, "engine", "src", "game", "SurvivorsUI.h"),
    os.path.join(ROOT, "web", "src", "WebApp.cpp"),
]
OUT_FONT = os.path.join(ROOT, "engine", "assets", "fonts", "ErMaoUI.ttf")


def collect_chars():
    import re

    source = open(GLYPH_SOURCE, encoding="utf-8").read()
    match = re.search(r'const char\* kSurvivorsUIGlyphs =\s*((?:"[^"]*"\s*)+);', source)
    if not match:
        sys.exit("找不到 kSurvivorsUIGlyphs 字表")
    text = "".join(re.findall(r'"([^"]*)"', match.group(1)))
    for path in GAME_SOURCES:
        text += open(path, encoding="utf-8").read()
    chars = {
        ch
        for ch in text
        if 0x20 <= ord(ch) < 0x7F or "\u4e00" <= ch <= "\u9fff" or ch in "·×“”→、。！？（），：；"
    }
    return "".join(sorted(chars))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", required=True, help="WenQuanYi Micro Hei source (.ttf/.ttc)")
    parser.add_argument("--font-number", type=int, default=0, help="TTC face index (default 0)")
    parser.add_argument("--out", default=OUT_FONT, help="output subset path")
    args = parser.parse_args()

    glyphs = collect_chars()
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False, encoding="utf-8") as handle:
        handle.write(glyphs)
        text_path = handle.name
    try:
        subprocess.run(
            [
                sys.executable,
                "-m",
                "fontTools.subset",
                args.font,
                f"--font-number={args.font_number}",
                f"--text-file={text_path}",
                "--unicodes=U+0020-007E",
                "--layout-features=*",
                "--no-hinting",
                f"--output-file={args.out}",
            ],
            check=True,
        )
    finally:
        os.unlink(text_path)

    font = TTFont(args.out)
    cmap = set()
    for table in font["cmap"].tables:
        cmap.update(table.cmap.keys())
    missing = [ch for ch in glyphs if ord(ch) not in cmap]
    if missing:
        sys.exit(f"字体子集缺少 {len(missing)} 个字符: {''.join(missing)}")
    print(f"{args.out}: {len(glyphs)} 个字符，{os.path.getsize(args.out)} 字节")


if __name__ == "__main__":
    main()
