# ErMao Engine（二毛引擎）v0.3

> Windows / C++17 / D3D11 / **Dear ImGui** 的 GAMES104 教学引擎。
> 分层干净、能一条命令跑起来、编辑器五脏俱全，关卡与玩法**代码优先**。
> v0.3 起自带一个能玩的**刷怪生存 Demo**（碰撞、3D 头顶血条、游戏菜单）。

![刷怪 Demo](docs/screenshots/demo-wave.png)

## 30 秒跑起来

```bat
build.bat
build\bin\ErMao.exe            :: 打开编辑器（默认加载刷怪竞技场）
```

- 编辑器里按 **F5**（或工具栏"运行"）→ 弹出游戏菜单 → `开始游戏`
- 直接看演示：`build\bin\ErMao.exe --demo`
- 打包：菜单 `文件 → 打包 → 打包 Windows`

## 操作

| 场景 | 操作 |
|---|---|
| 视口 | 右键拖动环视 · WASD/QE 移动 · 左键拾取 · **F 聚焦选中** · 拖动三轴 Gizmo 平移 |
| 运行模式 | WASD 移动 · Shift 奔跑 · 空格跳跃 · 右键拖动转视角 · **左键射击** · Esc 暂停 |
| 编辑器 | Ctrl+S/O/N 保存/打开/新建 · Ctrl+D 复制 · Delete 删除 · F2 线框 · F4 阴影 · F11 隐藏 UI · F12 截图 |

## v0.3 做了什么

### 1. 刷怪生存 Demo（代码优先）
- `game/WaveArena.cpp`：52×52 竞技场、围墙、立柱、矮墙、木箱、场地灯
- `game/WaveGame.cpp`：`Health / Combat / Projectile / Enemy` 四个玩法组件 + `WaveGame` 波次导演
- 玩法：左键射击 → 球-球命中 → 敌人掉血；敌人近身攻击；清波进下一波；死亡结算
- 碰撞：角色/敌人 vs 静态几何（球-AABB，带竖直区间）、投射物 vs 敌人（球-球）

### 2. 3D UI 与游戏菜单
- **头顶血条**：世界坐标投影到屏幕，ImGui 前景层绘制；带遮挡检测、距离缩放、受击白闪、玩家血量数字
- **菜单**：主菜单 / 暂停（Esc）/ 失败结算，全部 ImGui
- **HUD**：生命条、波次、得分、准星、操作提示

### 3. 编辑器补齐
- **资源浏览器**（Unity 风格）：左侧分类、右侧缩略图网格；模型缩略图是**真的把网格渲染到 128² RT**；双击放入场景/赋贴图/加载场景
- **位移 Gizmo**：X/Y/Z 三轴拖拽（屏幕投影 + 世界/像素换算）
- **F 聚焦**：按包围盒把编辑器相机对准选中物体

### 4. 打包
- `文件 → 打包 → 打包 Windows`：生成 `dist/ErMao-Windows/`（exe + shaders + assets + run.bat），**已验证可独立运行**
- `文件 → 打包 → 打包 Linux（源码包）`：引擎只有 D3D11 后端，导出的是源码 + 构建脚本 + `README-LINUX.md`（写明补 Vulkan/OpenGL 的步骤），不假装有 Linux 二进制

### 5. 修复
- 第三人称鼠标 **X 轴反向**（yaw 符号），并加"反转 Y 轴"选项；按住空格不再连跳
- 审核 R4–R8 的收尾项：消息钩子只观察不消费、`WantCaptureKeyboard`、`imgui.ini` 落在 exe 旁、字体路径取自 `GetWindowsDirectoryW`、ImGui 独立 target、GPU 查询状态机、`Editor::Shutdown`、PIE 逻辑去重、PIE 后玩家指针悬垂、平台边缘侧滑等（逐条见 `docs/07`）

## 截图

| 编辑器（资源浏览器 + Gizmo） | 游戏菜单 |
|---|---|
| ![编辑器](docs/screenshots/editor.png) | ![菜单](docs/screenshots/demo-menu.png) |

## 文档

| 文档 | 内容 |
|---|---|
| [方案文档](docs/00-ErMao引擎方案文档.md) | 架构、选型、模块设计、里程碑 |
| [GAMES104 课程映射](docs/01-GAMES104-课程映射.md) | 22 讲 × 代码 × 演示 × 完成度 |
| [分章讲解](docs/02-分章讲解.md) | 原理 → 实现 → 验证 → 差距 |
| [构建与运行](docs/03-构建与运行.md) | 环境、CLI、排错、代码导读 |
| [UI 与 ImGui 编辑器](docs/04-UI与ImGui编辑器.md) | 集成、面板、离屏视口、资源导入 |
| [第三人称关卡](docs/05-第三人称关卡.md) | 关卡构建、控制器数学、弹簧臂 |
| [审核修复记录](docs/06-审核修复记录.md) | R1–R3 逐条回应 |
| [v0.3 新功能](docs/07-v0.3-新功能.md) | Demo、血条、资源浏览器、Gizmo、打包、R4–R8 修复 |
| **[GAMES104 讲义](docs/lectures/README.md)** | 22 讲逐讲：数学依据 + 代码位置 + 可复现实验 + 自测题 |

## 目录

```
engine/src/
  core/       数学、JSON、日志、计时、文件（UTF-8）、JobSystem
  platform/   Win32 窗口、输入（边沿检测）、文件对话框
  rhi/        D3D11 设备、缓冲、纹理、RT、运行时 Shader、截图
  renderer/   相机、网格、实例化、阴影、天空、后处理、调试线、离屏视口、缩略图
  scene/      World / GameObject / Component / 反射 / 序列化 / 射线查询
  asset/      AssetManager（注册表 + 导入）、OBJ 加载
  physics/    简化刚体（世界坐标、球/盒碰撞、休眠）
  ai/         导航栅格、A*、感知与状态机
  game/       第三人称关卡、刷怪竞技场、控制器、WaveGame
  editor/     ImGuiLayer、编辑器面板、Gizmo
  runtime/    Engine 主循环、Packager（Windows/Linux 包）
third_party/imgui/ Dear ImGui（docking，仅保留编译必需文件）
```

## 自动化

```bat
ErMao.exe --test-serialize --frames 3      :: 场景 JSON 往返（字节级）
ErMao.exe --smoke --frames 200             :: 脚本化移动/跳跃
ErMao.exe --import engine\assets\models\crystal.obj --frames 3
ErMao.exe --demo --frames 420 --screenshot demo.png
ErMao.exe --package windows --frames 2
```

## 已知缺口

骨骼蒙皮、粒子、行为树、网络、GI、GPU Driven；编辑器没有旋转/缩放 Gizmo、撤销栈；
Linux 运行时需要先补 Vulkan/OpenGL 后端。逐条状态见课程映射与审核修复记录。
