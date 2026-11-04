#!/usr/bin/env python3
"""Patch Emscripten glue for WeChat mini-game compatibility.

WeChat's adapter exposes a read-only HTMLDocument.title; Emscripten's
emscripten_set_window_title() (called by SDL_CreateWindow) assigns it and
throws before main() can render. Guard the assignment with try/catch.
"""
import re
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_webgl_glue.py <game.js>", file=sys.stderr)
        return 2
    path = sys.argv[1]
    text = open(path, encoding="utf-8").read()
    guarded = "emscripten_set_window_title=title=>{try{document.title=UTF8ToString(title);}catch(e){}};"
    if guarded in text:
        print(f"[web] glue already patched: {path}")
        return 0
    pattern = re.compile(
        r"emscripten_set_window_title\s*=\s*[^;]*?document\.title\s*=\s*UTF8ToString\(title\);"
    )
    patched, count = pattern.subn(guarded, text, count=1)
    if count == 0:
        print("[web] ERROR: emscripten_set_window_title pattern not found in " + path, file=sys.stderr)
        return 1
    open(path, "w", encoding="utf-8").write(patched)
    print(f"[web] patched WeChat-safe window title in {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
