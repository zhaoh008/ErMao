// ErMao Engine - D3D11 设备与交换链（GAMES104 第 4 讲：渲染管线入口）
#pragma once

#include "core/Types.h"

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace erm {

using Microsoft::WRL::ComPtr;

class Window;

// 对 D3D11 的最薄封装：
//   * 创建 device / context / flip-model swapchain
//   * 管理 backbuffer 与深度缓冲，处理 resize
//   * 其余资源（buffer/texture/shader/state）由 rhi 下的各个类自管理
class Device {
public:
    bool Init(Window* window, bool enableDebugLayer = true);
    void Shutdown();

    void BeginFrame(const float clearColor[4]);
    void EndFrame(bool vsync);

    // 窗口尺寸变化后调用（内部 WaitIdle → 重建 backbuffer）
    void Resize(u32 width, u32 height);

    ID3D11Device*        GetDevice() const { return m_device.Get(); }
    ID3D11DeviceContext* GetContext() const { return m_context.Get(); }
    IDXGISwapChain1*     GetSwapChain() const { return m_swapChain.Get(); }
    ID3D11RenderTargetView* GetBackbufferRTV() const { return m_backRTV.Get(); }
    ID3D11DepthStencilView* GetDepthDSV() const { return m_depthDSV.Get(); }

    u32  GetWidth() const { return m_width; }
    u32  GetHeight() const { return m_height; }
    bool IsValid() const { return m_device != nullptr; }

private:
    bool CreateDeviceAndSwapChain(void* hwnd, u32 width, u32 height, bool enableDebugLayer);
    bool CreateBackbufferViews();
    void ReleaseBackbufferViews();

    ComPtr<ID3D11Device>           m_device;
    ComPtr<ID3D11DeviceContext>    m_context;
    ComPtr<IDXGISwapChain1>        m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_backRTV;
    ComPtr<ID3D11Texture2D>        m_depthTex;
    ComPtr<ID3D11DepthStencilView> m_depthDSV;

    u32 m_width = 0;
    u32 m_height = 0;
    DXGI_FORMAT m_format = DXGI_FORMAT_R8G8B8A8_UNORM;
};

} // namespace erm
