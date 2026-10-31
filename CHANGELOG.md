# 变更日志（Changelog）

遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/) 与语义化版本。
版本标签见 `git tag`；每个版本对应一个可运行的状态。

## [Unreleased]

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
