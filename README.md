# ErMao Engine（二毛引擎）v0.8.6

> Windows / C++17 / D3D11 / **Dear ImGui** 的 GAMES104 教学引擎。
> 分层干净、一条命令跑起来、编辑器完整；关卡与玩法**代码优先**，带可玩的 2D/3D 幸存者 Demo。

![刷怪 Demo](docs/screenshots/demo-wave.png)

## 30 秒跑起来

```bat
setup.bat                        :: 第一次：检查依赖 + 构建 + 冒烟
build.bat                        :: 之后：Release 构建
build\bin\ErMao.exe              :: 打开编辑器（默认刷怪竞技场）
```

- 编辑器里按 **F5**（或工具栏“运行”）→ 游戏菜单 → `开始游戏`；
- 直接看演示：`--demo`（竞技场）/ `--demo2d` / `--demo3d`；
- 打包：菜单 `文件 → 打包`。

环境要求、CLI 参数、CLion/VS 配置与排错见 **[构建与运行](docs/版本/构建与运行.md)**。

## 操作

| 场景 | 操作 |
|---|---|
| 视口 | 右键拖动 + WASD/QE 移动相机 · 左键拾取 · **W/E/R 位移/旋转/缩放 Gizmo** · F 聚焦 |
| 运行模式 | WASD 移动 · Shift 奔跑 · 空格跳跃 · 右键拖动转视角 · 左键射击 · Esc 暂停 |
| 编辑器 | Ctrl+S/O/N 保存/打开/新建 · Ctrl+D 复制 · Delete 删除 · F2 线框 · F4 阴影 · F11 隐藏 UI · F12 截图 |

## 现在能做什么（v0.8.6）

- **渲染**：D3D11 前向管线，Shadow → Sky → Opaque → Skinned → Particle → Debug → Post；
  PBR/阴影/后处理、GPU Driven 剔除（`--level cull`）、2D 精灵批处理；
- **场景**：GameObject/Component、反射驱动 Inspector、JSON 存读、Play-In-Editor 快照回滚；
- **编辑器**：ImGui Dock、世界大纲/检视器/资源浏览器/控制台/统计/设置、拾取、Gizmo、模型贴图导入；
- **玩法**：第三人称关卡、刷怪竞技场、2D/3D 幸存者（碰撞、血条、菜单、音效）；
- **系统**：骨骼蒙皮与动画混合、教学级物理、导航 A* + 行为树、XAudio2 音频；
- **工具**：`--script` AI 无头接口（建场景/改组件/导资产/截图/打包）、Windows 游戏包、Linux 源码包；
- **Vulkan**：阶段一三角形 + 截图，用来演示 RHI 抽象方向。

版本历史、每个版本做了什么、还缺什么：**[版本记录](docs/版本/版本记录.md)**（明细在 `CHANGELOG.md`）。

## 文档

全部文档只有三类入口，先看大纲：**[docs/README.md](docs/README.md)**。

| 入口 | 内容 |
|---|---|
| **[代码总纲](docs/00-代码总纲.md)** | `main` → 全部次级函数的调用堆栈，7 张时序图，函数索引与速查表 |
| **[版本文档](docs/版本/版本记录.md)** | [版本记录](docs/版本/版本记录.md)、[构建与运行](docs/版本/构建与运行.md)、[发布与工程](docs/版本/发布与工程.md)、[修复记录](docs/版本/修复记录.md) |
| **[教学文档](docs/教学/README.md)** | [教学大纲](docs/教学/README.md)、[引擎架构](docs/教学/引擎架构.md)、[课程映射](docs/教学/课程映射.md)、[分章讲解](docs/教学/分章讲解.md)、[学习指南](docs/教学/学习指南.md)、[编辑器与关卡](docs/教学/编辑器与关卡.md)、[AI 开发指南](docs/教学/AI开发指南.md) |
| **[GAMES104 讲义](docs/教学/讲义/README.md)** | 26 篇讲义（L00–L26）：数学依据 + 代码位置 + 可复现实验 + 自测题 |

**离线文档站**：根目录 `ErMao-学习文档.html`，按“总纲 / 版本文档 / 教学文档”分组，
带侧边栏、搜索、图片放大和每讲原理图；拷到任何地方双击即看。改了 Markdown 后重新生成：

```bat
python tools\build_docs_site.py
```

## 目录

```
engine/src/
  core/       数学、JSON、日志、计时、文件（UTF-8）、JobSystem
  platform/   Win32 窗口、输入边沿、音频、文件对话框
  rhi/        D3D11 设备、缓冲、纹理、RT、运行时 Shader、截图、Vulkan 实验后端
  renderer/   相机、网格、实例化、阴影、天空、后处理、调试线、离屏视口、缩略图
  scene/      World / GameObject / Component / 反射 / 序列化 / 射线查询
  asset/      AssetManager（注册表 + 导入）、OBJ 加载
  animation/  骨骼、关键帧采样、混合、蒙皮矩阵
  physics/    简化刚体（世界坐标、球/盒碰撞、休眠）
  ai/         导航栅格、A*、感知与状态机、行为树
  game/       代码关卡、第三人称控制器、WaveGame、幸存者、粒子
  editor/     ImGuiLayer、编辑器面板、Gizmo、PIE
  runtime/    Engine 主循环、AgentScript、Packager
third_party/imgui/  Dear ImGui（docking，仅保留编译必需文件）
```

## 自动化

```bat
ErMao.exe --test-serialize --frames 3      :: 场景 JSON 往返（字节级）
ErMao.exe --smoke --frames 200             :: 脚本化移动/跳跃
ErMao.exe --import engine\assets\models\crystal.obj --frames 3
ErMao.exe --demo --frames 420 --screenshot demo.png
ErMao.exe --package-game windows --frames 2
```

## 已知缺口

- **Linux 没有二进制**：只有 Win32 + D3D11，发布的是源码包；
- **Vulkan 只到阶段一**：三角形 + 截图，渲染 Pass 未迁；
- **网络、完整 GI 只有设计**（L18 / L21）；
- **编辑器没有撤销栈**；
- **测试以自检命令为主**：序列化/冒烟/导入/打包/文档引用，数学与 JSON 单元测试待补。

逐条状态与后续计划见 [版本记录](docs/版本/版本记录.md)。

## 许可与素材

- 代码：见根目录 `LICENSE`；
- 第三方素材（Kenney / OpenGameArt / 用户角色包）来源与许可见 [发布与工程 §8](docs/版本/发布与工程.md#8-素材来源与许可)；
- Dear ImGui：MIT（`third_party/imgui/LICENSE.txt`）。
