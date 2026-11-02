# 变更日志（Changelog）

遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/) 与语义化版本。
版本标签见 `git tag`；每个版本对应一个可运行的状态。

## [Unreleased]

### Added
- 编辑器视角切换：**透视图 / 正视图 / 俯视图**（工具栏按钮、`显示 → 视角`、V 键循环）；正/俯视为正交相机，右键拖动平移、滚轮缩放
- 设置面板新增 **镜头速度**（1–40 m/s，Shift 加速 4 倍），不再写死 8/22
- 2D 关卡编辑使用正交正视图，编辑器相机、gizmo 与拾取共用同一投影
- `run.bat dev` / `run-dev.bat`：开发版启动（Debug + 控制台日志）；默认 `run.bat` 为发布版
- `--dev` / `--log` / `--nolog`：日志按需输出（控制台 / 仅文件 / 关闭）
- 2D / 3D 幸存者：敌人头顶 3D 血条（世界坐标投影、距离缩放、受击白闪）
- 2D / 3D 幸存者：命中漂浮伤害数字（2D 屏幕投影 / 3D 透视投影并按距离缩放）
- 击杀数驱动攻速：每击杀 +3%（递减叠加），HUD 显示攻速加成与当前攻击间隔

### Changed
- 关卡精简为两个 Demo：**2D 幸存者 / 3D 幸存者**；默认启动 2D，关卡菜单与资源浏览器只保留这两项
- 点击“运行 / F5”直接开局（不再先停在幸存者主菜单）；无编辑器的游戏包 `play=1` 同样直接开局
- 编辑器相机移动速度改由 `Camera::moveSpeed` 驱动，设置面板实时生效
- `ErMao.exe` 改为 GUI 子系统：发布版启动不再额外弹 CMD 控制台
- 发布版默认不写 `ErMao.log`；日志文件固定放在 exe 旁边，自动化参数仍会自动写日志
- 发布版启动失败时弹窗提示改用 `run.bat dev` / `--log` 排错（自动化任务不弹窗）
- 打包产物新增 `run-dev.bat`，`run.bat` 保持发布版行为
- 静态库按 Release/Debug 分目录，`run.bat` 切换版本不再触发 LNK4098 混链警告
- `文档/教学文档/引擎架构.md` §5.3 补全 RHI 层说明（概念、`rhi/` 实现走读、Vulkan 现状与真 RHI 差距）；术语表同步
- 教学讲义全面重写：26 篇统一按“现象 → 最简实现 → 具体失败 → 正式方案 → 源码 → 可验算例子 → 进阶数学”组织，
  面向会 C++、有 UE 经验但未实现过引擎模块的读者；同步 README 的读者假设/学习路线、术语表与参考资料
- 修正讲义中的技术问题：L01 累加器余数与触顶丢弃时间的账本、L04 矩阵复合顺序与投影数值例子、
  L08 模型空间/世界空间与“GPU 蒙皮”表述、L17 行为树记忆语义与错配的参考资料、L18 预测校正与 rollback 的混淆

### Fixed

- 编辑器视口渲染、gizmo/拾取与 3D 血条/伤害数字统一使用视口宽高比，修复默认停靠布局（非 16:9）下的投影偏移
- 选中对象后切换关卡/新建或加载场景会留下悬垂的编辑器选中项（use-after-free）；现在替换 World 前清空选中，并在帧首先按 id 回查再解引用
- 2D 精灵视口拾取与 F 聚焦：`WorldQuery` 在没有 `MeshRenderer` 时按 `SpriteRendererComponent` 的尺寸/偏移/旋转构造薄 AABB；同一平面命中按精灵 `layer` 优先，地面不再抢选玩家/敌人
- OBJ 导入的多边形面（四边形/五边形等）按扇形三角化，不再只渲染每个面的第一个三角形
- 修复非 ASCII（中文等）路径下纹理加载、截图保存与 WAV 音频加载失败：进入 WIC / 文件读取前统一使用
  UTF-8→UTF-16 转换（`FileSystem::Utf8ToWide` / `ReadBinaryFile`）
- Play/PIE 删除动画目标或第三人称主相机后，`Animator` / `ThirdPersonController` 仍解引用缓存裸指针导致 use-after-free；现在按对象 id 向 `World` 校验，失效后安全重解析
- 程序化圆柱（`primitives::Cylinder`）上下端盖三角形绕序与侧面相反，主不透明通道背面剔除后顶盖不可见（第三人称油桶呈开口管状）
- 新建场景未清除当前场景路径，Ctrl+S 会用空场景覆盖旧文件；从资源浏览器打开普通场景后路径未更新，保存会写回上一个文件
- 无头接口不再把失败当成功：`package.game` 失败时返回 `ok:false` 与 `error`；`--script` / `--validate` / `--package` / `--package-game` 失败时进程以非 0 退出码结束，CI 可据此失败
- 修复 `--script --json-out` 的逐条结果被覆盖：报告只含 `script` 字段，现由 `RunAgentScript` 写入完整的 `ok` / `total` / `succeeded` / `results`，逐条失败原因可供 AI/CI 读取
- 旋转 gizmo 改为绕物体**局部 X/Y/Z** 旋转；只命中朝向相机的前半环，并按环的屏幕切线累计角度，不再出现“斜拖一下同时转两轴 / 点错环”的问题
- 正交（正视图/俯视图/2D）拾取射线从近平面像素位置出发，不再全部从画面中心发射
- 编辑模式下 2D 精灵把整张序列图当一帧压扁导致玩家“消失”（第一帧 UV 未应用）
- 2D 精灵动画的帧裁剪 `frameMin`/`frameMax` 与 `columnsOverride` 未参与场景存档，保存后重新打开会用默认裁剪重写 UV、整条序列图被压进一帧；三个字段现已随场景保存并恢复
- 2D 精灵旋转（`SpriteRenderer.rotation` 与 Transform 的 Z 旋转）之前被渲染器忽略，现在生效
- Play 后玩法输入要求鼠标停在视口上；现在点击“运行”后可直接用 WASD

- 3D 幸存者击杀数一直为 0（敌人死亡未计数），结算与击杀攻速加成都受影响
- 重开一局没有恢复攻击力/攻速/最大生命/经验上限等升级属性
- 编辑器内 2D 幸存者 HUD 提前 `EndFrame()`，导致同帧后续 UI 绘制时机错误
- `World::Update/FixedUpdate` 遍历根节点改为快照：组件在 `OnUpdate` 里生成箭/宝石/特效会让 `m_roots` 扩容，
  导致 2D/3D 幸存者偶发 `0xC0000005` 崩溃

## [0.8.7] - 2026-11-02

### Changed
- 文档整体迁入顶层 `文档/`：`代码总纲/`、`版本文档/`、`教学文档/`（讲义在 `教学文档/讲义/`）；
  原 `docs/` 目录及其中的截图/原理图一并移动
- `README`、`CONTRIBUTING`、`tools/docs-check.ps1`、`tools/add_math_refs.py` 等路径同步更新

### Removed
- 移除随仓库提交的 `ErMao-学习文档.html` 与 `tools/build_docs_site.py`
  （单文件 HTML 文档站及其生成脚本）；文档以 Markdown 为准，不再维护 HTML 产物

## [0.8.6] - 2026-11-02

### Changed
- 文档体系收敛为两类：`docs/版本/`（版本记录/修复记录/构建与运行/发布与工程）与
  `docs/教学/`（引擎架构/课程映射/分章讲解/学习指南/编辑器与关卡/AI 指南/讲义）；
  根目录不再保留十几篇零散文档，统一从 `docs/README.md` 进入
- 新增 `docs/00-代码总纲.md`：`main` → 全部次级函数的调用堆栈、7 张 Mermaid 时序图、
  按层函数索引与“我想改 X”速查表；替代原 `14-从入口到功能的调用栈.md`
- HTML 文档站改为“总纲 / 版本文档 / 教学文档”分组，并支持在页面内渲染 Mermaid 时序图

### Removed
- 合并重复文档：方案文档的版本流水并入版本记录；构建/新电脑/CLion 合并为构建与运行；
  打包/Git/素材许可合并为发布与工程；编辑器与第三人称关卡合并
- 删除 `docs/blog/` 博客规划（10 篇细纲与审稿清单，内容与讲义重复）

## [0.8.5] - 2026-11-02

### Changed
- 调用栈文档改为「大纲优先」：§1 启动 / §2 每帧 / §3 渲染 / §4 资源 / §5 编辑器与 AI / §6 其他子系统 / §7 游戏 / §8 速查 / §9 追栈 / §10 算法入口
- 算法细节压缩为一句 + 指向对应讲义，不再在调用栈文档里展开推导


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
