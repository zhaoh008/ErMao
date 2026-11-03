// ErMao Engine - Vulkan 后端（阶段一：设备 / 交换链 / 管线 / 三角形 / 截图）
//
// 这是从 D3D11 迁移到 Vulkan 的第一步：证明这台机器上能创建 instance、device、
// swapchain，编译 SPIR-V、建 graphics pipeline、提交命令并 present。
// 场景的 shadow/opaque/sky/particles pass 仍然走 D3D11 路径，后续按阶段迁移。
#pragma once

#include "core/Types.h"

#include <string>

namespace erm {

class Window;

class VulkanDemo {
public:
    // 当前构建是否真正启用了 Vulkan 后端（无 SDK 的构建返回 false）
    static bool IsAvailable();
    bool Init(Window& window);
    // 渲染 frames 帧；screenshotPath 非空时把最后一帧存成 PNG
    bool Run(u32 frames, const std::string& screenshotPath);
    void Shutdown();

    bool Ready() const { return m_device != nullptr; }

private:
    struct Impl;
    Impl* m_impl = nullptr;
    void* m_device = nullptr; // 仅用于 Ready() 的轻量标记
};

} // namespace erm
