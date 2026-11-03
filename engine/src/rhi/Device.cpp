// ErMao Engine - D3D11 设备实现
#include "rhi/Device.h"
#include "core/Log.h"
#include "platform/Window.h"

#include <windows.h>

namespace erm {

bool Device::Init(Window* window, bool enableDebugLayer) {
    ER_ASSERT(window, "Device::Init 需要有效窗口");
    void* hwnd = window->NativeHandle();
    m_width = window->Width();
    m_height = window->Height();
    if (!CreateDeviceAndSwapChain(hwnd, m_width, m_height, enableDebugLayer)) return false;
    if (!CreateBackbufferViews()) return false;
    LogInfo("D3D11 设备就绪，后备缓冲 %ux%u", m_width, m_height);
    return true;
}

bool Device::CreateDeviceAndSwapChain(void* hwnd, u32 width, u32 height, bool enableDebugLayer) {
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;

    // 降级顺序（审核 P3-2）：硬件+调试层 → 硬件 → WARP。
    // 以前调试层不可用时会直接掉到 WARP（用户只会觉得"引擎好卡"）。
    struct Attempt {
        D3D_DRIVER_TYPE driver;
        UINT            flags;
        const char*     label;
    };
    std::vector<Attempt> attempts;
#ifdef _DEBUG
    if (enableDebugLayer) attempts.push_back({D3D_DRIVER_TYPE_HARDWARE, D3D11_CREATE_DEVICE_DEBUG, "硬件+调试层"});
#else
    (void)enableDebugLayer;
#endif
    attempts.push_back({D3D_DRIVER_TYPE_HARDWARE, 0, "硬件"});
    attempts.push_back({D3D_DRIVER_TYPE_WARP, 0, "WARP 软光栅"});

    HRESULT hr = E_FAIL;
    for (const auto& attempt : attempts) {
        m_device.Reset();
        m_context.Reset();
        hr = D3D11CreateDevice(nullptr, attempt.driver, nullptr, attempt.flags, levels, 2, D3D11_SDK_VERSION,
                               &m_device, &obtained, &m_context);
        if (SUCCEEDED(hr)) {
            LogInfo("D3D11 设备创建成功（%s）", attempt.label);
            break;
        }
        LogWarn("D3D11 创建失败（%s, 0x%08X），尝试降级", attempt.label, static_cast<unsigned>(hr));
    }
    if (FAILED(hr)) {
        LogError("D3D11 设备创建彻底失败 (0x%08X)", static_cast<unsigned>(hr));
        return false;
    }
    LogInfo("D3D11 FeatureLevel = 0x%X", static_cast<unsigned>(obtained));

    // 从设备反查适配器与工厂（多显卡时保证是同一块卡，审核 P3-3）
    ComPtr<IDXGIDevice> dxgiDevice;
    m_device.As(&dxgiDevice);
    ComPtr<IDXGIAdapter> adapter;
    if (dxgiDevice && SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) {
        DXGI_ADAPTER_DESC desc{};
        adapter->GetDesc(&desc);
        char name[256]{};
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, 255, nullptr, nullptr);
        LogInfo("渲染适配器: %s（显存 %llu MB）", name,
                static_cast<unsigned long long>(desc.DedicatedVideoMemory / (1024ull * 1024ull)));
    }

    ComPtr<IDXGIFactory2> factory;
    ComPtr<IDXGIAdapter> adapterForFactory;
    if (dxgiDevice) dxgiDevice->GetAdapter(&adapterForFactory);
    if (adapterForFactory) {
        ComPtr<IDXGIFactory> baseFactory;
        if (SUCCEEDED(adapterForFactory->GetParent(__uuidof(IDXGIFactory), (void**)baseFactory.GetAddressOf()))) {
            baseFactory.As(&factory);
        }
    }
    if (!factory) {
        hr = CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), (void**)factory.GetAddressOf());
        if (FAILED(hr)) {
            LogError("CreateDXGIFactory2 失败 (0x%08X)", static_cast<unsigned>(hr));
            return false;
        }
    }

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width = width;
    scd.Height = height;
    scd.Format = m_format;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.Scaling = DXGI_SCALING_STRETCH;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // 现代 flip model：不再有 DWM 拷贝惩罚
    scd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    hr = factory->CreateSwapChainForHwnd(m_device.Get(), (HWND)hwnd, &scd, nullptr, nullptr, &m_swapChain);
    if (FAILED(hr)) {
        LogError("CreateSwapChainForHwnd 失败 (0x%08X)", (unsigned)hr);
        return false;
    }
    factory->MakeWindowAssociation((HWND)hwnd, DXGI_MWA_NO_ALT_ENTER);
    return true;
}

bool Device::CreateBackbufferViews() {
    ComPtr<ID3D11Texture2D> backbuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backbuffer.GetAddressOf());
    if (FAILED(hr)) { LogError("获取 backbuffer 失败 (0x%08X)", (unsigned)hr); return false; }

    hr = m_device->CreateRenderTargetView(backbuffer.Get(), nullptr, &m_backRTV);
    if (FAILED(hr)) { LogError("创建 backbuffer RTV 失败 (0x%08X)", (unsigned)hr); return false; }

    D3D11_TEXTURE2D_DESC dd{};
    dd.Width = m_width;
    dd.Height = m_height;
    dd.MipLevels = 1;
    dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc.Count = 1;
    dd.Usage = D3D11_USAGE_DEFAULT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    hr = m_device->CreateTexture2D(&dd, nullptr, &m_depthTex);
    if (FAILED(hr)) { LogError("创建深度缓冲失败 (0x%08X)", (unsigned)hr); return false; }

    hr = m_device->CreateDepthStencilView(m_depthTex.Get(), nullptr, &m_depthDSV);
    if (FAILED(hr)) { LogError("创建深度视图失败 (0x%08X)", (unsigned)hr); return false; }

    D3D11_VIEWPORT vp{};
    vp.Width = (float)m_width;
    vp.Height = (float)m_height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);
    return true;
}

void Device::ReleaseBackbufferViews() {
    m_backRTV.Reset();
    m_depthDSV.Reset();
    m_depthTex.Reset();
}

void Device::Resize(u32 width, u32 height) {
    if (!m_swapChain || width == 0 || height == 0) return;
    if (width == m_width && height == m_height) return;

    // ResizeBuffers 前必须解绑并释放所有对 backbuffer 的引用
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_context->ClearState();
    m_context->Flush();
    ReleaseBackbufferViews();

    HRESULT hr = m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        LogError("ResizeBuffers 失败 (0x%08X)，交换链保持旧尺寸，重建后备缓冲视图", (unsigned)hr);
        // ResizeBuffers 失败时交换链仍是旧尺寸；视图已在上面释放，必须按旧尺寸重建，
        // 否则 BeginFrame 会绑定空 RTV/DSV 持续黑屏。重建失败则清尺寸，允许下次 Resize 重试。
        if (!CreateBackbufferViews()) {
            LogError("后备缓冲视图按旧尺寸重建失败，等待下一次窗口尺寸变化重试");
            m_width = 0;
            m_height = 0;
        }
        return;
    }
    m_width = width;
    m_height = height;
    if (!CreateBackbufferViews()) {
        LogError("后备缓冲视图创建失败 %ux%u，等待下一次窗口尺寸变化重试", width, height);
        m_width = 0;
        m_height = 0;
        return;
    }
    LogInfo("交换链尺寸更新为 %ux%u", width, height);
}

void Device::BeginFrame(const float clearColor[4]) {
    ID3D11RenderTargetView* rtv = m_backRTV.Get();
    m_context->OMSetRenderTargets(1, &rtv, m_depthDSV.Get());
    m_context->ClearRenderTargetView(rtv, clearColor);
    m_context->ClearDepthStencilView(m_depthDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

void Device::EndFrame(bool vsync) {
    if (!m_swapChain) return;
    HRESULT hr = m_swapChain->Present(vsync ? 1 : 0, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        LogError("设备丢失 (0x%08X)，引擎需要重启", (unsigned)hr);
    }
}

void Device::Shutdown() {
    if (m_context) { m_context->ClearState(); m_context->Flush(); }
    ReleaseBackbufferViews();
    m_swapChain.Reset();
    m_context.Reset();
    m_device.Reset();
}

} // namespace erm
