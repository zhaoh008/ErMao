#!/usr/bin/env python3
"""Patch Emscripten glue for WeChat mini-game compatibility.

WeChat's adapter exposes a read-only HTMLDocument.title; Emscripten's
emscripten_set_window_title() (called by SDL_CreateWindow) assigns it and
throws before main() can render. Guard the assignment with try/catch.

WeChat 8.0.80/iOS 的 WasmPatch 可能在运行时改动 wasm 函数表，却不更新
Emscripten 的 wasmTableMirror；getWasmTableEntry 的断言会直接 abort
("JavaScript-side Wasm function table mirror is out of date!")。这里改为
直接读 wasmTable.get()，让真实表成为唯一依据。
"""
import re
import sys


TABLE_OLD = (
    "var getWasmTableEntry=funcPtr=>{var func=wasmTableMirror[funcPtr];"
    "if(!func){if(funcPtr>=wasmTableMirror.length)wasmTableMirror.length=funcPtr+1;"
    "wasmTableMirror[funcPtr]=func=wasmTable.get(funcPtr)}"
    "assert(wasmTable.get(funcPtr)==func,\"JavaScript-side Wasm function table mirror is out of date!\");"
    "return func};"
)
TABLE_NEW = "var getWasmTableEntry=funcPtr=>wasmTable.get(funcPtr);"


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_webgl_glue.py <game.js>", file=sys.stderr)
        return 2
    path = sys.argv[1]
    text = open(path, encoding="utf-8").read()
    changed = False

    # WeChat's WasmPatch/表镜像会和 JS 侧缓存不一致；Emscripten 的断言会直接
    # abort（"JavaScript-side Wasm function table mirror is out of date!"）。
    # 直接读真实 wasm 表，绕开镜像缓存与断言。
    if TABLE_NEW in text:
        print(f"[web] wasm table glue already patched: {path}")
    elif TABLE_OLD in text:
        text = text.replace(TABLE_OLD, TABLE_NEW, 1)
        changed = True
        print(f"[web] patched WeChat-safe wasm table entry in {path}")
    else:
        print("[web] WARNING: getWasmTableEntry pattern not found in " + path, file=sys.stderr)

    guarded = "emscripten_set_window_title=title=>{try{document.title=UTF8ToString(title);}catch(e){}};"
    if guarded in text:
        print(f"[web] window title already guarded: {path}")
    else:
        pattern = re.compile(
            r"emscripten_set_window_title\s*=\s*[^;]*?document\.title\s*=\s*UTF8ToString\(title\);"
        )
        text, count = pattern.subn(guarded, text, count=1)
        if count == 0:
            print("[web] ERROR: emscripten_set_window_title pattern not found in " + path, file=sys.stderr)
            return 1
        changed = True
        print(f"[web] patched WeChat-safe window title in {path}")

    if changed:
        open(path, "w", encoding="utf-8").write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
