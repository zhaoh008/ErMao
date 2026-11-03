// ErMao Engine - 渲染目标与状态实现
#include "rhi/RenderTarget.h"
#include "core/Log.h"

namespace erm {

bool RenderTarget::Create(ID3D11Device* device, u32 width, u32 height, DXGI_FORMAT colorFormat, bool withDepth,
                          bool colorHasSrv, bool colorSrgb) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = colorFormat;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | (colorHasSrv ? D3D11_BIND_SHADER_RESOURCE : 0);
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &m_colorTex);
    if (FAILED(hr)) { LogError("RenderTarget 颜色纹理创建失败 (0x%08X)", (unsigned)hr); Shutdown(); return false; }

    hr = device->CreateRenderTargetView(m_colorTex.Get(), nullptr, &m_rtv);
    if (FAILED(hr)) { LogError("RenderTarget RTV 创建失败 (0x%08X)", (unsigned)hr); Shutdown(); return false; }

    if (colorHasSrv) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        // sRGB 视图：8 位色缓冲才需要（HDR 不需要）
        DXGI_FORMAT srvFormat = colorFormat;
        if (colorSrgb && colorFormat == DXGI_FORMAT_R8G8B8A8_UNORM) srvFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        srvDesc.Format = srvFormat;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;
        hr = device->CreateShaderResourceView(m_colorTex.Get(), &srvDesc, &m_colorSrv);
        if (FAILED(hr)) { LogError("RenderTarget SRV 创建失败 (0x%08X)", (unsigned)hr); Shutdown(); return false; }
    }
    (void)colorSrgb;

    if (withDepth) {
        D3D11_TEXTURE2D_DESC dd{};
        dd.Width = width;
        dd.Height = height;
        dd.MipLevels = 1;
        dd.ArraySize = 1;
        dd.Format = DXGI_FORMAT_R32_TYPELESS;
        dd.SampleDesc.Count = 1;
        dd.Usage = D3D11_USAGE_DEFAULT;
        dd.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        hr = device->CreateTexture2D(&dd, nullptr, &m_depthTex);
        if (FAILED(hr)) { LogError("RenderTarget 深度纹理创建失败 (0x%08X)", (unsigned)hr); Shutdown(); return false; }

        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        hr = device->CreateDepthStencilView(m_depthTex.Get(), &dsvDesc, &m_dsv);
        if (FAILED(hr)) { LogError("RenderTarget DSV 创建失败 (0x%08X)", (unsigned)hr); Shutdown(); return false; }

        D3D11_SHADER_RESOURCE_VIEW_DESC dsrv{};
        dsrv.Format = DXGI_FORMAT_R32_FLOAT;
        dsrv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        dsrv.Texture2D.MostDetailedMip = 0;
        dsrv.Texture2D.MipLevels = 1;
        hr = device->CreateShaderResourceView(m_depthTex.Get(), &dsrv, &m_depthSrv);
        if (FAILED(hr)) { LogError("RenderTarget 深度 SRV 创建失败 (0x%08X)", (unsigned)hr); Shutdown(); return false; }
    }

    // 只有全部资源创建成功后才发布尺寸：任一失败时 Shutdown() 已把尺寸清零，
    // EnsureSceneTarget/EnsureViewportTarget 会重新尝试，而不是把空视图当成有效目标。
    m_width = width;
    m_height = height;
    return true;
}

void RenderTarget::Shutdown() {
    m_width = 0;
    m_height = 0;
    m_colorSrv.Reset();
    m_rtv.Reset();
    m_colorTex.Reset();
    m_depthSrv.Reset();
    m_dsv.Reset();
    m_depthTex.Reset();
}

void RenderTarget::Bind(ID3D11DeviceContext* ctx) {
    ID3D11RenderTargetView* rtv = m_rtv.Get();
    ctx->OMSetRenderTargets(1, &rtv, m_dsv.Get());
}

void RenderTarget::BindDepthOnly(ID3D11DeviceContext* ctx) {
    ctx->OMSetRenderTargets(0, nullptr, m_dsv.Get());
}

void RenderTarget::Clear(ID3D11DeviceContext* ctx, const float color[4], float depth) {
    if (m_rtv) ctx->ClearRenderTargetView(m_rtv.Get(), color);
    if (m_dsv) ctx->ClearDepthStencilView(m_dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, depth, 0);
}

// ------------------------------------------------------------------ DepthTarget
bool DepthTarget::Create(ID3D11Device* device, u32 width, u32 height) {
    m_size = width;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32_TYPELESS; // 无类型格式，DSV 用 D32，SRV 用 R32
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &m_tex);
    if (FAILED(hr)) { LogError("阴影贴图创建失败 (0x%08X)", (unsigned)hr); return false; }

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    hr = device->CreateDepthStencilView(m_tex.Get(), &dsvDesc, &m_dsv);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    hr = device->CreateShaderResourceView(m_tex.Get(), &srvDesc, &m_srv);
    return SUCCEEDED(hr);
}

void DepthTarget::Shutdown() {
    m_srv.Reset();
    m_dsv.Reset();
    m_tex.Reset();
}

void DepthTarget::BindAndClear(ID3D11DeviceContext* ctx) {
    ctx->OMSetRenderTargets(0, nullptr, m_dsv.Get());
    ctx->ClearDepthStencilView(m_dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
}

void DepthTarget::BindAsTexture(ID3D11DeviceContext* ctx, UINT slot) {
    ID3D11ShaderResourceView* srv = m_srv.Get();
    ctx->PSSetShaderResources(slot, 1, &srv);
}

// ------------------------------------------------------------------ RenderStates
ComPtr<ID3D11DepthStencilState> RenderStates::s_depthOpaque;
ComPtr<ID3D11DepthStencilState> RenderStates::s_depthTestOnly;
ComPtr<ID3D11DepthStencilState> RenderStates::s_depthNone;
ComPtr<ID3D11BlendState>        RenderStates::s_blendOpaque;
ComPtr<ID3D11BlendState>        RenderStates::s_blendAlpha;
ComPtr<ID3D11BlendState>        RenderStates::s_blendAdditive;
ComPtr<ID3D11RasterizerState>   RenderStates::s_rasterSolid;
ComPtr<ID3D11RasterizerState>   RenderStates::s_rasterWire;
ComPtr<ID3D11RasterizerState>   RenderStates::s_rasterNoCull;
ComPtr<ID3D11RasterizerState>   RenderStates::s_rasterNoCullWire;
bool                            RenderStates::s_wireframe = false;

void RenderStates::Init(ID3D11Device* device) {
    D3D11_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_LESS;
    device->CreateDepthStencilState(&ds, &s_depthOpaque);

    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    device->CreateDepthStencilState(&ds, &s_depthTestOnly);

    ds.DepthEnable = FALSE;
    device->CreateDepthStencilState(&ds, &s_depthNone);

    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = FALSE;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&bd, &s_blendOpaque);

    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    device->CreateBlendState(&bd, &s_blendAlpha);

    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    device->CreateBlendState(&bd, &s_blendAdditive);

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_BACK;
    rd.FrontCounterClockwise = TRUE; // 右手系 + 外侧逆时针绕序
    rd.DepthClipEnable = TRUE;
    device->CreateRasterizerState(&rd, &s_rasterSolid);

    rd.FillMode = D3D11_FILL_WIREFRAME;
    device->CreateRasterizerState(&rd, &s_rasterWire);

    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    device->CreateRasterizerState(&rd, &s_rasterNoCull);

    rd.FillMode = D3D11_FILL_WIREFRAME;
    device->CreateRasterizerState(&rd, &s_rasterNoCullWire);
}

void RenderStates::Shutdown() {
    s_depthOpaque.Reset();
    s_depthTestOnly.Reset();
    s_depthNone.Reset();
    s_blendOpaque.Reset();
    s_blendAlpha.Reset();
    s_rasterSolid.Reset();
    s_rasterWire.Reset();
    s_rasterNoCull.Reset();
    s_rasterNoCullWire.Reset();
}

void RenderStates::SetOpaque(ID3D11DeviceContext* ctx) {
    ctx->OMSetDepthStencilState(s_depthOpaque.Get(), 0);
    const float blend[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(s_blendOpaque.Get(), blend, 0xFFFFFFFF);
}

void RenderStates::SetBlendOpaque(ID3D11DeviceContext* ctx) {
    const float blend[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(s_blendOpaque.Get(), blend, 0xFFFFFFFF);
}

void RenderStates::SetDepthTestNoWrite(ID3D11DeviceContext* ctx) {
    ctx->OMSetDepthStencilState(s_depthTestOnly.Get(), 0);
}

void RenderStates::SetDepthNone(ID3D11DeviceContext* ctx) {
    ctx->OMSetDepthStencilState(s_depthNone.Get(), 0);
}

void RenderStates::SetAlphaBlend(ID3D11DeviceContext* ctx) {
    const float blend[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(s_blendAlpha.Get(), blend, 0xFFFFFFFF);
}

void RenderStates::SetAdditiveBlend(ID3D11DeviceContext* ctx) {
    const float blend[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(s_blendAdditive.Get(), blend, 0xFFFFFFFF);
}

void RenderStates::SetWireframe(ID3D11DeviceContext* ctx, bool enabled) {
    s_wireframe = enabled;
    ctx->RSSetState(enabled ? s_rasterWire.Get() : s_rasterSolid.Get());
}

void RenderStates::SetCullBack(ID3D11DeviceContext* ctx, bool cull) {
    ctx->RSSetState(cull ? s_rasterSolid.Get() : s_rasterNoCull.Get());
}

void RenderStates::SetCullNone(ID3D11DeviceContext* ctx) {
    ctx->RSSetState(s_wireframe ? s_rasterNoCullWire.Get() : s_rasterNoCull.Get());
}

void RenderStates::SetFrontFaceCW(ID3D11DeviceContext* ctx) {
    (void)ctx; // 模型数据统一按顺时针正面导出，无需切换
}

} // namespace erm
