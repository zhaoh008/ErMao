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

if [ "${WECHAT_DEBUG_LOG:-0}" = "1" ]; then
  python3 "$(dirname "$0")/inject-wechat-log.py" "${OUT_DIR}/minigame"
fi

echo "小游戏工程已生成：${OUT_DIR}/minigame"
echo "下一步：微信开发者工具 -> 导入 ${OUT_DIR}/minigame（需要小游戏 AppID 预览/真机调试）"
