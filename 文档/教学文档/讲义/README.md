# ErMao 讲义：从“会写 C++”到“看懂引擎每个模块”

> **读者定位**：会写 C++、用过 UE 做玩法/蓝图，但没有从零实现过引擎各模块。
> 不需要图形学、物理仿真、动画、音频或引擎架构的前置知识；新的专有名词在正文首次出现处解释。
>
> **写法**：每讲都从“一个能观察到的现象”开始 → 先给能跑的最简实现 → 用具体数字让它失败 →
> 再引入正式概念 → 回到本仓库源码 → 走一遍可验算的小例子 → 最后才是数学依据、工程取舍和实验。
> 公式和优化不再放在理解流程之前。
>
> **规模**：L00 + GAMES104 主线 + 工程专题（L20–L26），共 26 篇（L18/19 合为一篇）。

## 怎么读

1. **按顺序读**：01→22 是依赖顺序。后面的讲会用到前面的结论（例如 L08 蒙皮要用 L03 的矩阵/四元数、L04 的坐标空间）。
2. **每讲先读“问题”和“最简实现”**：这两节不需要数学；看懂“为什么原来的办法不够”，再进入正式方案。
3. **“源码”一节对照仓库读**：所有代码块都标了是**概念伪代码**、**源码节选**还是**尚未实现的设计**，不要把它们混为一谈。
4. **“进阶”可以第二遍再读**：数学推导、稳定域、性能模型都在这里，不影响理解主流程。
5. **自测题带答案位置**：做不出来时回到对应小节，而不是背结论。

## 学习路线（选一条）

| 路线 | 读什么 | 可以跳过 |
|---|---|---|
| **两周看懂骨架** | L00 → L01 → L02 → L03 → L04 → L14 → L15 | L05–L07 的进阶数学、L08/L09、L20–L22 |
| **我要改渲染** | L00 → L04 → L05 → L06 → L07 → L08 → L22 | L10/L11/L16/L17（知道有这回事即可） |
| **我要做玩法** | L03 → L14 → L15 → L10 → L11 → L16 → L17 | L05–L07 的推导、L20/L22 |
| **全部** | L00 → L26 | — |

**这些内容和“学引擎”无关，别花时间**：打包器（Packager）、Linux 源码包、文件对话框、`imgui.ini` 布局、Windows 消息循环细节。它们是工程配套，不是原理。

## 难度与实现状态

- `[必读]` 引擎骨架：L01 L02 L03 L04 L14 L15
- `[选读]` 想做玩法/工具再读：L10 L11 L13 L16
- `[进阶]` 可以先跳过：L05 L06 L07 L08 L09 L17 L20 L21 L22
- 状态标记：**已实现** = 源码可指认；**部分实现** = 只有简化版；**尚未实现的设计** = 只讲方案，不冒充代码。

## 目录

| 讲 | 标题 | 代码落点 | 状态 |
|---|---|---|---|
| [00](L00-从黑屏到第一个三角形.md) | 从黑屏到第一个三角形 | `rhi/Device` `rhi/Shader` `renderer/Mesh` | 已实现 |
| [01](L01-引擎架构与主循环.md) | 引擎架构与主循环 | `runtime/Engine` `core/Timer` | 已实现 |
| [02](L02-分层与依赖方向.md) | 分层与依赖方向 | `engine/src/*` | 已实现 |
| [03](L03-游戏世界与变换.md) | 游戏世界、变换与四元数 | `scene/` `core/Math.h` | 已实现 |
| [04](L04-渲染管线与坐标空间.md) | 渲染管线与坐标空间 | `renderer/Renderer.cpp` `renderer/Camera` | 已实现 |
| [05](L05-光照与BRDF.md) | 光照、BRDF 与能量守恒 | `shaders/PBR.hlsl` | 已实现 |
| [06](L06-阴影与天空.md) | 阴影映射与程序化天空 | `shaders/Shadow.hlsl` `Sky.hlsl` | 已实现 |
| [07](L07-后处理与色彩.md) | HDR、色调映射与 Gamma | `shaders/Post.hlsl` | 已实现 |
| [08](L08-骨骼与蒙皮.md) | 骨骼、绑定姿势与线性混合蒙皮 | `animation/Skeleton` `Skinned.hlsl` | 已实现 |
| [09](L09-动画混合.md) | 关键帧插值、Cross-fade 与状态 | `animation/Skeleton.cpp` | 已实现 |
| [10](L10-物理基础.md) | 半隐式欧拉、冲量与摩擦 | `physics/PhysicsWorldSim.cpp` | 已实现（教学级） |
| [11](L11-碰撞与查询.md) | AABB、射线求交与空间查询 | `scene/WorldQuery.cpp` | 已实现 |
| [12](L12-粒子与特效.md) | 粒子系统：发射、模拟与 Billboard | `game/ParticleEmitter` `Particle.hlsl` | 已实现 |
| [13](L13-编辑器与工具链.md) | 即时模式 UI、拾取与 Gizmo | `editor/` | 已实现 |
| [14](L14-序列化与反射.md) | 反射、JSON 与场景往返 | `scene/Reflection` `core/Json` | 已实现 |
| [15](L15-Gameplay框架.md) | 组件、固定步长与角色控制 | `game/ThirdPersonController` | 已实现 |
| [16](L16-AI与寻路.md) | 导航栅格、A\* 与感知 | `ai/NavGrid` `ai/Agents` | 已实现 |
| [17](L17-行为树.md) | 行为树：三态、Running 续跑、冷却 | `ai/BehaviorTree` `EnemyComponent` | 已实现 |
| [18-19](L18-网络基础.md) | 网络基础与同步 | — | 尚未实现的设计 |
| [20](L20-数据布局与JobSystem.md) | SoA、并行分块与 Amdahl | `core/JobSystem` | 已实现 |
| [21](L21-GI与光照探针.md) | 全局光照近似与探针 | `World::ambient*` | 部分实现（仅环境光） |
| [22](L22-GPU%20Driven.md) | GPU Driven：compute 剔除 + 间接绘制 | `shaders/Cull.hlsl` `Renderer::GpuCulledDraw` | 已实现（最小闭环） |
| [23](L23-2D渲染与像素游戏.md) | 2D 渲染与像素风幸存者游戏 | `Camera2D` `SpritePass` `game/Survivors` | 已实现 |
| [24](L24-3D幸存者与外部模型.md) | 3D 幸存者：外部 OBJ、材质、粒子特效 | `ObjLoader` `GetMeshAsset` `Survivors3D` | 已实现 |
| [25](L25-音频PCM与混音.md) | 音频：WAV/PCM 与混音 | `platform/Audio` `AudioSystem` | 已实现 |
| [26](L26-Vulkan后端阶段一.md) | Vulkan 阶段一：实例/设备/交换链/三角形 | `rhi/VulkanBackend` | 部分实现（阶段一） |

## 术语与资料

- 遇到不认识的词，先查 [术语表](术语表.md)；正文首次出现处也会就地解释，不要求来回跳转。
- 讲义里的 `[RTR4]`、`[FIXED]` 等引用，完整条目在 [参考资料](参考资料.md)。

## 实验环境

```bat
build.bat
build\bin\ErMao.exe                 :: 编辑器
build\bin\ErMao.exe --demo2d        :: 2D 幸存者 Demo
build\bin\ErMao.exe --demo3d        :: 3D 幸存者 Demo
```

所有截图在 `文档/screenshots/`，CLI 参数见 [构建与运行](../../版本文档/构建与运行.md)。
讲义中的实验如果标注“本次未实际运行”，表示编写环境不是 Windows，不能拿它当实测日志。
