#!/usr/bin/env bash
# 用微信官方 wx-transformer 把 Emscripten 的 H5 产物转成小游戏工程。
# 用法：WX_TRANSFORMER=/path/to/wx-transformer-linux ./web/tools/package-wechat.sh [build-dir] [output-dir]
set -euo pipefail

BUILD_DIR="${1:-build-web}"
OUT_DIR="${2:-${BUILD_DIR}/wechat-build}"
TRANSFORMER="${WX_TRANSFORMER:-wx-transformer-linux}"
APP_ID="${WECHAT_MINI_GAME_APPID:-wx0000000000000000}"

for file in game.js game.wasm game.data; do
  if [ ! -f "${BUILD_DIR}/${file}" ]; then
    echo "缺少 ${BUILD_DIR}/${file}；请先构建 Web 版本。" >&2
    exit 1
  fi
done

if ! command -v "${TRANSFORMER}" >/dev/null 2>&1 && [ ! -x "${TRANSFORMER}" ]; then
  echo "找不到 wx-transformer: ${TRANSFORMER}" >&2
  echo "下载：https://developers.weixin.qq.com/minigame/dev/guide/game-engine/common-adaptation/Design/QuickStart.html" >&2
  exit 1
fi

mkdir -p "${OUT_DIR}"
"${TRANSFORMER}" \
  --js-file "${BUILD_DIR}/game.js" \
  --wasm-file "${BUILD_DIR}/game.wasm" \
  --data-file "${BUILD_DIR}/game.data" \
  --output "${OUT_DIR}" \
  --app-id "${APP_ID}" \
  --app-name "ErMao 幸存者" \
  --engine-name "Custom" \
  --engine-version "0.1.0" \
  --webgl-version 1 \
  --device-orientation landscape \
  --load-from-subpackage

# wx-transformer 的 project.config.json 模板 appid 不会跟随 --app-id 更新
# （会保留模板里的 wx80127ef2ce867268），开发者工具预览/上传时会拿错 AppID，
# 报“登录用户不是该小程序的开发者”。这里显式写入实际 AppID 并校验。
python3 - "${OUT_DIR}/minigame/project.config.json" "${APP_ID}" <<'PYINNER'
import json
import sys
path, appid = sys.argv[1], sys.argv[2]
with open(path, encoding="utf-8") as f:
    data = json.load(f)
data["appid"] = appid
with open(path, "w", encoding="utf-8") as f:
    json.dump(data, f, ensure_ascii=False, indent=2)
    f.write("\n")
print(f"[wechat] project.config.json appid = {appid}")
PYINNER
actual_appid=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1],encoding="utf-8"))["appid"])' "${OUT_DIR}/minigame/project.config.json")
if [ "${actual_appid}" != "${APP_ID}" ]; then
  echo "错误：project.config.json 的 appid 是 ${actual_appid}，期望 ${APP_ID}" >&2
  exit 1
fi

# 关闭 WXGameKit 的“最佳实践检测”开发提示（“未使用 Wasm 代码分包 / 预下载能力”）。
# 这两个提示是 Unity 插件的通用建议：wasm/data 已经走 game.json 的 wasmcode/data-package
# 分包和 parallelPreloadSubpackages；WasmSplit 是 Unity 专用工具，自定义 Emscripten
# 无法套用。提示只在开发/体验版出现，关闭避免手机端反复弹窗。
if [ -f "${OUT_DIR}/minigame/wx-game-kit/config.json" ]; then
  python3 - "${OUT_DIR}/minigame/wx-game-kit/config.json" <<'PYCFG'
import json
import sys
path = sys.argv[1]
with open(path, encoding="utf-8") as f:
    data = json.load(f)
develop = data.setdefault("develop", {})
develop["hideTimeLogModal"] = True
monitor = develop.setdefault("monitor", {})
monitor["enable"] = False
monitor["showSuggestModal"] = False
with open(path, "w", encoding="utf-8") as f:
    json.dump(data, f, ensure_ascii=False, indent=2)
    f.write("\n")
print("[wechat] disabled WXGameKit best-practice monitor popups")
PYCFG
fi

# 手机竖屏：微信小游戏方向改为 portrait（否则真机会自动切横屏）。
if [ -f "${OUT_DIR}/minigame/game.json" ]; then
  python3 - "${OUT_DIR}/minigame/game.json" <<'PYORI'
import json
import sys
path = sys.argv[1]
with open(path, encoding="utf-8") as f:
    data = json.load(f)
data["deviceOrientation"] = "portrait"
with open(path, "w", encoding="utf-8") as f:
    json.dump(data, f, ensure_ascii=False, indent=2)
    f.write("\n")
print("[wechat] deviceOrientation = portrait")
PYORI
fi

# 转换工具会重写 framework.js；如果构建产物里的 title 补丁丢了，iOS 会在
# SDL 创建窗口时直接抛 “Cannot assign to read only property 'title'”，
# 这里在打包阶段硬性校验，避免再发一个启动即崩的 iPhone 包。
if ! grep -q 'emscripten_set_window_title=title=>{try{document.title' "${OUT_DIR}/minigame/framework.js"; then
  echo "错误：framework.js 缺少微信 read-only document.title 保护，iOS 会启动崩溃。" >&2
  exit 1
fi

if [ "${WECHAT_DEBUG_LOG:-0}" = "1" ]; then
  python3 "$(dirname "$0")/inject-wechat-log.py" "${OUT_DIR}/minigame"
  if ! head -1 "${OUT_DIR}/minigame/game.js" | grep -q "ermao-logger"; then
    echo "错误：实时日志没有作为首个模块注入 game.js。" >&2
    exit 1
  fi
  echo "诊断版：已注入首模块实时日志/vConsole/屏显错误（filter: ermao）"
fi

echo "小游戏工程已生成：${OUT_DIR}/minigame"
echo "下一步：微信开发者工具 -> 导入 ${OUT_DIR}/minigame（需要小游戏 AppID 预览/真机调试）"
