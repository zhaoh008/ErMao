# 第 26 讲：Vulkan 后端（阶段一：设备、交换链、管线）【进阶·已实现】

> 目标不是「把引擎翻译成 Vulkan」，而是先把**最小闭环**跑通：
> instance → device → swapchain → render pass → pipeline → command buffer → present → 截图。
> 代码：`rhi/VulkanBackend.h/.cpp`、`shaders/vk/*.hlsl`（dxc 编译成 SPIR-V）。
> 运行：`build\bin\ErMao.exe --vulkan-test`

![Vulkan 三角形](../screenshots/vulkan.png)

---

## 0. 为什么是「阶段一」

| 阶段 | 内容 | 状态 |
|---|---|---|
| **一** | VkInstance / 物理设备 / 逻辑设备 / Win32 surface / swapchain / render pass / graphics pipeline / 命令缓冲 / 同步 / present / 回读截图 | ✅ 本讲 |
| 二 | 把 `Renderer` 的 sky + opaque pass 抽到 `rhi::IDevice`，D3D11/Vulkan 双实现 | 计划 |
| 三 | 阴影贴图、蒙皮、粒子、后处理、ImGui 后端 | 计划 |
| 四 | 描述符/bindless、管线缓存、多帧并行 | 计划 |

工业引擎不会「重写一遍」，而是先有 RHI 抽象，再补后端。
本工程目前是 **D3D11 主路径 + Vulkan 最小闭环**，两条路都能在这台机器上跑起来。

---

## 1. D3D11 与 Vulkan 的心智模型差异

| 概念 | D3D11 | Vulkan |
|---|---|---|
| 设备 | `ID3D11Device` 自动管理 | 手动选物理设备、建逻辑设备、队列 |
| 资源状态 | 驱动隐式跟踪 | **显式** layout / barrier |
| 命令 | 立即上下文 `*Set*` | `VkCommandBuffer` 录制后提交 |
| 同步 | 隐式 | 信号量 + fence |
| 着色器 | HLSL 运行时编译 | SPIR-V 预编译 |
| 内存 | 驱动分配 | 自己 `vkAllocateMemory` + 绑定 |

一句话：**Vulkan 把 D3D11 驱动做的事交给你做**，换来的是可控与多线程。

---

## 2. 初始化顺序（代码里的 6 步）

```
vkCreateInstance
  → vkCreateWin32SurfaceKHR(HWND)
  → 枚举物理设备，挑「图形 + 呈现」队列族
  → vkCreateDevice + vkGetDeviceQueue
  → vkCreateSwapchainKHR（格式/尺寸/呈现模式）
  → render pass + image view + framebuffer + pipeline + 命令池
```

### 2.1 交换链尺寸

```cpp
extent = caps.currentExtent;
if (extent.width == 0xFFFFFFFF) {           // 由窗口决定
    extent.width  = clamp(width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    extent.height = clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
}
```

### 2.2 格式

优先用 surface 报告的第一个格式（本机是 `VK_FORMAT_B8G8R8A8_UNORM`）。
截图时直接按 BGRA 写 PNG，避免通道顺序错。

---

## 3. 渲染通道与管线

### 3.1 Render pass

一个颜色附件：

```
loadOp  = CLEAR
storeOp = STORE
initialLayout = UNDEFINED
finalLayout   = PRESENT_SRC_KHR
```

子通道依赖把 `COLOR_ATTACHMENT_OUTPUT` 的写操作排到呈现之前。

### 3.2 三角形管线

| 状态 | 设置 |
|---|---|
| 顶点输入 | 无（用 `SV_VertexID` 生成三个顶点） |
| 拓扑 | `TRIANGLE_LIST` |
| 光栅化 | 填充、不剔除 |
| 混合 | 关闭 |
| 视口 | 全屏 |

顶点着色器（`shaders/vk/triangle.vert.hlsl`）：

```hlsl
VSOutput VSMain(uint vertexId : SV_VertexID) {
    float2 positions[3] = { float2(0,0.55), float2(0.5,-0.45), float2(-0.5,-0.45) };
    float4 colors[3]    = { float4(0.95,0.65,0.15,1), float4(0.25,0.65,0.95,1), float4(0.55,0.85,0.35,1) };
    ...
}
```

`[[vk::location(0)]]` 指定 varying 位置，dxc 的 `-spirv` 输出直接嵌进 `VulkanTriangleSpirv.h`。

编译命令：

```bat
dxc -T vs_6_0 -E VSMain -spirv triangle.vert.hlsl -Fo triangle.vert.spv
dxc -T ps_6_0 -E PSMain -spirv triangle.frag.hlsl -Fo triangle.frag.spv
python tools\gen_vk_spirv.py
```

---

## 4. 一帧的流程

```
vkWaitForFences → vkResetFences
vkAcquireNextImageKHR（signal imageAvailable）
开始命令缓冲 → beginRenderPass(clear) → bindPipeline → vkCmdDraw(3)
→ endRenderPass → 若截图：barrier + vkCmdCopyImageToBuffer + barrier
→ vkEndCommandBuffer
vkQueueSubmit（等 imageAvailable，signal renderFinished）
vkQueuePresentKHR
```

同步对象：

- `imageAvailable`：交换链图可用；
- `renderFinished`：渲染完可呈现；
- `inFlight` fence：CPU 不等 GPU 空转。

---

## 5. 回读截图

```
image (COLOR_ATTACHMENT / PRESENT)
  → barrier: PRESENT_SRC → TRANSFER_SRC
  → vkCmdCopyImageToBuffer(staging, HOST_VISIBLE)
  → barrier: TRANSFER_SRC → PRESENT_SRC
  → map + WIC 写 PNG
```

这就是「无人工干预验证画面」的手段：`--vulkan-test` 跑 120 帧后自动存
`docs/screenshots/vulkan.png`。

---

## 6. 踩过的坑

| 现象 | 原因 | 修法 |
|---|---|---|
| `vulkan_win32.h` 里 `HWND` 未定义 | 没先包含 `<windows.h>` | 先 `#define VK_USE_PLATFORM_WIN32_KHR` 再包含 |
| 链接 `vulkan-1.lib` 失败 | 只给 ErMaoEngine 加了 Lib 目录 | 可执行目标也要 `target_link_directories` |
| 退出时 `vkDestroyShaderModule: Invalid device` | 析构重复调用 Shutdown | 让 `Shutdown()` 幂等，`~Impl` 只调一次 |
| 画面全黑 | 没提交 / 没 present | 检查 submit 的 wait stage 与 semaphore |

---

## 7. 实验

```bat
:: 跑 120 帧并截图
build\bin\ErMao.exe --vulkan-test

:: 看日志里的设备与格式
type ErMao.log | findstr Vulkan
```

---

## 8. 练习

1. 把 `vkCmdDraw` 的顶点数从 3 改成 6：**预期**仍是三角形（多余顶点被忽略/重画）。
2. 把 `frontFace` 改成 `VK_FRONT_FACE_COUNTER_CLOCKWISE` 并开启背面剔除：**预期**三角形消失。
3. 把 `presentMode` 改成 `VK_PRESENT_MODE_IMMEDIATE_KHR`：**预期**帧率更高但可能撕裂。
4. 去掉 `vkQueueWaitIdle`：**预期**截图偶发花屏（读到未完成的图像）。

---

## 9. 迁移路线（下一阶段）

1. **RHI 接口**：`rhi/IDevice.h` 定义 `CreateTexture/CreateBuffer/BeginFrame/Draw`；
2. **Sky + Opaque**：先迁最简单的两个 pass，验证 PBR 着色器 SPIR-V 化；
3. **Shadow**：需要 depth image + sampler comparison；
4. **ImGui**：`imgui_impl_vulkan` + descriptor pool；
5. **描述符管理**：先 per-pass 更新，后续做 bindless。

> 结论：Vulkan 不再只是「计划」，而是这台机器上**能跑出画面**的第二条后端；
> 但离「全部 pass 迁移完」还有明确的工作量，这一讲把它如实记下来。

## 数学依据与参考资料

> 目的：不要求背下推导，但要知道这一讲**用的是什么数学工具**、去哪里查。

| 用到的原理 | 数学形式 / 名称 | 参考 |
|---|---|---|
| 交换链与呈现 | surface caps / present mode / extent | [VK] |
| 显式同步 | semaphore + fence，barrier 管理 layout | [VK] |
| 渲染通道 | loadOp/storeOp/layout 转换 | [VK] |
| SPIR-V | dxc -spirv 生成的中间表示 | [VK] |
