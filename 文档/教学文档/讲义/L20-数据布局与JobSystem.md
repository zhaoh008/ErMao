# 第 20 讲：数据布局与 Job System【选读】

> 对应 GAMES104 第 20 讲（DOP 与 Job System）。
> 代码：`engine/src/core/JobSystem.h/.cpp`（**已实现**）、
> `engine/src/renderer/Renderer.cpp` 的 `BuildBatches`（视锥剔除，**部分实现**）、
> 统计面板的“CPU 基准（100 万次）”。
> 目标：讲清楚“为什么 AoS 慢、SoA 快”“线程开多少”“并行到底能快多少”。
> 实现状态：**已实现** = `JobSystem::ParallelFor`（原子领块、临时线程）、AoS 实例数据、SoA 可见性数组；
> **部分实现** = `BuildBatches` 的并行剔除只在 CPU 剔除模式执行；**未实现的设计** = 常驻线程池/工作窃取。

## 1. 现象：1000 个物体每帧更新很慢

场景里有 1000 个物体，每帧要做两件事：

1. 更新：`pos += vel * dt`，偶尔加一点重力/阻尼；
2. 渲染前剔除：算世界包围盒，判断在不在相机视锥里。

在编辑器里看统计，帧时间 20 ms 上下，其中“更新 + 剔除”占了不少；把物体加到
5000 个，帧时间继续涨。任务管理器里只有一个核在满负荷，其他核闲着。

第一反应通常是“开线程”。但在开线程之前，先看数据是怎么摆的。

## 2. 最简实现：AoS + 单线程循环

**AoS（Array of Structures，结构体数组）** 是最自然的写法：一个物体一个结构体，
结构体里放它全部字段。

```cpp
// 概念伪代码：AoS + 单线程
struct Object {
    Vec3  pos;      // 12 B
    Vec3  vel;      // 12 B
    float mass;     // 4 B
    float radius;   // 4 B
    u32   id;       // 4 B
    u32   flags;    // 4 B
    // 为了对齐再补 24 B，合计 64 B（假设）
};
std::vector<Object> objects;   // 1000 个，共 64 KB

void Update(float dt) {
    for (Object& o : objects) {
        o.vel.y -= 9.8f * dt;          // 重力
        o.pos += o.vel * dt;           // 位置积分
    }
}
```

这段代码正确、直白，1000 个物体也不慢。问题出现在规模变大、并且要和其他系统
（剔除、动画、粒子）抢同一个主线程的时候。

## 3. 让简单实现具体地失败

### 3.1 缓存不友好：读 12 B，搬 64 B

CPU 从内存读数据是按**缓存行（cache line）** 搬的，x86 上通常是 64 B。
AoS 里一个 `Object` 正好 64 B，所以：

- 遍历 1000 个物体，即使只读 `pos`（12 B），也要搬 `1000 × 64 B = 64 KB`，
  即 **1000 条缓存行**；
- 真正用到的 `pos` 只有 `1000 × 12 B = 12 KB`，有效带宽约 18.75%。

如果只读 `pos` 和 `vel`（24 B），有效利用率 37.5%。这不是“内存不够”，
而是**每条缓存行里大部分字节被白白搬进缓存**，还可能把有用的数据挤出去。

### 3.2 单核瓶颈：主线程被占住

更新循环是串行的：1000 个物体、每个假设 2 µs（含少量数学运算），就是 2 ms；
60 FPS 的预算是 16.7 ms，占 12%。看起来还好，但“更新 + 剔除 + 实例填充 + 提交”
串起来就可能超过预算，而其他 15 个核在旁观。

### 3.3 任务切分不均：不是“除以线程数”就完事

假设直接把 1000 个物体分成 16 份，每份 62.5 个：前 8 份 63 个、后 8 份 62 个，
看起来均匀。但如果某些物体带的组件多、更新更贵（例如触发器等），
固定切分就会出现“有的线程早干完、有的还在跑”，最后仍要等最慢的那个。

真实渲染器里的剔除更明显：一个物体的 AABB 可能很大，8 个角点变换都做；
另一个物体很小。**工作量不是按“物体个数”均匀分布的。**

## 4. SoA：把字段拆成各自的数组

**SoA（Structure of Arrays，数组的结构）** 把每个字段单独放一个数组：

```cpp
// 概念伪代码：SoA
struct Objects {
    std::vector<Vec3>  pos;    // 12 KB
    std::vector<Vec3>  vel;    // 12 KB
    std::vector<float> mass;   // 4 KB
    std::vector<u32>   id;
    std::vector<u32>   flags;
};
```

只读 `pos` 时，缓存行里全是 `pos`：`1000 × 12 B = 12 KB`，约 **188 条缓存行**，
比 AoS 的 1000 条少 5 倍多。只读 `pos + vel` 时是 375 条，仍比 AoS 少。

SoA 还有两个好处：

- **SIMD 友好**：一条指令可以处理 4/8 个连续的 `float`，因为同字段在内存里连续；
- **按需加载**：更新只碰 `pos/vel`，渲染只碰 `pos`，不会把 `mass/flags` 拉进来。

但 SoA 不是无条件更好：

| 场景 | 更适合 |
|---|---|
| 批量、同构计算（粒子、剔除、蒙皮） | SoA |
| 每个对象都要访问全部字段、对象数量少 | AoS |
| 需要把数据整体传给 GPU（顶点/实例布局） | AoS（GPU 期望交错布局） |
| 随机访问单个对象的多个字段 | AoS（一次缓存行拿全） |

本工程的实例数据是 **AoS**：`InstanceVertex` 是 9 个 `Vec4`，共 **144 B**
（世界矩阵 4 列 + 法线矩阵 3 列 + albedo + 参数），因为 GPU 的输入布局本来就要求
每个实例的数据连续交错。但**剔除**用的是 SoA 思路：`m_visibility` 是一个独立的
`std::vector<u8>`，剔除循环只写它，不碰 144 B 的实例数据。

## 5. ParallelFor：真实接口与调度

`JobSystem` 是一个全静态类（`core/JobSystem.h`）：

```cpp
// 源码节选：core/JobSystem.h
static void Init(u32 threadCount = 0);   // 0 = 硬件线程数
static void Shutdown();
static u32  ThreadCount();
static void ParallelFor(u32 begin, u32 end,
                        const std::function<void(u32)>& body,
                        u32 chunkSize = 32);
static void ParallelFor(u32 begin, u32 end,
                        const std::function<void(u32, u32)>& rangeBody,
                        u32 chunkSize = 32);
static float Benchmark(bool parallel, u32 iterations);
```

实现（`core/JobSystem.cpp`）不是常驻线程池，而是**每次调用临时拉起线程**：

```text
// 概念伪代码：ParallelFor 的实际调度
if (end <= begin) return;
if (s_threadCount <= 1 || total <= chunkSize) { rangeBody(begin, end); return; }

chunks  = ceil(total / chunkSize)
workers = min(chunks, s_threadCount)
nextChunk = 0
worker():
    loop:
        index = nextChunk.fetch_add(1)   // 原子领块
        if index >= chunks: break
        rangeBody(begin + index*chunkSize, min(...))
主线程也执行 worker()，再 join 其余 workers-1 个线程
```

三个要点：**动态领块**（不是“线程 i 固定做第 i 块”，而是原子计数器抢下一块，
先做完的继续领，缓解负载不均）；**主线程参与**（不只等不干）；**每次调用创建/销毁线程**
（比常驻池多几十微秒开销，但没有生命周期/唤醒丢失风险；教学规模一帧一次可接受）。

`chunkSize` 的取舍：太小 → 原子领块和线程创建/同步占比升高；太大 → 块数少，
`workers = min(chunks, threadCount)` 会把并行度卡住（例如 6401 个物体、
`chunkSize = 4096` 只有 2 块，16 核也只能用 2 核）；默认 32，渲染剔除传 128。

> 注意：这不是 work stealing（工作窃取）。work stealing 是每线程本地队列、空闲时
> 从别人队列偷任务；这里是共享一个原子计数器领块，实现和开销模型不同。参考 [JOB]。

## 6. 回到源码：`Renderer::BuildBatches` 怎么用并行

`BuildBatches`（`Renderer.cpp`）每帧做四件事：

```cpp
// 源码节选：Renderer.cpp（简化，保留结构与常量）
const u32 itemCount = static_cast<u32>(scene.items.size());
m_visibility.assign(itemCount, 1);

if (cull && itemCount > 0) {
    JobSystem::ParallelFor(
        0, itemCount,
        [&](u32 i) {
            const RenderItem& item = scene.items[i];
            if (!item.mesh || !item.mesh->IsValid()) { m_visibility[i] = 0; return; }
            const AABB& b = item.mesh->Bounds();
            // 8 个角点变换到世界空间，重新求 AABB
            AABB world; world.min = item.world.TransformPoint(corners[0]); ...
            for (int c = 1; c < 8; ++c) world.Expand(item.world.TransformPoint(corners[c]));
            m_visibility[i] = frustum.IntersectsAABB(world) ? 1 : 0;
        },
        128);
}

// 之后是串行部分：压缩可见列表、按 (mesh id, texture id) 排序、填充 InstanceVertex
```

核对到的事实：

- **块大小 128**，不是默认的 32；
- 并行体只写 `m_visibility[i]`（1 B），每个物体做 8 次点变换 + 8 次 `Expand` +
  `Frustum::IntersectsAABB`（6 个平面的 p-vertex 测试，见 `core/Math.h`）；
- **压缩、排序、填充实例**在并行循环之后串行执行：`std::sort` 按 mesh id/texture id
  分组，然后逐实例算 `world.Inverse().Transposed()`（法线矩阵，4×4 求逆，成本不低）；
- `BuildBatches(camera, scene, !GpuCullEnabled())`：**GPU 剔除开启时传 `cull=false`，
  这段 `ParallelFor` 根本不会执行**；本工程唯一的并行剔除路径只在 GPU 剔除关闭或不可用时走到。

另外，并行循环写的是**相邻的 1 B 元素**。块边界处两个线程可能写同一条缓存行
（128 B 的块不保证按 64 B 对齐），这属于**伪共享（false sharing）** 的潜在来源；
这里每个元素只写一次、之后才读，影响通常很小，但它是“并行写数组”要注意的典型问题。

## 7. Amdahl 定律：并行到底能快多少

设程序里可并行部分占比 `p`，并行度 `N`，理论加速比：

```text
S(N) = 1 / ((1 - p) + p / N)
```

`(1-p)` 是串行部分，`p/N` 是并行部分被分摊后的时间。几个代入：

| `p` | `N` | `S(N)` | 说明 |
|---|---|---|---|
| 0.50 | 任意大 | 2.0 | 一半串行，最多 2 倍 |
| 0.80 | 8 | `1/(0.2+0.1) = 3.33` | 10 ms 帧 → 3 ms |
| 0.90 | 8 | `1/(0.1+0.1125) = 4.71` | 串行 10% 仍拖后腿 |
| 0.95 | 16 | `1/(0.05+0.0594) = 9.14` | 接近 16，但永远到不了 |

**数值例子**：一帧 10 ms，其中 8 ms 可并行（`p = 0.8`），2 ms 串行。
8 线程时并行部分变成 `8/8 = 1 ms`，总时间 `2 + 1 = 3 ms`，加速比 `10/3 ≈ 3.33`。
即使把线程加到无穷多，极限也是 `10/2 = 5` 倍。

本工程的主线程还要提交 draw call、切状态、走 UI，这些是串行的；
所以“16 核”不可能等于“16 倍”。统计面板只显示 Benchmark 的串行/并行耗时和比值，
不做“渲染加速比”的宣传。

## 8. 线程数怎么选

- `JobSystem::Init()` 不传参数时，`s_threadCount = std::thread::hardware_concurrency()`；
- **代码里没有 `min(hardware, 16)` 的上限**，16 只是常见机器的观测值；
- `hardware_concurrency()` 返回的是逻辑处理器数（含超线程）。超线程共享执行单元，
  浮点/数学密集任务的收益通常小于 2 倍；
- 主线程也参与 `ParallelFor`，所以“worker 数”不是“额外核数”；
- 线程太多会带来调度、缓存抖动和内存带宽竞争；线程太少则吃不满。
  经验起点是物理核数，再用 Benchmark 和实际帧时间调。

统计面板显示 `JobSystem：%u 线程`，按钮“CPU 基准（100 万次）”会分别跑
`Benchmark(false)` 和 `Benchmark(true)`，显示“串行 x ms / 并行 y ms（z 倍）”。
这个基准的负载是 `sqrt + sin + cos`（`JobSystem.cpp`），块大小 4096，
它测的是**那个合成循环**，不是渲染器的加速比。

## 9. 小例子：1000 个物体走一遍

设 1000 个物体，`chunkSize = 128`：

```text
chunks  = ceil(1000 / 128) = 8
workers = min(8, 16)       = 8        ← 16 核只能用 8 个
每块 128 个物体（最后一块 104 个）
```

时间线（假设每个物体 2 µs，总工作 2 ms）：

| 阶段 | 谁做 | 耗时（估算） |
|---|---|---|
| `m_visibility.assign(1000, 1)` | 主线程 | 很短，但是串行 |
| 8 个块被 8 个 worker 动态领走 | 8 线程 | 约 `2000/8 = 250 µs` |
| 压缩可见列表 + 排序 + 填 144 B 实例 | 主线程 | 串行，未摊薄 |
| 上传实例 + 提交 | 主线程 | 串行 |

如果 `chunkSize = 4096`：`chunks = 1`，`workers = 1`，直接退化成串行；
如果 `chunkSize = 8`：`chunks = 125`，并行度上去了，但原子领块 125 次、
每次块内只有 8 个物体，调度开销占比升高。

如果 6400 个物体、`chunkSize = 128`：`chunks = 50`，`workers = 16`，
平均每个 worker 领 3~4 块，动态领块会让快的线程多领，尾部更短。

## 10. “并行一定更快”是错的

下面这些条件会让并行变慢，或者看起来没变快：

| 条件 | 具体后果 |
|---|---|
| 同步/join 开销 | 每次 `ParallelFor` 创建线程、原子领块、最后 join；工作量小时占比高 |
| 任务粒度太小 | 块内只有几十个元素，调度成本超过计算 |
| 负载不均 | 最慢的块决定结束时间；固定切分尤其明显 |
| 伪共享 | 两个线程写同一条缓存行的不同字节，缓存行来回失效 |
| 内存带宽饱和 | 多核同时读大数组，带宽成为瓶颈，加核不加速 |
| 数据竞争 | 共享累加器不加锁会算错；加锁又会串行化 |
| 串行部分 fixed | Amdahl 上限；压缩/排序/提交往往仍是串行 |

本工程里，`m_visibility` 的并行写入是“每个元素只写一次”，`m_stats` 的累加
放在并行循环之后串行做，避免了竞争；`Log` 有 mutex（`core/Log.h`），
但并行体里不应该打日志——那会把所有线程串行化。

**判断方法**：先测串行时间，再测并行时间，并且测“整体帧时间”而不只是那个循环。
如果循环从 2 ms 降到 0.4 ms，但帧时间只降了 0.3 ms，说明串行部分/同步吃掉了收益。

## 11. 实验（本次未实际运行）

本环境是 Linux，不能运行 Windows exe；以下操作与预期都是**条件性**的，
没有在本机复跑，不能当成实测数据。

```bat
:: 1) 统计面板看线程数与基准
build\bin\ErMao.exe --level cull --play --noeditor --frames 120
```

1. 打开“统计”面板，确认 `JobSystem：N 线程`；点“CPU 基准（100 万次）”，
   记下串行/并行耗时和比值。预期：并行更快，但比值小于线程数（Amdahl +
   线程创建 + 内存带宽）。
2. 加 `--no-gpu-cull` 再跑一次，强制走 `BuildBatches` 的 CPU 剔除并行路径；
   对比开关 GPU 剔除时的帧时间。预期：两者画出的可见实例数应一致，
   帧时间差异取决于 CPU 剔除成本和 GPU 提交成本，**不应期待某个固定倍数**。
3. 把 `BuildBatches` 的 `chunkSize` 从 128 改成 4096，跑 `--level cull`：
   预期块数从 51 掉到 2，并行度被卡在 2，CPU 剔除可能反而变慢。改成 8 则块数
   801、调度开销上升，可能变快或变慢，要看单物体成本和线程数。

排查入口：如果“并行后更慢”，先看块数是否小于线程数，再看并行体里有没有
共享写/日志/锁，最后看是不是整体帧时间被串行部分主导。

## 12. 常见错误

| 错误 | 后果 |
|---|---|
| 多线程写同一数组的相邻元素 | 伪共享，缓存行乒乓 |
| 任务粒度太小 | 线程/调度开销大于收益 |
| 共享状态无锁 | 数据竞争，结果随机错 |
| 假设并行一定更快 | Amdahl + 线程创建 + 内存带宽 |
| 把 `chunkSize` 调大来“减少开销” | 块数变少，`workers=min(chunks, 线程数)` 卡住并行度 |
| 在并行体里打日志/分配内存 | 锁或分配器把线程串行化 |

## 13. 自测题（含答案）

1. **一个结构体 64 B，遍历 1000 万个只读 `pos`（12 B），AoS 与 SoA 各要读多少缓存行？**
   AoS：`1000 万 × 64 B = 640 MB`，约 1000 万条缓存行；SoA：`1000 万 × 12 B = 120 MB`，
   约 187.5 万条。见 3.1/第 4 节。

2. **`p = 0.9`、`N = 8`，加速比上限是多少？**
   `1 / (0.1 + 0.9/8) = 1 / 0.2125 ≈ 4.71`。见第 7 节。

3. **伪共享是什么？怎么缓解？**
   两个线程写同一条缓存行的不同字节，缓存行在核间反复失效。缓解：把热点变量
   按缓存行对齐/填充（padding），或让每个线程写独立的局部数组最后合并。见第 10 节。

4. **为什么 `BuildBatches` 在物体很少时可能比串行慢？**
   块数少、线程创建/join 和原子操作占主导；且 GPU 剔除开启时它根本不做剔除。见第 6/10 节。

## 数学依据与参考资料

> 目的：不要求背下推导，但要知道这一讲**用的是什么数学工具**、去哪里查。

| 用到的原理 | 数学形式 / 名称 | 参考 |
|---|---|---|
| 并行加速上限 | `S(N) = 1 / ((1-p) + p/N)` | [AMDAHL] |
| 动态任务调度 | 原子计数器领块；work stealing 的对比 | [JOB] |
| 缓存行与数据布局 | 64 B 缓存行；AoS/SoA 的有效带宽 | [GAMES104] |
| 视锥剔除的并行化 | 每物体 8 角点 + 6 平面测试 | [CULL] |
