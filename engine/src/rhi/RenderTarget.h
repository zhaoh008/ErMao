// ErMao Engine - 渲染目标（HDR 场景缓冲、阴影深度图）
#pragma once

#include "core/Types.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace erm {

using Microsoft::WRL::ComPtr;

// 颜色 + 可选深度的离屏目标
class RenderTarget {
public:
    bool Create(ID3D11Device* device, u32 width, u32 height, DXGI_FORMAT colorFormat, bool withDepth,
                bool colorHasSrv = true, bool colorSrgb = false);
    void Shutdown();

    void Bind(ID3D11DeviceContext* ctx);
    void Clear(ID3D11DeviceContext* ctx, const float color[4], float depth = 1.0f);

    // 只绑定深度（阴影 Pass 用）
    void BindDepthOnly(ID3D11DeviceContext* ctx);

    ID3D11RenderTargetView*   RTV() const { return m_rtv.Get(); }
    ID3D11ShaderResourceView* ColorSRV() const { return m_colorSrv.Get(); }
    ID3D11ShaderResourceView* DepthSRV() const { return m_depthSrv.Get(); }
    u32 Width() const { return m_width; }
    u32 Height() const { return m_height; }

private:
    ComPtr<ID3D11Texture2D>          m_colorTex;
    ComPtr<ID3D11RenderTargetView>   m_rtv;
    ComPtr<ID3D11ShaderResourceView> m_colorSrv;
    ComPtr<ID3D11Texture2D>          m_depthTex;
    ComPtr<ID3D11DepthStencilView>   m_dsv;
    ComPtr<ID3D11ShaderResourceView> m_depthSrv;
    u32 m_width = 0;
    u32 m_height = 0;
};

// 深度-only 目标：阴影贴图
class DepthTarget {
public:
    bool Create(ID3D11Device* device, u32 width, u32 height);
    void Shutdown();

    void BindAndClear(ID3D11DeviceContext* ctx);
    void BindAsTexture(ID3D11DeviceContext* ctx, UINT slot);

    ID3D11ShaderResourceView* SRV() const { return m_srv.Get(); }
    u32 Size() const { return m_size; }

private:
    ComPtr<ID3D11Texture2D>          m_tex;
    ComPtr<ID3D11DepthStencilView>   m_dsv;
    ComPtr<ID3D11ShaderResourceView> m_srv;
    u32 m_size = 0;
};

// 常用管线状态（延迟创建 + 缓存）
class RenderStates {
public:
    static void Init(ID3D11Device* device);
    static void Shutdown();

    static void SetOpaque(ID3D11DeviceContext* ctx);
    static void SetBlendOpaque(ID3D11DeviceContext* ctx);
    static void SetDepthTestNoWrite(ID3D11DeviceContext* ctx);
    static void SetDepthNone(ID3D11DeviceContext* ctx);
    static void SetAlphaBlend(ID3D11DeviceContext* ctx);
    static void SetWireframe(ID3D11DeviceContext* ctx, bool enabled);
    static void SetCullBack(ID3D11DeviceContext* ctx, bool cull);
    static void SetCullNone(ID3D11DeviceContext* ctx);
    static void SetFrontFaceCW(ID3D11DeviceContext* ctx);

private:
    static ComPtr<ID3D11DepthStencilState> s_depthOpaque;
    static ComPtr<ID3D11DepthStencilState> s_depthTestOnly;
    static ComPtr<ID3D11DepthStencilState> s_depthNone;
    static ComPtr<ID3D11BlendState>        s_blendOpaque;
    static ComPtr<ID3D11BlendState>        s_blendAlpha;
    static ComPtr<ID3D11RasterizerState>   s_rasterSolid;
    static ComPtr<ID3D11RasterizerState>   s_rasterWire;
    static ComPtr<ID3D11RasterizerState>   s_rasterNoCull;
    static ComPtr<ID3D11RasterizerState>   s_rasterNoCullWire;
    static bool s_wireframe;
};

} // namespace erm
