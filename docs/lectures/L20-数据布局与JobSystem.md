# 第 20 讲：数据布局与 Job System

> 对应 GAMES104 第 20 讲（DOP 与 Job System）。
> 代码：`core/JobSystem.h/.cpp`、`renderer/Renderer.cpp` 的 `BuildBatches`（并行视锥剔除）、
> 统计面板的"CPU 基准（100 万次）"。
> 目标：讲清楚"为什么 AoS 慢、SoA 快""线程开多少""并行到底能快多少"。

---

## 1. 数据布局：AoS vs SoA

```cpp
// AoS（Array of Structures）：一个物体一个结构体
struct Object { Vec3 pos; Vec3 vel; float mass; ... };
std::vector<Object> objects;
```

- 遍历"只读 pos"时，也把 `vel/mass` 拉进缓存行 → 缓存利用率低；
- 适合"每个对象都要全字段"的逻辑。

```cpp
// SoA（Structure of Arrays）：字段各自成数组
struct Objects { std::vector<Vec3> pos; std::vector<Vec3> vel; std::vector<float> mass; };
```

- 只读 `pos` 时缓存行全是 `pos` → 带宽利用率高；
- SIMD 友好（一条指令处理 4/8 个 float）；
- 适合"批量、同构"的计算（粒子、剔除、蒙皮）。

本工程的实例数据是 AoS（`InstanceVertex` 144B），因为 GPU 输入布局本来就要交错；
但**剔除**是 SoA 思路：`m_visibility` 是一个独立的 `u8` 数组，只遍历它。

---

## 2. 并行分块

`JobSystem::ParallelFor(begin, end, fn, chunkSize)`：

```
把 [begin, end) 按 chunkSize 切成 N 块
起 N-1 个线程 + 主线程各领一块
全部完成后返回
```

- `chunkSize` 太小 → 线程创建/同步开销占比高；
- 太大 → 负载不均（有的线程早干完）。

经验：每块至少几万个元素，或者用"动态领任务"（work stealing）。

本工程 `BuildBatches` 用 `chunkSize=128`：128 个物体的 AABB 变换 + 平面测试，
在 16 线程下每块约 8 个任务，负载还算均匀（物体数少时并行反而更慢，见下）。

---

## 3. Amdahl 定律

设程序里可并行部分占比 `p`，并行度 `N`，理论加速比：

```
S(N) = 1 / ((1-p) + p/N)
```

- `p = 0.95, N = 16` → `S ≈ 9.1`（不是 16）；
- `p = 0.5` → 无论多少核，最多 2 倍。

本工程的渲染主线程是串行的（提交 draw call、状态切换），
所以 16 核不可能带来 16 倍；统计面板不做"并行加速比"的虚假宣传。

---

## 4. 线程数怎么选

- 硬件线程数（`std::thread::hardware_concurrency()`）；
- 本工程留一个核给主线程/系统：`min(hardware, 16)`；
- 超线程对浮点密集任务收益有限（共享 FPU），所以 8 核 16 线程不一定比 8 线程快。

---

## 5. 实验

```bat
build\bin\ErMao.exe --frames 60
```

1. 统计面板底部点"CPU 基准（100 万次）"，对比单线程与多线程耗时；
2. 把 `BuildBatches` 的 `chunkSize` 从 128 改到 4096，跑大场景（`--level arena`），
   观察帧时间变化；
3. 日志里 `JobSystem 就绪：并行度 16（硬件线程 16）`。

---

## 6. 常见错误

| 错误 | 后果 |
|---|---|
| 多线程写同一个数组的相邻元素 | 伪共享（false sharing），缓存行乒乓 |
| 任务粒度太小 | 线程开销大于收益 |
| 共享状态无锁 | 数据竞争（本工程 `Log` 有 mutex，其余只读） |
| 假设并行一定更快 | Amdahl + 线程创建成本 |

---

## 7. 自测题

1. 一个结构体 64 字节，遍历 100 万个只读 `pos`（12 字节），AoS 与 SoA 各要读多少缓存行？
2. `p = 0.9`，`N = 8`，加速比上限是多少？
3. 伪共享是什么？怎么用 padding 解决？
4. 为什么本工程的 `BuildBatches` 在物体很少时可能比串行慢？
