# ErMao Engine（二毛引擎）v0.2

> Windows / C++17 / D3D11 / **Dear ImGui** 的 GAMES104 教学引擎。
> 分层干净、能一条命令跑起来、编辑器五脏俱全，关卡与玩法**代码优先**。

![编辑器](docs/screenshots/editor.png)

## 30 秒跑起来

```bat
build.bat
build\bin\ErMao.exe
```

首次构建会把 `shaders/`、`assets/`（含一个外部 OBJ）拷到 exe 旁边，exe 目录自包含。

## 操作

| 操作 | 说明 |
|---|---|
| 视口内 `右键拖动` | 编辑器相机环视 |
| 视口内 `WASD / QE / Shift` | 编辑器相机移动 |
| 视口内 `左键点击` | 拾取物体（射线 vs 世界 AABB） |
| `F5` 或工具栏"运行" | Play：WASD 移动、Shift 奔跑、空格跳跃、右键拖动转视角 |
| `Ctrl+S / Ctrl+O / Ctrl+N` | 保存 / 打开 / 新建场景 |
| `Ctrl+D / Delete` | 复制 / 删除选中 |
| `F2 / F4` | 线框 / 阴影 |
| `F11` | 隐藏 / 显示编辑器 |
| `F12` | 截图（Present 之前） |

Play 模式截图（第三人称关卡）：

![第三人称](docs/screenshots/game-thirdperson.png)

## v0.2 做了什么

### 1. 用 Dear ImGui 重写编辑器（`third_party/imgui`，docking 分支）
- Dock 布局 + 菜单栏 + 工具栏 + 状态栏，布局自动保存到 `imgui.ini`
- **视口**：场景渲染到离屏 RT，再 `ImGui::Image` 显示；拖拽面板不变形，拾取坐标正确
- **世界大纲**：层级树、搜索、创建/删除/复制、右键菜单
- **资源浏览器**：资产表（名称/类型/面数）、搜索、**导入 OBJ 模型 / 导入贴图**、双击放入场景
- **检视器**：反射驱动（一份 `Describe()` 同时服务 UI 与 JSON），加/删组件
- **控制台**：分级过滤、文本搜索、自动滚动、清空
- **统计**：FPS、CPU 提交、**GPU 时间戳**、draw call/实例/三角/剔除、物理统计、帧时间曲线、JobSystem 基准
- **设置**：曝光、阴影、线框、调试视图、太阳/环境光、重力、导航重建

### 2. 代码优先的第三人称关卡（`engine/src/game/`）
- 关卡就是 `BuildThirdPersonLevel(World&)` 一个函数：台阶 → 平台链 → 空中长廊 → 终点光环
- 第三人称控制器：相机相对移动、重力/跳跃/地面射线、球-AABB 推离、相机弹簧臂
- 5 颗**外部 OBJ** 水晶、风车关键帧动画、3 个 A* 巡逻 AI

### 3. 按审核报告修了一轮
P0 输入边沿、法线逆转置、统计翻倍/GPU 计时、批次排序、阴影 texel 对齐、UTF-8 路径、
物理世界坐标、PostPass 状态、设备降级顺序等。逐条记录见
[审核修复记录](docs/06-审核修复记录.md)。

## 文档

| 文档 | 内容 |
|---|---|
| [方案文档](docs/00-ErMao引擎方案文档.md) | 架构、选型、模块设计、里程碑、交付状态 |
| [GAMES104 课程映射](docs/01-GAMES104-课程映射.md) | 22 讲 × 代码 × 演示 × 完成度 |
| [分章讲解](docs/02-分章讲解.md) | 原理 → 实现 → 验证 → 差距 |
| [构建与运行](docs/03-构建与运行.md) | 环境、CLI、排错、代码导读 |
| [UI 与 ImGui 编辑器](docs/04-UI与ImGui编辑器.md) | 集成方式、面板、离屏视口、资源导入 |
| [第三人称关卡](docs/05-第三人称关卡.md) | 关卡构建、控制器数学、弹簧臂、冒烟测试 |
| [审核修复记录](docs/06-审核修复记录.md) | R1–R3 逐条回应 |

## 目录

```
engine/src/
  core/       数学、JSON、日志、计时、文件（UTF-8）、JobSystem
  platform/   Win32 窗口、输入（边沿检测）、文件对话框
  rhi/        D3D11 设备、缓冲、纹理、RT、运行时 Shader、截图
  renderer/   相机、网格、实例化、阴影、天空、后处理、调试线、离屏视口
  scene/      World / GameObject / Component / 反射 / 序列化 / 射线查询
  asset/      AssetManager（注册表 + 导入）、OBJ 加载
  physics/    简化刚体（世界坐标、球/盒碰撞、休眠）
  ai/         导航栅格、A*、感知与状态机
  game/       代码优先关卡 + 第三人称控制器
  editor/     ImGuiLayer、编辑器面板
  runtime/    Engine 入口与主循环
engine/shaders/   HLSL（运行时编译）
third_party/imgui/ Dear ImGui（docking）
```

## 自动化

```bat
ErMao.exe --test-serialize --frames 3     :: 场景 JSON 往返（字节级比较）
ErMao.exe --smoke --frames 200            :: 脚本化移动/跳跃，打印位置
ErMao.exe --import engine\assets\models\crystal.obj --frames 3
ErMao.exe --noeditor --play --frames 120 --screenshot game.png
```

## 已知缺口

骨骼蒙皮、粒子、行为树、网络、GI、GPU Driven、Gizmo/撤销栈、数学单测与 CI。
逐条状态见课程映射与审核修复记录，不假装已完成。
