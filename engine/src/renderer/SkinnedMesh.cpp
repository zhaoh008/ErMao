// ErMao Engine - 蒙皮网格实现
#include "renderer/SkinnedMesh.h"

#include "core/Log.h"

#include <atomic>

namespace erm {

namespace {
std::atomic<u32> g_nextSkinnedMeshId{1};
}

bool SkinnedMesh::Create(ID3D11Device* device, const SkinnedMeshData& data) {
    Shutdown();
    m_data = data;
    m_data.ComputeBounds();
    if (m_data.vertices.empty() || m_data.indices.empty()) {
        LogError("SkinnedMesh::Create: 空数据");
        return false;
    }

    D3D11_BUFFER_DESC vbDesc{};
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.ByteWidth = static_cast<UINT>(m_data.vertices.size() * sizeof(SkinVertex));
    D3D11_SUBRESOURCE_DATA vbData{};
    vbData.pSysMem = m_data.vertices.data();
    if (FAILED(device->CreateBuffer(&vbDesc, &vbData, &m_vertexBuffer))) {
        LogError("SkinnedMesh 顶点缓冲创建失败");
        return false;
    }

    D3D11_BUFFER_DESC ibDesc{};
    ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    ibDesc.ByteWidth = static_cast<UINT>(m_data.indices.size() * sizeof(u32));
    D3D11_SUBRESOURCE_DATA ibData{};
    ibData.pSysMem = m_data.indices.data();
    if (FAILED(device->CreateBuffer(&ibDesc, &ibData, &m_indexBuffer))) {
        LogError("SkinnedMesh 索引缓冲创建失败");
        return false;
    }

    m_indexCount = static_cast<u32>(m_data.indices.size());
    m_id = g_nextSkinnedMeshId.fetch_add(1);
    return true;
}

void SkinnedMesh::Shutdown() {
    m_vertexBuffer.Reset();
    m_indexBuffer.Reset();
    m_indexCount = 0;
}

void SkinnedMesh::Bind(ID3D11DeviceContext* ctx) const {
    if (!IsValid()) return;
    UINT        stride = sizeof(SkinVertex);
    UINT        offset = 0;
    ID3D11Buffer* vb = m_vertexBuffer.Get();
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    ctx->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
}

void SkinnedMesh::Draw(ID3D11DeviceContext* ctx) const {
    if (!IsValid()) return;
    ctx->DrawIndexed(m_indexCount, 0, 0);
}

} // namespace erm
