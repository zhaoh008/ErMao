# Web / 微信视觉对比

- `before-native-1600x900.png`：Windows 原生 `ErMao.exe`（PR 的 Windows CI 产物，代码基线为 `main@d4676e5` + 本 PR 的非视觉改动），1600×900，`--demo2d --frames 300`，游戏时间 5 秒。
- `after-webgl-1600x900.png`：本 PR 的 `ermao-web` CI 产物，1600×900，`?play=1&frames=300`（预推进 300 个固定步 = 5 秒游戏时间）。
- `after-webgl-mobile-844x390.png`：同一 Web 产物，844×390 手机横屏视口，`?play=1&frames=300&mobile=1`，显示触摸摇杆与右上角 HUD 暂停热区。

采集方式：Windows 原生截图由 `ErMao.exe` 的 `--screenshot` 生成；Web 截图由 headless Chrome（SwiftShader WebGL1）生成。
- `ios-after-webgl-mobile-844x390.png`：本 PR（iOS 启动兼容加固 + 32MB 初始堆）的同一 Web 产物、同一 844×390 视口与 `?play=1&frames=300&mobile=1` 场景；与 `after-webgl-mobile-844x390.png` 视觉一致，用于确认兼容 shim/内存调整没有渲染回归。
- `mobile-hud-before-portrait-402x874.png` / `mobile-hud-after-portrait-402x874.png`：手机 HUD 重排前后，402×874 竖屏，`?play=1&frames=300&mobile=1`，同一状态（游戏时间 5 秒）；Before 为 `main@9886c02`，After 为本分支。Before 中可见左下常驻摇杆与右上大地图，After 为顶栏+暂停键+紧凑雷达+5 槽底栏，无摇杆。
- `mobile-hud-before-landscape-844x390.png` / `mobile-hud-after-landscape-844x390.png`：同一场景横屏 844×390；Before 为旧 HUD + 摇杆，After 为顶栏/暂停/雷达并排 + 5 槽底栏。
- `ui-after-mobile-844x390.png`：手机 UI 适配后（字体子集补全、文字 1.3x、小地图右上竖版并缩放），844×390，`?play=1&frames=300&mobile=1`；Before 为 `after-webgl-mobile-844x390.png`。
- `ui-after-desktop-1600x900.png`：桌面 UI 适配后（小地图右上竖版、桌面独立尺寸），1600×900，`?play=1&frames=300`；Before 为 `after-webgl-1600x900.png`。
