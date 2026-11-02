// ErMao Engine - 纹理（WIC 解码 PNG/JPG，程序化生成，mipmap）
#pragma once

#include "core/Types.h"
#include "core/Math.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace erm {

using Microsoft::WRL::ComPtr;

class Texture {
public:
    // 从 RGBA8 内存创建；srgb=true 时创建 sRGB 视图（颜色贴图必须为 true）
    bool CreateFromRGBA(ID3D11Device* device, u32 width, u32 height, const u8* rgba, bool srgb = false,
                        bool generateMips = true);
    // WIC 解码 png/jpg/bmp
    bool CreateFromFile(ID3D11Device* device, const std::string& path, bool srgb = true);

    void BindPS(ID3D11DeviceContext* ctx, UINT slot) const;

    ID3D11ShaderResourceView* SRV() const { return m_srv.Get(); }
    u32  Width() const { return m_width; }
    u32  Height() const { return m_height; }
    u32  Id() const { return m_id; } // 稳定排序用
    bool IsValid() const { return m_srv != nullptr; }

    static void BindSampler(ID3D11DeviceContext* ctx, UINT slot, D3D11_FILTER filter = D3D11_FILTER_ANISOTROPIC,
                            D3D11_TEXTURE_ADDRESS_MODE address = D3D11_TEXTURE_ADDRESS_WRAP);

private:
    bool CreateSRV(ID3D11Device* device, ID3D11Texture2D* tex, DXGI_FORMAT srvFormat);

    ComPtr<ID3D11Texture2D>          m_texture;
    ComPtr<ID3D11ShaderResourceView> m_srv;
    u32 m_width = 0;
    u32 m_height = 0;
    u32 m_id = 0;
};

// 程序化纹理生成（保证无美术资源也能跑出有质感的画面）
namespace procedural {
void Checkerboard(std::vector<u8>& rgba, u32 size, u32 cell, const Vec3& c0, const Vec3& c1);
void Noise(std::vector<u8>& rgba, u32 size, u32 seed, const Vec3& base, float contrast);
} // namespace procedural

} // namespace erm
