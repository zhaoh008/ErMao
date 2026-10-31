# 第 22 讲：GPU Driven 与间接绘制（设计 + 现状）

> 对应 GAMES104 第 22 讲（GPU Driven / Nanite）。
> 状态：本工程是**CPU 批处理 + 实例化**，没有 GPU 剔除/间接绘制。本文讲清差距与路径。

---

## 1. 现在的管线

```
CPU: 遍历 RenderScene.items → 视锥剔除 → 按 mesh/texture 分批 → 上传实例数据
GPU: 每批一次 DrawIndexedInstanced
```

瓶颈：

- 物体数上万时，CPU 每帧遍历 + 排序 + 上传就是几毫秒；
- draw call 数 = 批次数（材质种类），仍然受 CPU 提交限制。

---

## 2. GPU Driven 的核心

把"谁可见、画什么"交给 GPU：

```
1) 场景数据上传一次（顶点/索引/实例/包围盒）
2) Compute Shader：视锥剔除 + 遮挡剔除（Hi-Z）→ 可见列表
3) 间接绘制：DrawIndexedInstancedIndirect，参数由 GPU 写
```

数学上用第 4 讲的 6 个平面做 `AABB vs Frustum`，只是把循环搬到 GPU：

```hlsl
[numthreads(64,1,1)]
void CullCS(uint3 id : SV_DispatchThreadID) {
    if (id.x >= gInstanceCount) return;
    AABB box = gBounds[id.x];
    if (!InsideFrustum(box)) return;
    uint slot = gVisibleCount.Append(id.x);   // 追加到可见列表
}
```

`AppendStructuredBuffer` + 一个原子计数器，就是最简单的 GPU 剔除。

---

## 3. 间接绘制（D3D11）

```cpp
D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS args;
args.IndexCountPerInstance = ...;
args.InstanceCount = ...;   // 由 GPU 写
args.StartIndexLocation = ...;
args.BaseVertexLocation = ...;
args.StartInstanceLocation = 0;
ctx->DrawIndexedInstancedIndirect(argsBuffer, 0);
```

`argsBuffer` 用 `D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS` 创建，compute 里写入。

---

## 4. 更进一步的 Nanite（虚拟几何）

- 几何用 cluster（簇）+ LOD 层级；
- 每帧按屏幕误差选择 cluster；
- 用 compute 做软件光栅化（小三角形）或 meshlet 硬件路径。

本工程距离这个还有：cluster 构建、LOD 选择、可见性缓冲、软件光栅化——数千行。

---

## 5. 本工程如果要迈出第一步

1. `renderer/GpuCull.h`：`StructuredBuffer<Bounds>` + compute shader `CullCS` + 可见索引 buffer；
2. 用 `Device` 的 compute 支持（D3D11 已有）；
3. `DrawIndexedInstancedIndirect` 替代逐批 `Draw`；
4. 对比统计面板：draw call 数与 CPU 提交时间。

预计 300~400 行，值得做，但优先级低于第 12/17 讲（粒子/行为树）。

---

## 6. 自测题

1. GPU 剔除后，为什么还需要"按材质分批"？bindless 纹理能解决什么？
2. 间接绘制的参数为什么要放在 GPU buffer 里，而不是 CPU 读回再设置？
3. Hi-Z 遮挡剔除的数学基础是什么（深度金字塔）？
