# 变更日志（Changelog）

遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/) 与语义化版本。
版本标签见 `git tag`；每个版本对应一个可运行的状态。

## [Unreleased]

## [0.8.4] - 2026-11-02

### Added
- `docs/14-从入口到功能的调用栈.md`：从 `main` 到渲染/玩法/资源/序列化/编辑器/AI/音频/Vulkan/打包的完整调用栈，含速查表与追栈方法
- HTML 文档站新增「调用栈地图」


## [0.8.3] - 2026-11-02

### Added
- `CMakePresets.json`：`vs2022` / `ninja-msvc-debug` / `ninja-msvc-release` 预设
- `docs/13-CLion配置.md`：工具链、Generator、Run Configuration、报错对照

### Fixed
- CLion 用 MinGW 或未配置工具链时，CMake 报 `No CMAKE_CXX_COMPILER` / 一堆编译错误：
  现在直接给出可操作的修复步骤
- Ninja 单配置生成器也把产物放到 `build/bin`；`/MP` 仅在 VS 生成器下启用


### Added
- 菜单栏「关卡」：2D 幸存者 / 3D 幸存者 / 第三人称 / 竞技场 / 蒙皮 / GPU 剔除
- 资源管理器「场景」新增内置关卡（`关卡 · 2D 幸存者` 等），单击即切换
- 场景 JSON 记录 `levelKind`，读档后自动恢复 2D/3D 模式

### Changed
- 2D 地面从 1680 个地砖对象改为「一张重复贴图 + 1 个精灵」，世界大纲只剩 `地面`/`玩家`

### Fixed
- 内置关卡不在资源列表、无法从编辑器选择
- 保存的 2D/3D 场景重新打开后按 3D 模式渲染（黑屏/空场景）


## [0.8.1] - 2026-11-01

### Fixed
- 无 Vulkan SDK 的机器编译失败（ulkan 头文件被无条件包含）：ERMAO_HAS_VULKAN 宏隔离 + 空实现
- setup.ps1 把仓库根目录算成上一级

### Added
- setup.bat / setup.ps1 一键依赖检查/安装、构建、冒烟
- docs/12-其他电脑运行.md（含干净 clone 验证步骤）

## [0.8.0] - 2026-11-01

### Added
- **AI 优先接口**：`--script commands.json --json-out report.json`，无头完成建场景 / 加组件 / 改字段 /
  导资产 / 存盘 / 截图 / 打包；`--validate` 输出结构化校验
- 博客系列规划：10 篇 / 41 章 + 审稿清单（`docs/blog/`）
- `docs/11-Git工作流.md`、`.github/workflows/build.yml`、`.githooks/pre-commit`

### Changed
- README 增加 Git 工作流与博客规划入口

## [0.7.0] - 2026-11-01

### Added
- 音频子系统（XAudio2 + WAV/PCM），2D/3D 幸存者接入音效与音量菜单、`--audio-test`
- 每讲「数学依据与参考资料」+ 参考资料总表（36 条）
- Vulkan 阶段一：instance / device / swapchain / pipeline / SPIR-V 三角形 / 截图，`--vulkan-test`

## [0.6.0] - 2026-10-31

### Added
- 3D 幸存者（XZ 移动、跟随相机、外部 OBJ 模型、粒子特效）
- `AssetManager::GetMeshAsset` 懒加载 OBJ；材质路径识别
- 粒子支持贴图 + 加法混合；2D/3D 游戏打包（Windows exe + Linux 源码）

## [0.5.0] - 2026-10-31

### Added
- 2D 渲染：`Camera2D`、`Sprite.hlsl`、精灵批处理、序列帧
- 「二毛幸存者」2D 像素游戏；`game.cfg` 与游戏包发布

## [0.4.0] - 2026-10-31

### Added
- GAMES104 讲义 L00–L24、术语表、难度标签、每讲练习与数值演算
- 离线 HTML 学习站（单文件、侧边栏、搜索、图片放大）

## [0.3.0] - 2026-10-31

### Added
- 刷怪 Demo：碰撞、3D 头顶血条、游戏菜单；资源浏览器缩略图
- 位移 Gizmo + F 聚焦；Windows/Linux 打包；序列化字节一致

## [0.2.0] - 2026-10-30

### Added
- Dear ImGui 编辑器（Dock、世界大纲、检视器、资源、控制台、统计）
- 第三人称关卡与控制器；审核修复项

## [0.1.0] - 2026-10-30

### Added
- 初始基线：D3D11 渲染、场景/组件、JSON 序列化、主循环
  （v0.1 的历史未单独入库，代码状态从 v0.2 起可追溯）
