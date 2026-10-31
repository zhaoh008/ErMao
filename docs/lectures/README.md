# ErMao 讲义：按 GAMES104 的 22 讲逐步拆解

> 读者假设：计算机科学与技术专业本科毕业，会 C++、线代、微积分，但没写过引擎。
> 写法：**先给数学依据，再给代码位置，最后给可复现实验**。公式不装饰，只写推导必需的部分。
>
> 配套代码：`engine/src/`，每讲开头列文件；每讲结尾列验证命令与自测题。

## 怎么读

1. **按顺序读**：01→22 是依赖顺序，后面的讲会用到前面的结论（例如 08 蒙皮要用 03 的四元数、04 的矩阵约定）。
2. **每讲三段式**：
   - `数学`：定义、推导、为什么是這個形式；
   - `代码`：在哪个文件、哪个函数，关键 10~30 行；
   - `实验`：一条命令或一个操作，能看到/测出结论。
3. **不要跳过自测题**：每题都能在代码里找到答案。

## 先读这两个

- [第 0 课：从黑屏到第一个三角形](L00-从黑屏到第一个三角形.md) —— 没写过渲染器的读者从这里开始。
- [术语表](术语表.md) —— 遇到不认识的词先查这里。

## 学习路线（选一条）

| 路线 | 读什么 | 跳过什么 |
|---|---|---|
| **两周看懂骨架** | 00 → 01 → 02 → 03 → 04 → 14 → 15 | 05/06/07 的所有公式、08/09、20/21/22 |
| **我要改渲染** | 04 → 05 → 06 → 07 → 08 → 22 | 10/11/16/17（知道有这回事即可） |
| **我要做玩法** | 03 → 14 → 15 → 10 → 11 → 16 → 17 | 05/06/07 的推导、20/22 |
| **全部** | 01 → 22 | — |

**这些内容和"学引擎"无关，别花时间**：打包器（Packager）、Linux 源码包、文件对话框、`imgui.ini` 布局、Windows 消息循环细节。它们是工程配套，不是原理。

## 难度标注

- `[必读]` 引擎骨架：01 02 03 04 14 15
- `[选读]` 想做玩法/工具再读：10 11 13 16 20
- `[进阶]` 可以先跳过：05 06 07 08 09 17 21 22（用到再回来）
- `[设计]` 只有设计没有实现：18-19 21 22

## 目录

| 讲 | 标题 | 代码落点 | 状态 |
|---|---|---|---|
| [01](L01-引擎架构与主循环.md) | 引擎架构与主循环 | `runtime/Engine` `core/Timer` | ✅ |
| [02](L02-分层与依赖方向.md) | 分层与依赖方向 | `engine/src/*` | ✅ |
| [03](L03-游戏世界与变换.md) | 游戏世界、变换与四元数 | `scene/` `core/Math.h` | ✅ |
| [04](L04-渲染管线与坐标空间.md) | 渲染管线与坐标空间 | `renderer/Renderer.cpp` `renderer/Camera` | ✅ |
| [05](L05-光照与BRDF.md) | 光照、BRDF 与能量守恒 | `shaders/PBR.hlsl` | ✅ |
| [06](L06-阴影与天空.md) | 阴影映射与程序化天空 | `shaders/Shadow.hlsl` `Sky.hlsl` | ✅ |
| [07](L07-后处理与色彩.md) | HDR、色调映射与 Gamma | `shaders/Post.hlsl` | ✅ |
| [08](L08-骨骼与蒙皮.md) | 骨骼、绑定姿势与线性混合蒙皮 | `animation/Skeleton` `Skinned.hlsl` | ✅ |
| [09](L09-动画混合.md) | 关键帧插值、Cross-fade 与状态 | `animation/Skeleton.cpp` | ✅ |
| [10](L10-物理基础.md) | 半隐式欧拉、冲量与摩擦 | `physics/PhysicsWorldSim.cpp` | ✅ |
| [11](L11-碰撞与查询.md) | AABB、射线求交与空间查询 | `scene/WorldQuery.cpp` | ✅ |
| [12](L12-粒子与特效.md) | 粒子系统：发射、模拟与 Billboard | `game/ParticleEmitter` `Particle.hlsl` | ✅ |
| [13](L13-编辑器与工具链.md) | 即时模式 UI、拾取与 Gizmo | `editor/` | ✅ |
| [14](L14-序列化与反射.md) | 反射、JSON 与场景往返 | `scene/Reflection` `core/Json` | ✅ |
| [15](L15-Gameplay框架.md) | 组件、固定步长与角色控制 | `game/ThirdPersonController` | ✅ |
| [16](L16-AI与寻路.md) | 导航栅格、A\* 与感知 | `ai/NavGrid` `ai/Agents` | ✅ |
| [17](L17-行为树.md) | 行为树：三态、Running 续跑、冷却 | `ai/BehaviorTree` `EnemyComponent` | ✅ |
| [18-19](L18-网络基础.md) | 网络基础与同步（设计） | — | ✗ 设计 |
| [20](L20-数据布局与JobSystem.md) | SoA、并行分块与 Amdahl | `core/JobSystem` | ✅ |
| [21](L21-GI与光照探针.md) | 全局光照近似（设计） | `World::ambient*` | ◐ 近似 |
| [22](L22-GPU Driven.md) | GPU Driven 与间接绘制（设计 + 现状） | — | ✗ 设计 |

## 实验环境

```bat
build.bat
build\bin\ErMao.exe                 :: 编辑器
build\bin\ErMao.exe --level skinning --play --noeditor --frames 120 --screenshot skin.png
```

所有截图在 `docs/screenshots/`，CLI 参数见 `docs/03-构建与运行.md`。
