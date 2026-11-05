# ErMao Web / 微信小游戏后端

这是 ErMao 引擎的第一阶段**跨平台运行时**：同一份 C++ 玩法（`engine/src/game/Survivors.cpp`、
`SurvivorsUI.cpp`、`scene/`、`asset/`）通过 Emscripten 编译成 WebAssembly，
图形走 OpenGL ES 2.0 / **WebGL 1.0**（优先兼容微信小游戏），平台层由 SDL2（浏览器）
或微信小游戏转换工具接管。

玩法代码没有第二份实现——这里是渲染/平台后端，不是重写过的 Web 游戏。

## 构建

需要 Emscripten **3.1.51**（微信官方自研引擎导出文档推荐的版本）和 CMake：

```bash
source ~/emsdk/emsdk_env.sh

# 可选但推荐：固定 SDL2 端口源码，避免 Emscripten 的 GitHub archive 端口下载失败
curl -fsSL https://www.libsdl.org/release/SDL2-2.24.2.tar.gz -o /tmp/SDL2.tar.gz
tar -xzf /tmp/SDL2.tar.gz -C /tmp
export EMCC_LOCAL_PORTS="sdl2=/tmp/SDL2-2.24.2"

emcmake cmake -S web -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web -j
```

产物在 `build-web/`：

```
game.js     JavaScript 胶水代码（微信转换工具的输入）
game.wasm   WebAssembly 代码
game.data   预加载的 engine/assets（贴图、音频、字体、场景）
index.html  本地浏览器测试页
```

本地预览：

```bash
python3 -m http.server 8000 -d build-web
# 浏览器打开 http://localhost:8000/index.html
# 自动开局并预推进 300 个固定步（5 秒游戏时间），首帧即截图状态：
# http://localhost:8000/index.html?play=1&frames=300
# 手机视口（竖屏或 844×390 横屏会自动切换手机 HUD；触控由浏览器/微信提供）：
# http://localhost:8000/index.html?play=1&frames=300&mobile=1
# UI/特效走查页（与桌面 --ui-preview 共用状态构造）：
# http://localhost:8000/index.html?ui-preview=prepare&frames=2
# http://localhost:8000/index.html?ui-preview=newskills&frames=240
# http://localhost:8000/index.html?ui-preview=gacha&frames=120&mobile=1
```

## 微信小游戏

先下载微信官方 `wx-transformer`（[小游戏转换工具](https://developers.weixin.qq.com/minigame/dev/guide/game-engine/common-adaptation/Design/TransformTool.html)），
然后：

```bash
WX_TRANSFORMER=/path/to/wx-transformer-linux \
WECHAT_MINI_GAME_APPID=wx你的AppID \
./web/tools/package-wechat.sh build-web build-web/wechat-build
```

转换工具会生成 `wechat-build/minigame/`，可以用微信开发者工具打开、预览、真机调试。
没有 AppID 时工具仍可生成项目结构，但预览/上传需要开发者工具登录一个有
「小游戏」类目的 AppID；本仓库不包含任何 AppID 或凭据。

包体约束（微信官方）：代码包总大小 ≤30MB，主包 ≤4MB，`game.js` ≤2MB。
当前 2D 资源约 8MB（主包约 1.6MB，data-package 约 6.1MB），转换工具支持 CDN/分包；主包超限时按转换工具文档调整
`--cdn-url` / `--load-from-subpackage`。

## iOS 与安卓的差异 / 排错

同一份 wasm 包在安卓能跑、iPhone 报“程序遇到错误”，通常不是玩法代码的差异，而是
两端小游戏运行时不同：

- **JS 引擎**：安卓是 V8（JIT），iOS 普通小游戏模式是 JavaScriptCore（无 JIT）；
  打开 iOS 高性能模式后才是 WKWebView + JIT。本 AppID 未开通高性能模式
  （插件日志 `canUseHighPerformance: false`），所以 iOS 走的是无 JIT 的普通模式。
- **DOM/Canvas 适配层**：Emscripten/SDL2 启动窗口时依赖 `document.title`、
  `devicePixelRatio`、`canvas.getBoundingClientRect` 等浏览器 API；微信 iOS 与
  安卓提供的 shim 并不完全一致，缺失时 SDL 会抛异常，微信只显示“程序遇到错误”，
  没有可读堆栈。`document.title` 只读的问题已在 `patch_webgl_glue.py` +
  `WebApp::Init` 中防护；其余最小接口在 `WebApp::Init` 的
  `InstallWeChatCompatShims()` 中补齐（浏览器里是 no-op）。
- **WebAssembly**：iOS/安卓都走微信的 `WXWebAssembly` 路径；这里用 WebGL1、
  无 SIMD/多线程，wasm 只用到 MVP + sign-ext，iOS 15+ 均支持。初始堆已从 128MB
  降到 32MB、最大 256MB，避免 iOS 运行时对声明 2GB 上限的额外压力。
- **iOS GL 状态查询兼容**：微信 iOS 的 WXWebGL 对 `glGetIntegerv` 的多个 pname
  （`GL_TEXTURE_BINDING_2D` 32873、`GL_CURRENT_PROGRAM` 35725 等）返回 undefined，
  每帧打印 `GL_INVALID_ENUM in glGet0v`，既刷屏又明显卡顿。ImGui 的 OpenGL3 后端
  默认每帧保存/恢复大量 GL 状态；Emscripten 构建下现在跳过整段保存/恢复（渲染
  状态由 WebRenderer / ImGui 自己每帧显式设置），初始化时的
  `GL_ARRAY_BUFFER_BINDING` 查询也跳过，清屏前关闭上一帧 UI 留下的 scissor，
  纹理上传前清空旧 GL 错误。桌面 GL 路径保持原行为。

- **调试**：微信的“真机调试”只支持安卓；iOS 无 vConsole 时用
  `WECHAT_DEBUG_LOG=1 ./web/tools/package-wechat.sh ...` 打包，会在 `game.js`
  最前面注入 `ermao-logger.js`：实时日志（运维中心筛 `ermao`）、`wx.setEnableDebug`
  打开 vConsole、首个未捕获错误直接 `wx.showModal` 显示在手机上，并记录
  SDL/GL/字体/资源启动阶段与 DOM/Canvas 能力快照。打包脚本还会校验
  `framework.js` 的 `document.title` 保护，防止再发启动即崩的 iPhone 包。

### 预览二维码固定位置

最新微信小游戏预览码统一写到 **`D:\GameEngine\dist\web-mobile-export\wechat-preview.png`**
（每次构建覆盖更新），不要再按版本/日期/修复名另起文件名；导入/扫码只认这一个路径。
`wechat-preview-info.json` 同步更新。

- 微信 8.0.80/iOS 的 WasmPatch 会在运行时改动 wasm 函数表，Emscripten 的
  `wasmTableMirror` 可能与真实表不一致，`getWasmTableEntry` 断言直接 abort
  （`JavaScript-side Wasm function table mirror is out of date!`）。构建后用
  `patch_webgl_glue.py` 把该函数改为直接读 `wasmTable.get()`，不再依赖镜像缓存。

- 手机竖屏：`game.json` 的 `deviceOrientation` 改为 `portrait`；`MakeCanvas` 在竖屏视口使用 1600 设计高度（宽度约 736），字号 1.3x。
- 手机战斗 HUD 按手机 HUD 原型（mobile-hud）重排：等级/计时/击杀与生命/经验合并为一块顶栏，44px 暂停键紧随其后，雷达缩小到右上角（竖屏在顶栏下方），技能槽固定为底部 5 格；普通桌面 HUD 不变。
- 手机 HUD 从微信 `safeArea` 与菜单胶囊位置换算内边距，竖屏再额外保留顶部空间，避免被刘海/状态栏/胶囊遮挡。

## 当前范围

- ✅ 2D 幸存者：菜单、出战准备、HUD、暂停、设置、结算、技能图鉴
- ✅ 2D 精灵批处理、正交相机、程序化贴图、序列帧
- ✅ ImGui 中文 UI（内置 Apache-2.0 的 WenQuanYi Micro Hei 子集字体）
- ✅ 手机触控：隐藏摇杆，屏幕任意位置按下拖动即可移动；顶栏暂停键可触摸，暂停后的按钮仍可触摸；画布随窗口/DPR 调整
- ✅ 浏览器 H5 与微信小游戏共用同一份 wasm 产物
- ⏳ 音频：接口已保留，当前静音（下一步接 SDL2 音频 / Web Audio）
- ⏳ 3D/Vulkan/编辑器：未纳入本阶段
- ⏳ 微信真机验证：需要 AppID + 微信开发者工具

## 设计说明

- `ERMAO_WEB` 宏让 `rhi/Texture.h` 与 `asset/AssetManager.h` 在 Web 构建下只暴露
  与后端无关的纹理接口；桌面 D3D11 路径保持不变。
- Web 构建排除 `runtime/Engine.cpp`、`editor/`、D3D11 `rhi/*.cpp`、
  `platform/Window|Input|Audio` 的原生实现，替换为 `web/src/` 下的 SDL2/GLES2/文件系统/时钟实现。
- `World`/`Scene`/`Game` 层不感知 D3D11 或 WebGL；`renderer/RenderScene.h` 的
  `SpriteDraw` 是两端共用的渲染输入。

## 已知限制

- 音频尚未实现，玩法会调用静音的 `AudioSystem` 占位实现。
- 只编译了 2D 路径；3D 模型、阴影、后处理、编辑器不在本阶段。
- 字体是 UI 文本子集；新增中文文本需要重新生成 `engine/assets/fonts/ErMaoUI.ttf`
  （见 `engine/assets/fonts/LICENSE.txt`）。
- 微信 iOS 的 WebGL1 兼容性最好；不要在本阶段打开 WebGL2/实例化。
