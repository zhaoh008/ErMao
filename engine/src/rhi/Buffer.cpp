// ErMao Engine - GPU 缓冲实现
#include "rhi/Buffer.h"
#include "core/Log.h"

namespace erm {

bool GpuBuffer::Create(ID3D11Device* device, const void* data, size_t size, D3D11_BIND_FLAG bind,
                       D3D11_USAGE usage, UINT miscFlags, UINT stride) {
    m_stride = stride;
    m_size = size;
    m_usage = usage;

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = (UINT)size;
    desc.Usage = usage;
    desc.BindFlags = bind;
    desc.MiscFlags = miscFlags;
    if (usage == D3D11_USAGE_DYNAMIC) desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = data;
    HRESULT hr = device->CreateBuffer(&desc, data ? &init : nullptr, &m_buffer);
    if (FAILED(hr)) {
        LogError("GpuBuffer::Create 失败 size=%zu bind=%u (0x%08X)", size, (unsigned)bind, (unsigned)hr);
        return false;
    }
    return true;
}

void GpuBuffer::Update(ID3D11DeviceContext* ctx, const void* data, size_t size) {
    if (!m_buffer || !data) return;
    if (m_usage == D3D11_USAGE_DYNAMIC) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(ctx->Map(m_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, data, std::min(size, m_size));
            ctx->Unmap(m_buffer.Get(), 0);
        }
    } else {
        ctx->UpdateSubresource(m_buffer.Get(), 0, nullptr, data, 0, 0);
    }
}

} // namespace erm
