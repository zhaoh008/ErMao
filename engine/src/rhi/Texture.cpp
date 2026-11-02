// ErMao Engine - 纹理实现
#include "rhi/Texture.h"
#include "core/FileSystem.h"
#include "core/Log.h"
#include "core/Math.h"

#include <wincodec.h>
#include <windows.h>
#include <atomic>

#pragma comment(lib, "windowscodecs.lib")

namespace erm {

namespace {
ComPtr<IWICImagingFactory> GetWICFactory() {
    static ComPtr<IWICImagingFactory> factory;
    if (!factory) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    }
    return factory;
}
} // namespace

bool Texture::CreateFromRGBA(ID3D11Device* device, u32 width, u32 height, const u8* rgba, bool srgb,
                             bool generateMips) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = generateMips ? 0 : 1; // 0 = 完整 mip 链（GenerateMips 需要）
    desc.ArraySize = 1;
    desc.Format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (generateMips ? D3D11_BIND_RENDER_TARGET : 0);
    desc.MiscFlags = generateMips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = rgba;
    init.SysMemPitch = width * 4;

    // 生成 mipmap 的标准流程：
    //   1) MipLevels=0 创建完整 mip 链（不能再直接传 pInitialData，见 D3D11 子资源规则）
    //   2) UpdateSubresource 写第 0 级
    //   3) 建 SRV（全部 mip）后 GenerateMips
    if (generateMips) {
        ID3D11DeviceContext* ctx = nullptr;
        device->GetImmediateContext(&ctx);
        HRESULT hr = device->CreateTexture2D(&desc, nullptr, &m_texture);
        if (FAILED(hr)) { LogError("创建纹理失败 (0x%08X)", (unsigned)hr); ctx->Release(); return false; }
        ctx->UpdateSubresource(m_texture.Get(), 0, nullptr, rgba, width * 4, 0);

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = (UINT)-1;
        hr = device->CreateShaderResourceView(m_texture.Get(), &srvDesc, &m_srv);
        if (FAILED(hr)) { LogError("创建纹理 SRV 失败 (0x%08X)", (unsigned)hr); ctx->Release(); return false; }
        ctx->GenerateMips(m_srv.Get());
        ctx->Release();
    } else {
        HRESULT hr = device->CreateTexture2D(&desc, &init, &m_texture);
        if (FAILED(hr)) { LogError("创建纹理失败 (0x%08X)", (unsigned)hr); return false; }
        if (!CreateSRV(device, m_texture.Get(), desc.Format)) return false;
    }

    m_width = width;
    m_height = height;
    static std::atomic<u32> s_nextTextureId{1};
    if (m_id == 0) m_id = s_nextTextureId.fetch_add(1);
    return true;
}

bool Texture::CreateSRV(ID3D11Device* device, ID3D11Texture2D* tex, DXGI_FORMAT srvFormat) {
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = srvFormat;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = -1;
    const HRESULT hr = device->CreateShaderResourceView(tex, &srvDesc, &m_srv);
    if (FAILED(hr)) { LogError("创建纹理 SRV 失败 (0x%08X)", (unsigned)hr); return false; }
    return true;
}

bool Texture::CreateFromFile(ID3D11Device* device, const std::string& path, bool srgb) {
    auto factory = GetWICFactory();
    if (!factory) { LogError("WIC 工厂创建失败"); return false; }

    const std::wstring wpath = FileSystem::Utf8ToWide(path);
    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = factory->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ,
                                                    WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) { LogError("WIC 无法解码: %s", path.c_str()); return false; }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) return false;

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) return false;
    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) { LogError("WIC 格式转换失败: %s", path.c_str()); return false; }

    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    std::vector<u8> pixels(static_cast<size_t>(w) * h * 4);
    hr = converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data());
    if (FAILED(hr)) { LogError("WIC 读取像素失败: %s", path.c_str()); return false; }

    LogInfo("加载纹理 %s (%ux%u)", path.c_str(), w, h);
    return CreateFromRGBA(device, w, h, pixels.data(), srgb, true);
}

void Texture::BindPS(ID3D11DeviceContext* ctx, UINT slot) const {
    ID3D11ShaderResourceView* srv = m_srv.Get();
    ctx->PSSetShaderResources(slot, 1, &srv);
}

void Texture::BindSampler(ID3D11DeviceContext* ctx, UINT slot, D3D11_FILTER filter,
                          D3D11_TEXTURE_ADDRESS_MODE address) {
    // 简单的采样器缓存：参数作为 key
    struct Key {
        D3D11_FILTER filter;
        D3D11_TEXTURE_ADDRESS_MODE address;
        bool operator<(const Key& o) const {
            return filter != o.filter ? filter < o.filter : address < o.address;
        }
    };
    static std::map<Key, ComPtr<ID3D11SamplerState>> cache;
    static ID3D11Device* cachedDevice = nullptr;

    ID3D11Device* device = nullptr;
    ctx->GetDevice(&device);
    if (device != cachedDevice) { cache.clear(); cachedDevice = device; }

    Key key{filter, address};
    auto it = cache.find(key);
    if (it == cache.end()) {
        D3D11_SAMPLER_DESC desc{};
        desc.Filter = filter;
        desc.AddressU = address;
        desc.AddressV = address;
        desc.AddressW = address;
        desc.MipLODBias = 0.0f;
        desc.MaxAnisotropy = 8;
        desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        desc.MinLOD = 0.0f;
        desc.MaxLOD = D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;
        if (FAILED(device->CreateSamplerState(&desc, &sampler))) {
            device->Release();
            return;
        }
        it = cache.emplace(key, sampler).first;
    }
    ctx->PSSetSamplers(slot, 1, it->second.GetAddressOf());
    device->Release();
}

// ------------------------------------------------------------------ 程序化纹理
namespace procedural {

void Checkerboard(std::vector<u8>& rgba, u32 size, u32 cell, const Vec3& c0, const Vec3& c1) {
    rgba.resize(static_cast<size_t>(size) * size * 4);
    for (u32 y = 0; y < size; ++y) {
        for (u32 x = 0; x < size; ++x) {
            const bool odd = ((x / cell) + (y / cell)) & 1;
            const Vec3 c = odd ? c1 : c0;
            const size_t i = (static_cast<size_t>(y) * size + x) * 4;
            rgba[i + 0] = (u8)(Saturate(c.x) * 255.0f);
            rgba[i + 1] = (u8)(Saturate(c.y) * 255.0f);
            rgba[i + 2] = (u8)(Saturate(c.z) * 255.0f);
            rgba[i + 3] = 255;
        }
    }
}

void Noise(std::vector<u8>& rgba, u32 size, u32 seed, const Vec3& base, float contrast) {
    rgba.resize(static_cast<size_t>(size) * size * 4);
    u32 state = seed * 1664525u + 1013904223u;
    auto rnd = [&]() {
        state = state * 1664525u + 1013904223u;
        return ((state >> 8) & 0xFFFF) / 65535.0f;
    };
    for (u32 y = 0; y < size; ++y) {
        for (u32 x = 0; x < size; ++x) {
            const float n = (rnd() - 0.5f) * contrast;
            const size_t i = (static_cast<size_t>(y) * size + x) * 4;
            rgba[i + 0] = (u8)(Saturate(base.x + n) * 255.0f);
            rgba[i + 1] = (u8)(Saturate(base.y + n) * 255.0f);
            rgba[i + 2] = (u8)(Saturate(base.z + n) * 255.0f);
            rgba[i + 3] = 255;
        }
    }
}

} // namespace procedural

} // namespace erm
