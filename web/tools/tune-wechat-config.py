#!/usr/bin/env python3
"""Tune wx-game-kit config for this generic (non-Unity) game.

The WXGameKit dev monitor assumes asset preloading + WasmSplit V2. This game
ships one data package and one wasm subpackage with parallelPreloadSubpackages,
so those suggestions are false positives. Keep subpackages/preload, silence the
dev-only suggestion modal.
"""
import json
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: tune-wechat-config.py <minigame-dir>", file=sys.stderr)
        return 2
    path = sys.argv[1].rstrip("/") + "/wx-game-kit/config.json"
    data = json.load(open(path, encoding="utf-8"))
    develop = data.setdefault("develop", {})
    develop["enableDebugLog"] = False
    monitor = develop.setdefault("monitor", {})
    monitor["enable"] = False
    monitor["showSuggestModal"] = False
    monitor["showResultAfterLaunch"] = False
    json.dump(data, open(path, "w", encoding="utf-8"), ensure_ascii=False, indent=2)
    print(f"[wechat] dev monitor suggestions disabled: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
