# P8 · 进阶：GPU Driven 剔除与 Vulkan 阶段一

> CPU 只上传实例，剔除交给 compute；再把管线搬到 Vulkan。

## 细纲

### 1. 视锥剔除
- 6 平面与 p-vertex
- 为什么先测 p 点

### 2. GPU 间接绘制
- 压缩列表 + 间接参数
- D3D11 的 UAV/VB 限制

### 3. CPU/GPU 对拍
- 两边可见数一致才算对
- 常见矩阵约定错误

### 4. Vulkan 阶段一
- instance/device/swapchain
- SPIR-V 与管线
- 截图回读

## 素材

- 配图：gpu-cull.png, vulkan.png
- 代码：shaders/Cull.hlsl, Renderer::GpuCulledDraw, rhi/VulkanBackend.cpp
- 实验：`ErMao.exe --level cull / --vulkan-test`
- 数学参考：RTR4 §19.2, Wihlidal 2016, Vulkan 1.3 spec

## 自检问题

1. p-vertex 为什么能保守剔除？
2. 间接绘制的参数从哪来？
3. Vulkan 的 barrier 在解决什么？

## 状态

- [ ] 初稿
- [ ] 自测（能答 4/5）
- [ ] 冷却 48h 复读
- [ ] 读者测试
- [ ] 发布
