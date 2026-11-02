# 第 26 讲：Vulkan 后端（阶段一：设备、交换链、管线）【进阶·已实现】

> 目标不是“把引擎翻译成 Vulkan”，而是先把**最小闭环**跑通：
> instance → device → swapchain → render pass → pipeline → command buffer → present → 截图。
> 代码：`engine/src/rhi/VulkanBackend.h/.cpp`、`engine/shaders/vk/*.hlsl`
> （dxc 编译成 SPIR-V，再由 `tools/gen_vk_spirv.py` 生成 `VulkanTriangleSpirv.h`）。
> 运行：`build\bin\ErMao.exe --vulkan-test`
> 阶段一状态：**已实现**三角形 + 截图；描述符、多帧并行、Resize、纹理**未做**。

![Vulkan 三角形](../../screenshots/vulkan.png)

## 0. 阶段一到底做了什么

| 阶段 | 内容 | 状态 |
|---|---|---|
| **一** | `VkInstance` / 物理设备 / 逻辑设备 / Win32 surface / swapchain / render pass / graphics pipeline / 命令缓冲 / 信号量 + 栅栏 / present / 回读截图 | **本讲，已实现** |
| 二 | 把 `Renderer` 的 sky + opaque pass 抽到 `rhi::IDevice`，D3D11/Vulkan 双实现 | 计划 |
| 三 | 阴影贴图、蒙皮、粒子、后处理、ImGui 后端 | 计划 |
| 四 | 描述符/bindless、管线缓存、多帧并行、Resize | 计划 |

本工程目前是 **D3D11 主路径 + Vulkan 最小闭环**。Vulkan 路径只画一个三角形，不参与场景
渲染；它的价值是证明“这台机器上能建实例/设备/交换链/管线，能提交、能 present、能回读
截图”，为后续迁移排除环境风险。

## 1. 现象：已经有 D3D11，为什么还要一个 Vulkan 后端

用 D3D11 画一个三角形，心智模型是“设备管理一切”：

```cpp
// 概念伪代码：D3D11 的写法（不是本工程 Vulkan 代码）
device->CreateTexture2D(...);          // 驱动分配显存
context->OMSetRenderTargets(...);      // 绑定
context->Draw(3, 0);                   // 立即上下文提交
// 资源状态/内存/同步由驱动在背后处理
```

D3D11 的 `ID3D11DeviceContext` 是“立即上下文”：你调用 `*Set*`/`Draw`，命令就进入队列；
资源从“渲染目标”切到“着色器资源”，驱动会隐式插入屏障；显存由驱动分配，交换链由 DXGI
管理。写起来省心，代价是**控制权在驱动手里**。

当需求变成下面这些时，“设备管理一切”就不够了：

1. **多平台**：Windows 的 D3D11 到 Linux/Steam Deck（Vulkan）、macOS（Metal）、
   主机（各自 API）都要重写。需要一层显式 RHI 把平台差异关在下面。
2. **显式控制内存与布局**：什么资源放显存、什么时候上传、什么时候做布局转换，
   驱动不再替你猜；延迟渲染、GPU Driven、bindless 需要这种控制。
3. **多线程录制**：D3D11 的立即上下文本质上是单线程的；Vulkan 允许每线程各自的
   命令缓冲并行录制，主线程只提交。
4. **可预测的帧节奏**：显式信号量/栅栏让“CPU 最多领先 GPU 几帧”成为你自己的选择。

代价是：**D3D11 驱动替你做的事，现在要你自己做**。这不是“API 更底层所以更好”，
而是“用更多代码换可控性”。

## 2. 对象职责：每个东西负责什么

Vulkan 的对象很多，但可以按“谁管谁”分组。先记住一句话：
**instance 管加载器，device 管 GPU，queue 管提交，swapchain 管呈现，command buffer 管记录，
semaphore/fence 管顺序。**

### 2.1 实例、表面、物理设备、逻辑设备、队列

| 对象 | 一句话职责 | 为什么需要 / 阶段一怎么做 |
|---|---|---|
| `VkInstance` | 应用程序与 Vulkan 加载器/驱动的连接 | 所有调用的入口；阶段一只开 surface + win32 surface 两个扩展，API 1.1 |
| `VkSurfaceKHR` | 把窗口系统（HWND）抽象成可呈现的表面 | 交换链要知道“画到哪里”；由 `vkCreateWin32SurfaceKHR` 创建 |
| `VkPhysicalDevice` | 一块真实 GPU 及其能力/队列族/内存类型 | 创建逻辑设备前先查询；阶段一挑**第一个**同时支持图形与呈现的队列族 |
| `VkDevice` | 逻辑设备，你实际用来创建资源/管线 | 从物理设备创建，启用 `VK_KHR_swapchain`；阶段一只建一个队列 |
| `VkQueue` | 提交命令缓冲、等待/发信号的通道 | 从队列族取一个队列；图形、呈现共用同一个族 |

阶段一的设备选择很朴素：遍历物理设备 → 遍历队列族 → 找同时有
`VK_QUEUE_GRAPHICS_BIT` 且 `vkGetPhysicalDeviceSurfaceSupportKHR` 为真的族；找不到就失败。
工业引擎会打分（独显/显存/扩展），也会处理“图形族和呈现族分开”的情况；阶段一没做。

### 2.2 交换链、图像、视图、帧缓冲

| 对象 | 职责 | 阶段一细节 |
|---|---|---|
| `VkSwapchainKHR` | 管理一组用于呈现的图像 | `minImageCount = max(2, caps.minImageCount)`；FIFO；`COLOR_ATTACHMENT | TRANSFER_SRC` |
| `VkImage` | 交换链里的每一张图 | 由交换链创建，数量由驱动决定，代码用 `vkGetSwapchainImagesKHR` 取回 |
| `VkImageView` | 描述“怎么看待这张图”（格式/范围） | 每张图一个，作为 framebuffer 的附件 |
| `VkFramebuffer` | 把一组 view 绑定到某个 render pass | 每张交换链图一个 framebuffer |

交换链的尺寸和格式是阶段一必须处理的现实问题：

```cpp
// 源码节选：VulkanBackend.cpp
extent = caps.currentExtent;
if (extent.width == 0xFFFFFFFFu) {   // surface 不指定尺寸，由窗口决定
    extent.width  = clamp(width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    extent.height = clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
}
swapFormat = formats.empty() ? VK_FORMAT_B8G8R8A8_UNORM : formats[0].format;
```

- `currentExtent` 不是 `0xFFFFFFFF` 时必须用它；格式取 surface 报告的**第一个**，
  没有遍历挑 sRGB；颜色空间也取第一个；
- `minImageCount` 取 `max(2, caps.minImageCount)`，没有检查 `maxImageCount`；
  呈现模式写死 `VK_PRESENT_MODE_FIFO_KHR`（垂直同步，保证一定支持）；
- 没有处理 `VK_ERROR_OUT_OF_DATE_KHR`/`VK_SUBOPTIMAL_KHR`，也没有 Resize 后重建。

### 2.3 渲染通道、着色器、管线

**Render pass** 描述“一次渲染用哪些附件、每个附件 load/store 怎么做、布局怎么变”：

```text
// 阶段一的 render pass（源码事实）
颜色附件：format = swapFormat, samples = 1
loadOp = CLEAR；storeOp = STORE
initialLayout = UNDEFINED；finalLayout = PRESENT_SRC_KHR
依赖：src/dstStage = COLOR_ATTACHMENT_OUTPUT，dstAccess = COLOR_ATTACHMENT_WRITE
```

`initialLayout = UNDEFINED` 表示“不关心上一帧内容，直接清空”；`finalLayout =
PRESENT_SRC_KHR` 表示“渲染结束后这张图可以直接呈现”。这两个布局省掉了两次显式 barrier。

**管线**把固定功能状态“烤”成一个不可变对象。阶段一的三角形管线：

| 状态 | 设置 |
|---|---|
| 顶点输入 | 无（顶点着色器用 `SV_VertexID` 生成三个顶点） |
| 拓扑 | `TRIANGLE_LIST` |
| 光栅化 | 填充、`CULL_MODE_NONE`、`FRONT_FACE_CLOCKWISE` |
| 混合 | 关闭，颜色写掩码 `0xF` |
| 视口/裁剪、深度 | 视口固定整个 extent；无深度/模板 |
| 描述符 | **无**（pipeline layout 为空） |

顶点/片元着色器（`shaders/vk/triangle.vert.hlsl` / `triangle.frag.hlsl`）：

```hlsl
// 源码节选：triangle.vert.hlsl
VSOutput VSMain(uint vertexId : SV_VertexID) {
    float2 positions[3] = { float2(0.0,0.55), float2(0.5,-0.45), float2(-0.5,-0.45) };
    float4 colors[3] = { float4(0.95,0.65,0.15,1), float4(0.25,0.65,0.95,1),
                         float4(0.55,0.85,0.35,1) };
    VSOutput output;
    output.position = float4(positions[vertexId], 0.0, 1.0);
    output.color = colors[vertexId];
    return output;
}
```

`[[vk::location(0)]]` 指定 varying 位置。用 dxc 编译：

```bat
dxc -T vs_6_0 -E VSMain -spirv triangle.vert.hlsl -Fo triangle.vert.spv
dxc -T ps_6_0 -E PSMain -spirv triangle.frag.hlsl -Fo triangle.frag.spv
python tools\gen_vk_spirv.py     :: 把 .spv 转成 VulkanTriangleSpirv.h 的 uint32_t 数组
```

**管线布局为空**意味着阶段一没有 uniform buffer、没有纹理、没有描述符集。
这是阶段一“没做”的部分，不是“Vulkan 不需要”。

### 2.4 命令池、命令缓冲、信号量、栅栏

| 对象 | 职责 | 阶段一细节 |
|---|---|---|
| `VkCommandPool` | 分配命令缓冲的内存池 | 绑定到队列族；带 `RESET_COMMAND_BUFFER_BIT` |
| `VkCommandBuffer` | 记录“要 GPU 做什么” | 只有一个，每帧 `vkResetCommandBuffer` 后重录 |
| `VkSemaphore` | GPU-GPU 顺序（队列内/队列间） | `imageAvailable`（图可用）、`renderFinished`（可呈现） |
| `VkFence` | GPU→CPU 顺序（CPU 等 GPU） | `inFlight`，创建时置为 signaled，CPU 每帧等它 |
| `VkImageMemoryBarrier` | 图像布局/访问权限转换 | 截图时 PRESENT→TRANSFER_SRC→PRESENT |

**为什么需要两种同步？**

- **信号量**解决“GPU 的下一步要等 GPU 的下一步”：`vkAcquireNextImageKHR` 拿到图后，
  渲染必须等 `imageAvailable`；渲染完必须让 present 等 `renderFinished`。
- **栅栏**解决“CPU 要等 GPU”：CPU 不能在上一帧命令还没执行完时重置命令缓冲/覆盖
  staging 内存，所以 `vkWaitForFences(inFlight)`。栅栏 CPU 可见，信号量不可见。

**布局（layout）**是 Vulkan 特有的“图像当前怎么被使用”。同一张图从“颜色附件”变成
“拷贝源”必须用 barrier 声明；阶段一在截图时显式做了两次转换，渲染到 present 的转换
由 render pass 的 `finalLayout` 代劳。

## 3. 一帧的流程与同步点

```text
// 概念伪代码：阶段一每帧（VulkanBackend.cpp 的 Run 循环）
vkWaitForFences(inFlight); vkResetFences(inFlight)
vkAcquireNextImageKHR(imageAvailable, &imageIndex)
vkResetCommandBuffer(commandBuffer); vkBeginCommandBuffer()
  vkCmdBeginRenderPass(clear → PRESENT_SRC)
    vkCmdBindPipeline(pipeline); vkCmdDraw(3, 1, 0, 0)
  vkCmdEndRenderPass()
  若最后一帧且要截图：barrier → vkCmdCopyImageToBuffer(staging) → barrier
vkEndCommandBuffer()
vkQueueSubmit(等 imageAvailable, 发 renderFinished, 发 inFlight fence)
vkQueuePresentKHR(等 renderFinished)
vkQueueWaitIdle(queue)               // 阶段一：每帧都等 GPU 完全空闲
```

最后一行是关键：**阶段一实际上没有多帧并行**。`vkQueueWaitIdle` 让 CPU 每帧都等 GPU
完全空闲，所以 `inFlight` 栅栏功能上多余；它存在是为了以后去掉 `vkQueueWaitIdle` 时，
CPU 最多领先一帧；当前它并没有让 CPU 不等 GPU 空转。

阶段一的“一帧”因此是：

```text
CPU 录制 → 提交 → 等 GPU 画完 → present → 等队列空闲 → 下一帧
```

这不是性能优化的写法（GPU 和 CPU 没有重叠），但它是**最容易验证正确性**的写法。
多帧并行（每帧独立的命令缓冲/信号量/栅栏）是下一阶段的事。

## 4. 回读截图：阶段一的“自动验证”

`--vulkan-test` 让 `Engine::Init` 在创建窗口后直接进入 `VulkanDemo`：

```cpp
// 源码节选：runtime/Engine.cpp
VulkanDemo demo;
if (demo.Init(m_window)) {
    demo.Run(120, "文档/screenshots/vulkan.png");
} else {
    LogError("[Vulkan] 初始化失败");
}
demo.Shutdown();
m_window.Destroy();
std::exit(0);          // 不再初始化 D3D11 设备
```

`Run` 只在**最后一帧**（`frame + 1 == frames`）做截图：`PRESENT_SRC → TRANSFER_SRC`
barrier → `vkCmdCopyImageToBuffer(staging)` → `TRANSFER_SRC → PRESENT_SRC` barrier →
循环结束后 `vkMapMemory` → WIC 写 PNG。

几个事实：

- staging 缓冲在循环前创建，大小 `width × height × 4`；格式假设 4 B/像素；
- `vkMapMemory` 在循环之后、且每帧都 `vkQueueWaitIdle`，读到的是完成后的图；
- `SaveBgraPng` 用 WIC 按 `GUID_WICPixelFormat32bppBGRA` 写文件。**代码没有检查
  `swapFormat` 是否真是 BGRA**：本机常见的是 `B8G8R8A8_UNORM`；若第一个格式是 RGBA，
  截图会红蓝互换。这是阶段一的已知简化；
- 只有最后一帧被拷贝，固定跑 120 帧（`--frames` 无效）；截图是相对路径，须从仓库根启动。

## 5. 阶段一没做的事（下一阶段）

| 没做 | 后果 / 下一阶段 |
|---|---|
| 描述符集、uniform buffer、纹理/采样器 | 三角形没有材质/相机矩阵；阶段二做 sky/opaque 时必须补 |
| 多帧并行（每帧独立命令缓冲/信号量/栅栏） | 每帧 `vkQueueWaitIdle`，CPU/GPU 不重叠 |
| Swapchain Resize / `OUT_OF_DATE` 处理 | 窗口尺寸变化后 acquire 失败直接返回 false |
| 深度缓冲、阴影、MSAA | 只能画一个无深度三角形 |
| 校验层 / debug messenger | 错误可能静默；阶段一没开 `VK_LAYER_KHRONOS_validation` |
| 显式内存分配器 | 只有截图 staging 用了 `vkAllocateMemory`，其余由驱动/交换链管理 |
| 图形/呈现分离队列族 | 只接受“同一个族同时支持图形+呈现”的设备 |
| sRGB / 色彩空间选择 | 取 surface 第一个格式和颜色空间，没做 gamma 校正 |

这些不是“Vulkan 不需要”，而是阶段一刻意收窄范围：先跑通最小闭环，再按阶段补，避免陷入描述符/内存/多帧同步的组合爆炸。

## 6. 小例子：1600×900 的截图要多大

窗口 1600×900，交换链格式 4 B/像素：

```text
单张图 / staging = 1600 × 900 × 4 B = 5,760,000 B ≈ 5.49 MiB（只分配一块）
120 帧 = 只拷贝最后 1 帧，回读量就是 5.49 MiB，不是 120 倍
```

`extent` 可能不是 1600×900：如果 `caps.currentExtent` 给了固定值，代码用它；否则用窗口
尺寸 clamp 到 `minImageExtent/maxImageExtent`，所以截图分辨率以日志里的 `(%ux%u)` 为准。
交换链图像数量由驱动决定（常见 2~3 张），`minImageCount = max(2, ...)` 只保证至少 2 张；
若驱动给 3 张，`images/views/framebuffers` 各有 3 个，每帧按 `imageIndex` 选 framebuffer。

## 7. 踩过的坑（源码/构建里能对上）

| 现象 | 原因 | 修法 |
|---|---|---|
| `vulkan_win32.h` 里 `HWND` 未定义 | 包含顺序不对 | 先 `#define VK_USE_PLATFORM_WIN32_KHR`，再 `<windows.h>`，再 vulkan 头 |
| 链接 `vulkan-1.lib` 失败 | 只给 `ErMaoEngine` 加了库目录 | 可执行目标 `ErMao` 也要 `target_link_directories`（CMake 里两处都加了） |
| 退出时 `vkDestroyShaderModule: Invalid device` | 析构重复调用 `Shutdown` | `Shutdown()` 幂等（`if (!device && !instance) return;`），`~Impl` 只调一次 |
| 画面全黑 | 没提交 / 没 present / 等错 stage | 检查 `vkQueueSubmit` 的 wait stage 和信号量 |
| 截图红蓝互换 | 代码假设交换链格式是 BGRA | 阶段一未检查；换格式时需要按 `swapFormat` 选择 WIC 像素格式 |
| `--vulkan-test` 直接报“不可用” | 构建时没找到 `VULKAN_SDK` | 设置环境变量 `VULKAN_SDK`（CMake 检查 SDK 的 include 目录下是否存在 `vulkan.h`） |

`Shutdown` 的顺序也值得看：先 `vkDeviceWaitIdle`，再销毁 shader module、pipeline、
pipeline layout、render pass、command pool、信号量、栅栏、framebuffer、view、swapchain，
最后 device；surface/instance 在 device 之后销毁。顺序错了会报 “Destroy…: Invalid device”。

## 8. 实验（本次未实际运行）

本环境是 Linux，不能运行 Windows exe，所以下面只列操作与预期，**没有实测日志**：

```bat
:: 从仓库根目录运行，保证截图相对路径可写
build\bin\ErMao.exe --vulkan-test
type ErMao.log | findstr Vulkan
```

预期日志（具体设备名/格式因机器而异）：

```text
[Vulkan] 物理设备: <你的 GPU 名>
[Vulkan] 逻辑设备与队列就绪
[Vulkan] 截图已保存: 文档/screenshots/vulkan.png (1600x900)
[Vulkan] 渲染 120 帧完成（1600x900）
```

排查顺序：没有 `物理设备` 行 → 驱动/`VULKAN_SDK`；acquire 失败 → 窗口/交换链尺寸或
`OUT_OF_DATE`；画面全黑 → submit 的 wait stage、信号量、`loadOp/finalLayout`；
截图颜色不对 → `swapFormat` 与 WIC 格式不匹配。

## 9. 练习（含预期）

1. 把 `vkCmdDraw(commandBuffer, 3, ...)` 改成 6：**预期**仍只看到一个三角形。
   顶点着色器按 `vertexId` 取数组，`vertexId >= 3` 是**越界读取**（行为未定义），
   不是“多余顶点被忽略”；正确做法是同时改着色器或加保护。
2. 把 `frontFace` 改成 `VK_FRONT_FACE_COUNTER_CLOCKWISE` 并开启背面剔除：
   **预期**三角形消失（当前顶点在屏幕空间是顺时针）。
3. 把 `presentMode` 改成 `VK_PRESENT_MODE_IMMEDIATE_KHR`：**预期**可能撕裂；但代码没有
   查询 `vkGetPhysicalDeviceSurfacePresentModesKHR`，某些平台不支持 IMMEDIATE，创建会失败。
4. 去掉每帧 `vkQueueWaitIdle`：**预期**CPU 会领先，必须同时改成每帧独立的命令缓冲/
   信号量/栅栏，否则重置命令缓冲或复用信号量会出错——这是引入竞态，不是优化。

## 10. 自测题（含答案）

1. **D3D11 和 Vulkan 最核心的心智模型差异是什么？**
   D3D11 由驱动隐式管理资源状态、内存和同步；Vulkan 把这些显式交给你：
   布局转换、barrier、信号量/栅栏、内存分配都要自己写。

2. **信号量和栅栏分别解决什么问题？**
   信号量做 GPU-GPU 顺序（如“渲染等图可用”“present 等渲染完”）；
   栅栏做 GPU→CPU 顺序（CPU 等 GPU 完成后再重置命令缓冲/读 staging）。

3. **为什么阶段一每帧 `vkQueueWaitIdle`？**
   为了正确性和易调试：CPU 每帧等 GPU 完全空闲，避免多帧并行的同步复杂度。
   代价是没有 CPU/GPU 重叠；下一阶段做多帧并行。

## 11. 迁移路线（下一阶段，均为计划）

1. **RHI 接口**：在 `rhi/` 下抽象 `CreateTexture/CreateBuffer/BeginFrame/Draw`，
   D3D11 与 Vulkan 各实现一份；先迁 sky + opaque 两个 pass。
2. **描述符与常量**：per-frame uniform buffer、纹理/采样器、descriptor set/pool；
   先 per-pass 更新，后续再做 bindless。
3. **深度与阴影**：depth image + comparison sampler，迁 `ShadowPass`。
4. **多帧并行**：每帧独立命令缓冲、信号量、栅栏；去掉 `vkQueueWaitIdle`；
   处理 swapchain Resize / `OUT_OF_DATE` / `SUBOPTIMAL`。
5. **ImGui 后端**：`imgui_impl_vulkan` + descriptor pool。
6. **内存与管线缓存**：显式分配器、`VkPipelineCache`、管线布局复用。

## 数学依据与参考资料

> 目的：不要求背下推导，但要知道这一讲**用的是什么数学工具**、去哪里查。

| 用到的原理 | 数学形式 / 名称 | 参考 |
|---|---|---|
| 交换链尺寸选择 | `clamp(window, minImageExtent, maxImageExtent)`；`currentExtent` 特例 | [VK] |
| 交换链图像数量 | `minImageCount = max(2, caps.minImageCount)` | [VK] |
| 显式同步 | semaphore（GPU-GPU）+ fence（GPU-CPU）+ barrier（layout/access） | [VK] |
| 渲染通道 | `loadOp/storeOp/initialLayout/finalLayout` 与子通道依赖 | [VK] |
| 回读体积 | `width × height × 4 B`（BGRA8） | [VK] |
| SPIR-V | dxc `-spirv` 生成的中间表示 | [VK] |
