# ErMao Engine（二毛引擎）方案文档 v0.1

> 文档状态：设计基线（Design Baseline）
> 目标读者：熟悉 UE、读过 Hazel / Piccolo 源码，想通过"自己写一遍"吃透 GAMES104 的开发者
> 关联课程：[GAMES104 现代游戏引擎：从入门到实践](https://games-cn.org/games104/)（BoomingTech / Piccolo）

---

## 0. 一句话定位

**ErMao 是一个 Windows 平台、零第三方依赖、约一万行 C++ 的"可运行教学引擎"**：它用 Piccolo 的模块划分方式组织架构，用 Hazel 的轻量工程化方式落地，把 GAMES104 的 22 讲知识点变成**能在屏幕上看到、能在编辑器里点、能在代码里指认**的具体实现。

它不追求 3A 级画质，追求三件事：

1. **能跑**：`build.bat` 一条命令出 exe，双击即开，有编辑器、有场景、有可玩的 demo。
2. **能讲**：每个模块都能对应到课程的一讲，且对应关系写进 `docs/01-GAMES104-课程映射.md`。
3. **能改**：模块边界清晰、依赖单向，替换渲染后端/加系统不需要动别的层。

---

## 1. 为什么是这套技术选型

### 1.1 语言与构建

| 项 | 选择 | 理由 |
|---|---|---|
| 语言 | C++17 | 与 Piccolo/Hazel 一致；结构化绑定、`std::optional`、`if constexpr` 足够；不追 C++20 modules（MSVC 生态不稳，教学价值低） |
| 构建 | CMake 3.20+（VS 2022 生成器） | 用户已装 VS2022 + CMake；生成 `.sln` 可直接用 VS/CLion 调试 |
| 平台 | Windows 10/11 x64 | 目标机已确认（RTX 3090，D3D11 FL11_0） |
| 依赖 | **零第三方库** | 教学引擎最大的坑是"配环境配三天"。窗口用 Win32，图形用 D3D11，图片用 WIC，字体用 GDI，JSON 自己写 200 行 |

### 1.2 图形 API 为什么选 D3D11 而不是 Vulkan/OpenGL

- Piccolo 用 Vulkan，符合工业界趋势，但**Vulkan 的初始化样板（instance/device/queue/sync/descriptor）会吃掉本项目 30% 的代码量**，而它和 GAMES104 的知识点（渲染管线、光照、阴影、后处理）关系不大。
- OpenGL 在 Windows 上需要 glLoadGen/GLAD 之类的手工函数加载，且已停止演进。
- **D3D11 是 Windows 自带的**（`d3d11.dll` + `d3dcompiler_47.dll` 无需安装），初始化约 150 行，支持 Shader Model 5.0、MRT、Compute Shader、MSAA、Shadow Map，足够覆盖本课程全部渲染话题；并且 **WARP 软光栅可作兜底**，换台没独显的机器也能跑。
- RHI 层刻意做了薄封装（`IDevice` 概念上只有一份 `D3D11Device` 实现），后续加 Vulkan 后端时不需要动 Renderer。

### 1.3 对象模型：UE 风格 GameObject/Component，而不是纯 ECS

GAMES104 第 3 讲与 Piccolo 都用 **GameObject + Component**（UE 同源），第 20 讲才引入 Data-Oriented 思想。因此：

- **上层**：`World` 持有 `GameObject`，GameObject 挂 `Component`（Transform / MeshRenderer / Camera / Light / RigidBody / Animator / NavAgent …），与 UE 的心智模型一致，用户零学习成本。
- **下层**：渲染与物理的每帧数据用 **SoA 数组**（`InstanceData[]`、`Contact[]`）批量提交，体现 DOP；`JobSystem::ParallelFor` 对数组并行。
- **不做**：完整的 Archetype ECS（会显著增加代码量，且与本项目"讲清楚引擎结构"的目标冲突）。文档中说明 ECS 与 GO/Component 的取舍。

### 1.4 编辑器为什么"引擎内"实现

Hazel 的编辑器是独立 exe + 共享 engine 静态库；Piccolo 是运行时内嵌编辑器。ErMao 选**单进程内嵌编辑器**（类似 UE Editor）：

- 复用同一套 Renderer/World/Input，不需要 IPC 或序列化桥；
- **Play-In-Editor** 可以直接用"场景 JSON 快照 → 运行 → 回滚"实现，顺带验证序列化系统的正确性（这是 Hazel 的做法，非常优雅）；
- 代价是编辑器代码会进 runtime 二进制，用 `ER_EDITOR` 宏隔离即可。

---

## 2. 分层架构

```
┌──────────────────────────────────────────────────────────────────────┐
│  Application / Demo Game            runtime/                         │
│  （编辑器模式、Play 模式、示例关卡、玩家控制、演示脚本）                  │
├──────────────────────────────────────────────────────────────────────┤
│  Editor                             editor/                          │
│  即时模式 UI、Hierarchy / Inspector / Console / Stats、Gizmo、拾取     │
├──────────────────────────────────────────────────────────────────────┤
│  Gameplay Systems                   animation/  physics/  ai/        │
│  动画求值&蒙皮、刚体&碰撞&射线、A*寻路&行为树&感知                       │
├──────────────────────────────────────────────────────────────────────┤
│  Scene                              scene/  reflect/                 │
│  World / GameObject / Component / Transform 层级 / 反射 / 序列化       │
├──────────────────────────────────────────────────────────────────────┤
│  Renderer                           renderer/                        │
│  相机、前向管线（Shadow→Sky→Opaque→Debug→Post）、材质、实例化、调试绘制  │
├──────────────────────────────────────────────────────────────────────┤
│  RHI (Thin)                         rhi/                             │
│  Device / SwapChain / Buffer / Texture / Shader / PipelineState      │
├──────────────────────────────────────────────────────────────────────┤
│  Core + Platform                    core/  platform/                 │
│  数学、容器、日志、计时、JSON、GUID、JobSystem、Win32 窗口/输入/文件     │
└──────────────────────────────────────────────────────────────────────┘
       依赖方向严格单向：上层依赖下层，下层不知道上层存在
```

**关键约束**

- `core/` 与 `platform/` 不 include 任何上层头文件；RHI 不认识 Scene。
- Renderer 只吃"扁平化的渲染项列表"（`RenderItem` + `InstanceData`），不认识 GameObject；由 Scene 层负责把 GameObject 翻译成 RenderItem。这条边界是"渲染与场景解耦"的关键，也是 Piccolo 的设计。
- Editor 只通过 Scene 的公共 API 改数据，不直接摸 D3D11。

---

## 3. 目录结构

```
D:\GameEngine\
├─ CMakeLists.txt                  # 顶层：ErMaoEngine(静态库) + ErMao(exe)
├─ build.bat                       # 一键：vcvars + cmake + msbuild
├─ run.bat                         # 运行 build\bin\ErMao.exe
├─ README.md
├─ docs\
│   ├─ 00-ErMao引擎方案文档.md      # 本文档
│   ├─ 01-GAMES104-课程映射.md      # 22 讲 × 代码 × 演示
│   ├─ 02-分章讲解.md               # 逐讲深入讲解（配代码路径）
│   └─ screenshots\                 # 运行截图
├─ engine\
│   ├─ shaders\                     # HLSL，运行时 D3DCompile 编译（改完不用重编 exe）
│   │   ├─ PBR.hlsl  Sky.hlsl  Shadow.hlsl  DebugLine.hlsl  UI.hlsl  Post.hlsl  Skinned.hlsl
│   ├─ assets\
│   │   ├─ scenes\demo.ermao.json   # 编辑器可保存/加载的场景
│   │   ├─ models\                  # .obj
│   │   └─ textures\                # .png（WIC 加载）
│   └─ src\
│       ├─ core\        Types.h Math.h Log.h/.cpp Timer.h Json.h/.cpp Guid.h FileSystem.h/.cpp JobSystem.h/.cpp
│       ├─ platform\    Window.h/.cpp Input.h/.cpp
│       ├─ rhi\         Device.h/.cpp Shader.h/.cpp GpuBuffer.h/.cpp Texture.h/.cpp Pipeline.h/.cpp
│       ├─ renderer\    Camera.h/.cpp Mesh.h/.cpp Material.h/.cpp Renderer.h/.cpp ShadowMap.h/.cpp Sky.h/.cpp DebugDraw.h/.cpp PostProcess.h/.cpp TextRenderer.h/.cpp
│       ├─ scene\       Transform.h World.h/.cpp GameObject.h/.cpp Components.h/.cpp
│       ├─ reflect\     Reflection.h/.cpp Serializer.h/.cpp
│       ├─ asset\       AssetManager.h/.cpp ObjLoader.h/.cpp TextureLoader.h/.cpp PrimitiveMesh.h/.cpp
│       ├─ physics\     PhysicsWorld.h/.cpp Collider.h Raycast.h/.cpp
│       ├─ animation\   AnimationClip.h/.cpp Animator.h/.cpp Skeleton.h/.cpp
│       ├─ ai\          NavGrid.h/.cpp BehaviorTree.h/.cpp Agent.h/.cpp
│       ├─ editor\      Ui.h/.cpp (即时模式控件) Editor.h/.cpp (面板/Gizmo)
│       └─ runtime\     Application.h/.cpp Engine.h/.cpp DemoGame.h/.cpp main.cpp
└─ build\                           # CMake 生成物
```

---

## 4. 帧循环与线程模型

### 4.1 主循环（固定步长仿真 + 可变步长渲染）

```cpp
while (window.Open()) {
    Timer::Tick();                       // 高精度 QPC
    Input::Update();                     // 双缓冲按键状态
    Ui::BeginFrame();                    // 收集输入，构建 UI 顶点

    float frameDt = min(Timer::Delta(), 0.1f);
    accumulator += frameDt;
    while (accumulator >= kFixedDt) {    // 1/60
        if (playing) world.FixedUpdate(kFixedDt);   // 物理/动画/AI
        accumulator -= kFixedDt;
    }
    world.Update(frameDt);               // 逻辑（相机、脚本）
    renderer.Render(world, frameDt);     // 可变步长，可用 alpha 插值
    swapChain.Present(vsync);
}
```

- **为什么固定步长**：物理与动画的稳定性（第 10/11 讲）；`kFixedDt=1/60` 与 UE 的 `FixedTick` 同思路。
- **防死亡螺旋**：`maxSubSteps=5`，卡顿后丢弃积压时间。
- **插值**：Transform 保存 `prev` 与 `cur`，渲染用 `alpha=accumulator/dt` 插值，避免 60Hz 逻辑 + 144Hz 显示时的抖动。

### 4.2 线程

| 线程 | 职责 |
|---|---|
| Main | 窗口消息、输入、场景更新、提交渲染命令 |
| Worker × (N-1) | `JobSystem::ParallelFor`：视锥剔除、物理 broadphase、蒙皮矩阵预计算、地形/网格构建 |
| Present | 由 DXGI flip model 隐式管理（不阻塞 CPU 提交） |

JobSystem = 任务队列 + `std::condition_variable` + 原子计数 + `ParallelFor`（分块），对应第 20 讲。UI 中可切换串行/并行跑同一份压力测试，直接看到加速比。

---

## 5. 模块设计

### 5.1 Core

| 文件 | 内容 | 对应课程 |
|---|---|---|
| `Math.h` | `Vec2/3/4`、`Mat3/Mat4`（列主序，与 HLSL 对齐）、`Quat`、`Transform`、`Ray/AABB/Sphere/Plane`、`Lerp/Slerp`、透视/正交、LookAt、`Decompose` | 第 1/4 讲（数学基础） |
| `Log.h` | 分级日志（Trace/Info/Warn/Error）+ 环形缓冲（编辑器 Console 直接读）+ 文件输出 + `ER_ASSERT` | 第 13 讲（工具链） |
| `Timer.h` | QPC 计时、帧时间滑动平均 | 第 2 讲 |
| `Json.h` | 手写 DOM（对象/数组/数字/字符串/布尔）+ 解析 + 美化输出 + 错误定位 | 第 3/13 讲 |
| `JobSystem` | `ParallelFor(begin,end,fn)`、`parallel_invoke` | 第 20 讲 |
| `FileSystem` | 读文件、路径拼接、`assets/` 定位（exe 相对路径） | — |

### 5.2 Platform

- **Window**：`RegisterClassEx` + `CreateWindowEx`，处理 `WM_SIZE/WM_CLOSE/WM_KEYDOWN/WM_CHAR`，原始输入用 `WM_INPUT`（可选，先走 `WM_KEYDOWN`+`GetAsyncKeyState`）。
- **Input**：键盘 256 键 + 鼠标按键/位置/滚轮，`IsKeyDown / IsKeyPressed / IsKeyReleased`，整帧一致（帧首快照）。
- **光标**：编辑模式下 `RMB` 捕获光标（隐藏 + 锁定），实现 UE 式自由视角。

### 5.3 RHI（薄封装）

```
Device:  ID3D11Device / Context / SwapChain / 默认 RTV+DSV / 屏幕尺寸 / 分辨率变化
Buffer:  Vertex / Index / Constant / Structured，Usage(Immutable/Dynamic)，Map(Discard)
Texture: 2D + mip + SRV，从内存/文件创建，GenerateMips，采样器（Wrap/Clamp/Aniso）
Shader:  Vertex/Pixel/Compute，从 .hlsl 运行时编译（D3DCompile，带 #include 支持），错误框输出
State:   光栅化/深度/混合状态缓存（按 key 懒创建）
```

**错误处理**：所有 HRESULT 走 `ER_CHECK_HR`，失败弹 MessageBox 并写日志，绝不静默。

### 5.4 Renderer

#### 管线（每帧 Pass 顺序）

```
0. BeginFrame        清屏、设置默认 RT、收集 RenderItem
1. ShadowPass        平行光正交相机渲染深度到 2048² D32，PCF 3×3 采样
2. SkyPass           全屏三角形 + 视线方向重建，程序化天空 + 太阳盘（第 6 讲）
3. OpaquePass        前向：GGX + Lambert + 半球环境光；实例化提交；按材质/网格排序
4. DebugPass         线段/线框（AABB、射线、导航网格、骨骼、轨迹），可选叠加
5. PostPass          HDR(RGBA16F) → ACES Tonemap + Gamma + 暗角，输出到 backbuffer
6. UiPass            即时模式 UI（alpha blend、scissor），最后绘制
```

- **HDR 中间目标**：第 7 讲"后处理"的最小闭环；也避免天空/太阳被 LDR 裁剪。
- **实例化**：同 Mesh+Material 的多个对象合成一次 `DrawIndexedInstanced`，实例数据放在 `StructuredBuffer`（DOP）。
- **材质**：`albedo / roughness / metallic / emissive / tiling / 贴图`；无贴图时用 1×1 白纹理，保证 shader 分支单一。
- **相机**：透视/正交、FOV、近远平面、`View/Proj/ViewProj`；编辑器相机与游戏相机分离。

#### 着色器

| 文件 | 用途 | 要点 |
|---|---|---|
| `PBR.hlsl` | 主前向着色 | GGX NDF + Smith 几何项 + Schlick 菲涅尔；方向光 + 最多 8 个点光；阴影 PCF；法线贴图可选 |
| `Shadow.hlsl` | 深度 only | 顶点只做 MVP 变换，PS 空 |
| `Sky.hlsl` | 天空 | 视线方向 + 高度渐变 + 太阳 + 地平线雾 |
| `Skinned.hlsl` | 蒙皮 | 4 骨骼权重，`float4x4 Bones[64]`，与 PBR 共用光照函数 |
| `DebugLine.hlsl` | 线 | 深度测试/覆盖两种模式 |
| `UI.hlsl` | UI | 顶点色 + 纹理 + alpha |
| `Post.hlsl` | 后处理 | 全屏三角形，ACES、gamma、暗角、可选 FXAA |

### 5.5 Scene（第 3 讲：如何构建游戏世界）

- **World**：`vector<unique_ptr<GameObject>>` + `unordered_map<id, GameObject*>`；负责 `Update/FixedUpdate`、序列化、查询（按名字、按组件类型）。
- **GameObject**：`id(u64) / name / active / parent / children / transform / components[]`。
- **Transform**：局部 TRS + 世界矩阵缓存 + 脏标记；父级变化递归标脏；旋转用四元数（避免万向锁，第 8 讲）。
- **Component**：`virtual void OnAttach/OnUpdate/OnFixedUpdate/OnDrawInspector/Serialize`；类型通过反射注册，Inspector 与 JSON 都自动工作。
- **组件清单**（对应课程）：

| 组件 | 作用 | 课程 |
|---|---|---|
| `Transform` | 层级变换 | 3 |
| `MeshRenderer` | 网格 + 材质 + 可见性 | 4/7 |
| `CameraComponent` | 游戏相机（主相机切换） | 4 |
| `DirectionalLight` / `PointLight` | 光照 | 5 |
| `RigidBody` + `BoxCollider/SphereCollider` | 物理 | 10/11 |
| `Animator` + `SkinnedMeshRenderer` | 动画/蒙皮 | 8/9 |
| `NavAgent` | AI 寻路 | 16/17 |
| `PlayerController` | 输入驱动的角色移动（demo） | 15 |

- **Spawning/Destruction**：延迟队列（帧末统一增删），避免遍历时失效——UE 的 `PendingKill` 同思路。

### 5.6 反射与序列化（第 3/13 讲）

```cpp
class MeshRenderer : public Component {
    ER_DECLARE_COMPONENT(MeshRenderer)          // 类型注册
    ER_FIELD(Vec4, color, "Color")              // 字段元数据（偏移 + 类型 + 显示名）
    ER_FIELD(float, roughness, "Roughness", 0.f, 1.f)
};
```

- 宏生成 `static TypeInfo`，登记 `{name, offset, FieldType, min, max}`；`FieldType` 枚举覆盖 bool/int/float/Vec2/3/4/Quat/Color/string/AssetRef/GameObjectRef。
- **Inspector**：遍历 `TypeInfo.fields` 生成控件，新增字段零 UI 代码。
- **Serializer**：同一份元数据 → JSON；场景保存为 `.ermao.json`（可读、可 diff）。
- **Play-In-Editor**：Play 前把 World 序列化成字符串，Stop 时反序列化回滚，保证"运行时改的东西不脏资源"。

### 5.7 Assets（第 3/14 讲）

- `AssetManager`：`unordered_map<string, unique_ptr<Asset>>` 缓存 + 引用计数；路径相对 `assets/`。
- `ObjLoader`：手工解析 `v/vn/vt/f`（支持 `f v/vt/vn` 与负索引），生成 `MeshData{positions,normals,uvs,indices}`，支持子网格。
- `TextureLoader`：WIC 解码 PNG/JPG → `Texture`；`PrimitiveMesh` 程序生成 cube/sphere/plane/cylinder/torus + 骨骼胶囊体。
- 生成式资产兜底：所有 demo 场景不依赖外部文件也能跑（棋盘格/噪声纹理、程序网格），文件存在则优先加载——保证"拿到源码就能跑"。

### 5.8 Physics（第 10/11 讲）

- 积分：半隐式欧拉；重力 + 线性阻尼 + 角阻尼。
- 碰撞：`Sphere-Sphere`、`Sphere-Plane`、`AABB-AABB`、`Sphere-AABB`；静态平面/盒体作为地面与墙。
- 求解：位置修正 + 冲量（法向 + 切向摩擦），迭代 4 次；球体与盒体（盒体用 AABB 近似，注释说明与 OBB/SAT 的差距）。
- 休眠：线速度/角速度低于阈值若干帧后 sleep。
- `Raycast`：对 AABB/Sphere 求交，编辑器点击拾取与 AI 视线检测复用。
- 明确写在文档里：**不做** GJK/EPA、CCD、布料/破坏（第 11 讲后半），列为扩展。

### 5.9 Animation（第 8/9 讲）

- `AnimationClip`：`duration + tracks[{targetNode, T/R/S keys}]`；关键帧线性/球面插值。
- `Animator`：`Play/CrossFade(clip, duration)`、`speed`、`loop`、`time`；每帧求值写回骨骼 Transform。
- **蒙皮**：`SkinnedMeshRenderer` 持有 `bones[]`（GameObject*）+ `inverseBindMatrices`；顶点 4 权重；VS 里 `Σ w_i * Bone_i * v`。程序生成一个 3 骨骼胶囊角色 + 走路/挥手动画，开机即可看到蒙皮。
- 状态机：`Idle/Walk/Wave` 三态 + 参数 `speed` 切换 + 0.25s 交叉淡入，演示第 9 讲的 blend。
- **不做的**：动画压缩、IK、Motion Matching（列扩展）。

### 5.10 AI（第 16/17 讲）

- `NavGrid`：场景 XZ 平面栅格化（0.5m/格），静态碰撞体标记阻挡，8 邻域 **A\***（二叉堆开表），路径平滑（视线拉直）。
- `Agent`：沿路径 steering（到达减速、朝向插值）；状态机 `Patrol → Alert → Chase`；感知 = 距离 + 视野角 + 射线遮挡。
- `BehaviorTree`：`Sequence/Selector/Condition/Action` 组合子，Agent 行为树 JSON 化，可视化运行时状态（UI 里高亮当前节点）。
- **不做的**：NavMesh 多边形生成（CMG/Recast）、GOAP、群体避障（列扩展，文档给算法要点）。

### 5.11 Editor（第 13/14 讲）

即时模式 UI（自研，约 600 行），无 ImGui 依赖：

- **控件**：窗口（拖动/折叠/滚动）、Label、Button、Checkbox、SliderFloat、DragFloat3、ColorEdit、Combo、Tree、TextInput、ProgressBar。
- **字体**：GDI 把 ASCII 32–126 光栅化到 1024×1024 R8 图集（两种字号），UI 用 alpha 纹理采样——不引入 stb_truetype，也不用手写点阵。
- **面板**：Hierarchy（树 + 搜索 + 右键创建/删除）、Inspector（反射自动生成）、Console（日志环形缓冲 + 过滤）、Stats（帧时间/绘制调用/三角面/实体数/线程加速比）、Toolbar（Play/Pause/Stop、保存/加载、Gizmo 模式）。
- **交互**：`RMB` 视角、`WASD/QE` 移动、`Shift` 加速、鼠标左键拾取（射线 vs AABB）、`W/E/R` 切换平移/旋转/缩放 Gizmo，拖动轴约束（射线 vs 平面）。
- **Play 模式**：快照回滚；游戏相机接管；`Esc` 退出。

### 5.12 Runtime / Demo

- `Application`：持有 Window/Device/Renderer/World/Editor，负责初始化顺序、主循环、关闭。
- `DemoGame`（Play 模式内容）：
  1. 一个程序化地形/地面 + 若干箱体/球体（阴影、PBR 材质）；
  2. 一个 3 骨骼角色（蒙皮动画），`WASD` 移动、`Shift` 跑，动画状态机切换；
  3. 两个 AI 小怪用 A* 巡逻/追击，行为树控制，视野锥可见；
  4. 可点击发射小球（物理弹跳），轨迹与碰撞可视化；
  5. `Tab` 切换线框/调试绘制，`F1` 统计面板。

---

## 6. 启动与关闭顺序（关键，容易踩坑）

```
Init:  Log → Window → Device(SwapChain) → Renderer(Shader/管线) → AssetManager
       → World(DemoScene) → Editor → 主循环
Tick:  Window.PumpMessages → Input.Update → Editor/Game Update → Render → Present
Exit:  Device.WaitIdle → 释放顺序与创建相反 → 日志 flush → 关闭窗口
```

D3D11 的坑：**Present 前必须先 `OMSetRenderTargets` 回 backbuffer**（后处理 pass 会切 RT）；窗口 resize 必须 `WaitIdle → Release RTV/DSV → ResizeBuffers → 重建`；`WM_SIZE` 为 0（最小化）时跳过渲染。

---

## 7. GAMES104 22 讲 × ErMao 实现映射（概览）

> 详细版（含代码路径、演示操作、可讲的原理图）见 `docs/01-GAMES104-课程映射.md` 与 `docs/02-分章讲解.md`。

| 讲 | 主题 | ErMao 里的落点 | 打开就能看到 |
|---|---|---|---|
| 01 | 引擎导论 | `docs/` 架构图、`Engine.h` 初始化顺序 | 启动日志打印 6 个阶段 |
| 02 | 分层架构 | 第 2 节分层图，`core→rhi→renderer→scene` 单向依赖 | 代码 grep 依赖关系 |
| 03 | 构建游戏世界 | `World/GameObject/Component/Transform` + 反射序列化 | Hierarchy 增删、保存/加载 |
| 04 | 渲染基础 | `Device/SwapChain/Shader/Mesh`，顶点/索引/常量缓冲 | 屏幕上第一个带光照的立方体 |
| 05 | 光照材质着色器 | `PBR.hlsl`（GGX/Lambert/环境光）、材质参数 | Inspector 拖 roughness 实时变化 |
| 06 | 自然渲染 | `Sky.hlsl` 程序化天空 + 平行光太阳 | 天空/地平线/太阳 |
| 07 | 管线与后处理 | Shadow→Sky→Opaque→Debug→Post 六段式 + ACES tonemap | `F1` 看每 pass 耗时 |
| 08 | 动画基础 | `AnimationClip/Animator`、TRS 插值、蒙皮 | 角色挥手/走路 |
| 09 | 动画进阶 | 状态机 + 0.25s 交叉淡入 | 走→停 平滑过渡 |
| 10 | 物理基础 | 积分器、碰撞检测、冲量求解 | 小球下落弹跳不穿地 |
| 11 | 物理应用 | 射线拾取、触发器、关节（简化） | 点击物体、AI 视线 |
| 12 | 特效 | 调试粒子/轨迹（Points + 加法混合） | 小球拖尾 |
| 13 | 工具链 | 编辑器 UI、Inspector、日志 Console | 编辑器全貌 |
| 14 | 工具链进阶 | Play-In-Editor 快照、Gizmo、场景序列化 | Play/Stop 回滚 |
| 15 | Gameplay 复杂性与积木 | `PlayerController`、组件组合、事件（简化） | WASD 控制角色 |
| 16 | 基础 AI | NavGrid + A* + 状态机 + 感知 | 小怪绕柱追击 |
| 17 | 进阶 AI | 行为树 + 运行时可视化 | 行为树节点高亮 |
| 18 | 网络基础 | 文档讲解 + `NetSerialization` 骨架（不联机） | — |
| 19 | 网络进阶 | 文档讲解（状态同步/插值/回滚） | — |
| 20 | DOP 与 Job System | SoA 实例数组、`ParallelFor` 剔除/物理、加速比面板 | Stats 面板串行/并行对比 |
| 21 | 动态 GI / Lumen | 文档讲解 + 简化 SSGI 开关（可选） | — |
| 22 | GPU Driven / Nanite | 文档讲解 + 实例化/间接绘制已具备的基础 | — |

> 18/19/21/22 采用"代码骨架 + 文档精讲"的方式：这四讲在单机教学引擎里实现成本极高（需要服务器、GPU 驱动管线），强行实现会稀释前面的质量。文档会给出与 Piccolo 实现对照的要点，并预留接口。

---

## 8. 里程碑（每个里程碑都必须"能编译 + 能运行 + 有截图"）

| 阶段 | 内容 | 验收 |
|---|---|---|
| **M0** | 方案文档（本文）+ 工具链验证 | 文档评审通过；D3D11 probe 出画面 |
| **M1** | Core/Platform/RHI + 清屏 + 三角形 | exe 启动出窗口与彩色三角形，60FPS |
| **M2** | 数学/相机/网格/天空/光照 | 带阴影的 PBR 场景，鼠标可转视角 |
| **M3** | Scene/反射/序列化/资源 | 场景 JSON 保存后重新加载完全一致 |
| **M4** | 编辑器（UI/层级/检视器/Gizmo/PIE） | 点选物体、改参数、Play/Stop 回滚 |
| **M5** | 物理/动画/AI | 球体弹跳、角色蒙皮走路、小怪 A* 追击 |
| **M6** | 打磨 + 文档（映射表 + 分章讲解）+ 截图 | README 一页跑通；22 讲映射完整 |

---

## 9. 构建与运行

```bat
:: 一键构建（VS2022 x64 Release）
build.bat

:: 运行
build\bin\ErMao.exe
```

要求：Windows 10 1809+、VS2022（MSVC v143）、Windows SDK 10、CMake ≥ 3.20。
可选：`build.bat debug` 生成 Debug 版（带 D3D11 调试层、断言）。

**操作**：`RMB` 转视角 / `WASD` 移动 / `Shift` 加速 / 左键选中 / `W/E/R` Gizmo / `F5` Play / `Esc` 退出 Play / `F1` 统计。

---

## 10. 风险、取舍与演进

| 风险 | 对策 |
|---|---|
| 自研 UI 工作量大 | 控件只做 Inspector/Hierarchy 必需的 12 个；字体用 GDI 图集，不碰排版引擎 |
| 无外部美术资源 | 程序化网格 + 程序化纹理；OBJ/PNG 加载作为"有则用"的增强路径 |
| D3D11 状态管理易错 | 所有状态集中在 `Pipeline` 对象，禁止裸调 `OMSet*`；日志记录每次状态切换 |
| 物理精度不足 | 定位为"教学级"：球/盒 + 静态平面，明确不追求堆叠稳定性；文档给出 GJK/CCD 的演进路径 |
| 范围膨胀 | 里程碑制，M1–M5 每步可运行；18/19/21/22 只写文档与接口 |

**演进路线（v0.2+）**：多线程渲染命令缓冲 → 延迟渲染/集群光照 → NavMesh 生成 → 骨骼 IK → 网络状态同步 demo → Vulkan 后端。

---

## 11. 命名与代码规范

- 命名空间 `erm`；类型 `PascalCase`；成员 `m_` 前缀；常量 `k` 前缀；宏 `ER_` 前缀。
- 头文件 `#pragma once`；include 顺序：标准库 → 平台 → 本项目。
- 禁止异常跨模块边界；资源用 RAII（ComPtr）。
- 每个 `cpp` 顶部用一行注释标注"对应 GAMES104 第 N 讲"，方便学习时定位。

---

## 12. 交付状态（v0.1 实现结果，与本文档的差异都写在这里）

### 12.1 已完成（代码在仓库里，可运行、可交互）

| 模块 | 状态 | 说明 |
|---|---|---|
| Platform / Core | ✅ | Win32 窗口、输入、QPC 计时、日志（控制台+文件+缓冲）、手写 JSON、文件系统 |
| RHI (D3D11) | ✅ | 设备/交换链、Buffer、Texture（WIC + 程序化 + mipmap）、RenderTarget、运行时 Shader 编译、截图 |
| Renderer | ✅ | HDR 前向管线、GGX PBR、平行光/点光、2048² 阴影 PCF、程序化天空、实例化合批、视锥剔除、调试线、ACES 后处理 |
| Scene | ✅ | GameObject/Component/层级 Transform、延迟销毁、反射、JSON 存档、PIE 快照回滚 |
| Assets | ✅ | 程序化网格/纹理 + OBJ + WIC 贴图，缓存 |
| Physics | ✅ | 半隐式欧拉、球/盒碰撞、静态盒与地面、冲量、摩擦、休眠、迭代求解 |
| AI | ✅ | 导航栅格、A\*（二叉堆/8 邻域/视线拉直）、巡逻-追击状态机、距离+FOV+遮挡感知、调试可视化 |
| Animation | ◐ | 关键帧 TRS 节点动画（风车、浮动水晶）；**蒙皮未实现** |
| Editor | ✅ | 即时模式 UI、动态 Unicode 字库、层级/检视器/控制台/统计、拾取、Play/Pause/Stop、保存/读取 |
| JobSystem | ◐ | `ParallelFor`（每次调用起线程）+ 基准测试；用于视锥剔除 |

### 12.2 与方案文档的偏差（重要，避免误解）

1. **JobSystem 改为"每次 ParallelFor 临时起线程"**：原计划的常驻线程池在调试期出现过非确定性崩溃，最终定位是场景对象所有权 bug（已修复），但为了保证教学版稳定性，采用了更简单、无生命周期风险的模型。文档第 4.2 节描述的是目标形态。
2. **动画只做到节点关键帧**：蒙皮（骨骼调色板 + 顶点权重）没有实现，`docs/01` 与 `docs/02` 都标注了 `◐` 并给出扩展步骤。
3. **未实现**：粒子/特效系统（12 讲）、行为树（17 讲）、网络（18/19 讲）、GI（21 讲）、GPU Driven（22 讲）、Gizmo、撤销栈、音频。
4. **场景规模**：演示场景 27 个对象，面向"能讲清楚"而不是"压力测试"；`JobSystem` 的加速比用 CPU 基准按钮单独展示。

### 12.3 验证方式（可复现）

```bat
build.bat
build\bin\ErMao.exe --frames 30                                   :: 编辑器
build\bin\ErMao.exe --frames 150 --play --noeditor                :: Play + 物理 + AI + 动画
build\bin\ErMao.exe --debugview 1 --frames 20 --noeditor          :: 阴影系数视图
```

- 连续 8 次启动/退出无崩溃（Release）；
- 截图见 `docs/screenshots/`；
- 编辑器内 Play → Stop 可回滚场景；Save → Load 可复现场景。

### 12.4 下一步（按性价比排序）

1. 骨骼蒙皮 + 动画状态机（第 8/9 讲）；
2. Gizmo 与撤销栈（第 14 讲）；
3. 行为树与 NavMesh（第 17 讲）；
4. 粒子系统（第 12 讲）；
5. 常驻线程池 + 无锁队列（第 20 讲）。


---

## 13. v0.2 交付（2026-10-30）

### 13.1 主要变化

| 方向 | 内容 |
|---|---|
| UI | 自研即时模式 UI 换成 **Dear ImGui 1.93.1（docking）**；Dock 布局 + 菜单/工具栏/状态栏 + 七大面板；离屏视口（场景渲染到 RT 再 `ImGui::Image`） |
| 工具链 | 世界大纲（层级/搜索/增删改）、资源浏览器（资产表/搜索/**导入 OBJ 与贴图**/双击放入场景）、反射检视器、控制台、统计（GPU 时间戳）、设置 |
| 关卡 | 新增**代码优先**的第三人称关卡：台阶→平台链→空中长廊→终点光环，含外部 OBJ 水晶、风车关键帧动画、3 个 A* 巡逻 AI |
| 玩法 | `ThirdPersonController`：相机相对移动、重力/跳跃/地面射线、球-AABB 推离、轨道相机 + 弹簧臂 |
| 架构 | `Application` → `Engine`（编辑器/Play/离屏渲染三态）；`Renderer` 支持"渲染到后备缓冲"与"渲染到视口 RT"两种输出 |
| 质量 | 按审核报告修复 P0×1、P1×4、P2×13、P3×15（详见 `docs/06-审核修复记录.md`） |

### 13.2 与本文档的偏差

1. **RHI 定位**：接受审核意见，明确 ErMao 的 RHI 是"D3D11 便利封装"，**不承诺可替换后端**；
   渲染器直接使用 D3D11 上下文。文档 §1.2 的"换 Vulkan 不用动 Renderer"一句作废。
2. **UI 预算**：原计划自研 600 行 UI，实际接入 ImGui（第三方源码随仓库分发），
   把预算转投资源导入与第三人称玩法。
3. **线程模型**：JobSystem 采用"每次 ParallelFor 临时起线程"，不是常驻线程池（已在 §4.2 说明取舍）。
4. **未实现项**不变：蒙皮、粒子、行为树、网络、GI、GPU Driven、Gizmo/撤销栈。

### 13.3 验证

```bat
build.bat
build\bin\ErMao.exe                                   :: 编辑器
build\bin\ErMao.exe --smoke --frames 200              :: 角色移动/跳跃（日志有坐标）
build\bin\ErMao.exe --test-serialize --frames 3       :: 场景 JSON 字节级往返
build\bin\ErMao.exe --import engine\assets\models\crystal.obj --frames 3   :: 资源导入
build\bin\ErMao.exe --noeditor --play --frames 120 --screenshot game.png   :: 第三人称截图
```
