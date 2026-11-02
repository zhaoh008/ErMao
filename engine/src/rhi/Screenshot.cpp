// ErMao Engine - 截图实现
#include "rhi/Screenshot.h"
#include "core/FileSystem.h"
#include "core/Log.h"

#include <wincodec.h>
#include <wrl/client.h>
#include <windows.h>

#pragma comment(lib, "windowscodecs.lib")

namespace erm {

using Microsoft::WRL::ComPtr;

bool SaveBackbufferToPng(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain1* swapChain,
                         const std::string& path) {
    if (!device || !context || !swapChain) return false;

    ComPtr<ID3D11Texture2D> backbuffer;
    if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backbuffer.GetAddressOf()))) {
        LogError("截图失败：无法获取后备缓冲");
        return false;
    }

    D3D11_TEXTURE2D_DESC desc{};
    backbuffer->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC staging = desc;
    staging.Usage = D3D11_USAGE_STAGING;
    staging.BindFlags = 0;
    staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> stagingTex;
    if (FAILED(device->CreateTexture2D(&staging, nullptr, &stagingTex))) {
        LogError("截图失败：创建 staging 纹理失败");
        return false;
    }
    context->CopyResource(stagingTex.Get(), backbuffer.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        LogError("截图失败：Map 失败");
        return false;
    }

    const UINT width = desc.Width;
    const UINT height = desc.Height;
    std::vector<u8> rgba(static_cast<size_t>(width) * height * 4);
    for (UINT y = 0; y < height; ++y) {
        std::memcpy(rgba.data() + static_cast<size_t>(y) * width * 4,
                    static_cast<const u8*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch, width * 4);
    }
    context->Unmap(stagingTex.Get(), 0);

    // ------------------------------------------------ WIC 编码
    ComPtr<IWICImagingFactory> factory;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory)))) {
        LogError("截图失败：WIC 工厂创建失败");
        return false;
    }

    const std::wstring wpath = FileSystem::Utf8ToWide(path);
    ComPtr<IWICStream> stream;
    factory->CreateStream(&stream);
    if (FAILED(stream->InitializeFromFilename(wpath.c_str(), GENERIC_WRITE))) {
        LogError("截图失败：无法写入 %s", path.c_str());
        return false;
    }

    ComPtr<IWICBitmapEncoder> encoder;
    factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);

    ComPtr<IWICBitmapFrameEncode> frame;
    encoder->CreateNewFrame(&frame, nullptr);
    frame->Initialize(nullptr);
    frame->SetSize(width, height);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
    frame->SetPixelFormat(&format);

    // PNG 编码器经常把格式协商成 BGRA；此时必须交换 R/B，否则截图的颜色会整体偏色。
    if (IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) {
        for (size_t i = 0; i + 3 < rgba.size(); i += 4) std::swap(rgba[i], rgba[i + 2]);
    } else if (!IsEqualGUID(format, GUID_WICPixelFormat32bppRGBA)) {
        LogWarn("截图：WIC 返回了非预期像素格式，颜色可能不正确");
    }

    if (FAILED(frame->WritePixels(height, width * 4, static_cast<UINT>(rgba.size()), rgba.data()))) {
        LogError("截图失败：WritePixels 失败");
        return false;
    }
    frame->Commit();
    encoder->Commit();

    LogInfo("截图已保存: %s (%ux%u)", path.c_str(), width, height);
    return true;
}

} // namespace erm
