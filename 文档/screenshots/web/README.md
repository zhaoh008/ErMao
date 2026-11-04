# Web / 微信视觉对比

- `before-native-1600x900.png`：Windows 原生 `ErMao.exe`（PR 的 Windows CI 产物，代码基线为 `main@d4676e5` + 本 PR 的非视觉改动），1600×900，`--demo2d --frames 300`，游戏时间 5 秒。
- `after-webgl-1600x900.png`：本 PR 的 `ermao-web` CI 产物，1600×900，`?play=1&frames=300`（预推进 300 个固定步 = 5 秒游戏时间）。
- `after-webgl-mobile-844x390.png`：同一 Web 产物，844×390 手机横屏视口，`?play=1&frames=300&mobile=1`，显示触摸摇杆与右上角 HUD 暂停热区。

采集方式：Windows 原生截图由 `ErMao.exe` 的 `--screenshot` 生成；Web 截图由 headless Chrome（SwiftShader WebGL1）生成。
