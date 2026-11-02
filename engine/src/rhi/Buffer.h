// ErMao Engine - GPU 缓冲与常量缓冲
#pragma once

#include "core/Types.h"
#include "core/Math.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace erm {

using Microsoft::WRL::ComPtr;

// 通用 buffer 封装（顶点/索引/实例/常量）
class GpuBuffer {
public:
    bool Create(ID3D11Device* device, const void* data, size_t size, D3D11_BIND_FLAG bind,
                D3D11_USAGE usage = D3D11_USAGE_IMMUTABLE, UINT miscFlags = 0, UINT stride = 0);
    void Update(ID3D11DeviceContext* ctx, const void* data, size_t size);

    ID3D11Buffer* Get() const { return m_buffer.Get(); }
    UINT          Stride() const { return m_stride; }
    size_t        Size() const { return m_size; }

private:
    ComPtr<ID3D11Buffer> m_buffer;
    UINT                 m_stride = 0;
    size_t               m_size = 0;
    D3D11_USAGE          m_usage = D3D11_USAGE_IMMUTABLE;
};

// 常量缓冲：模板参数必须是 16 字节对齐的 POD
template <typename T>
class ConstantBuffer {
public:
    bool Create(ID3D11Device* device, D3D11_USAGE usage = D3D11_USAGE_DYNAMIC) {
        static_assert(sizeof(T) % 16 == 0, "常量缓冲大小必须是 16 的倍数");
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(T);
        desc.Usage = usage;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = (usage == D3D11_USAGE_DYNAMIC) ? D3D11_CPU_ACCESS_WRITE : 0;
        HRESULT hr = device->CreateBuffer(&desc, nullptr, &m_buffer);
        return SUCCEEDED(hr);
    }

    void Update(ID3D11DeviceContext* ctx, const T& data) {
        if (!m_buffer) return;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(ctx->Map(m_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &data, sizeof(T));
            ctx->Unmap(m_buffer.Get(), 0);
        }
    }

    void BindVS(ID3D11DeviceContext* ctx, UINT slot) const {
        ID3D11Buffer* b = m_buffer.Get();
        ctx->VSSetConstantBuffers(slot, 1, &b);
    }
    void BindPS(ID3D11DeviceContext* ctx, UINT slot) const {
        ID3D11Buffer* b = m_buffer.Get();
        ctx->PSSetConstantBuffers(slot, 1, &b);
    }
    void BindVSPS(ID3D11DeviceContext* ctx, UINT slot) const {
        BindVS(ctx, slot);
        BindPS(ctx, slot);
    }
    ID3D11Buffer* Get() const { return m_buffer.Get(); }

private:
    ComPtr<ID3D11Buffer> m_buffer;
};

} // namespace erm
