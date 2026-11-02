// ErMao Engine - 网格实现
#include "renderer/Mesh.h"
#include "core/Log.h"

#include <atomic>

namespace erm {

namespace {
std::atomic<u32> g_nextMeshId{1};
}

void MeshData::ComputeBounds() {
    if (positions.empty()) {
        bounds = AABB{};
        return;
    }
    bounds.min = positions[0];
    bounds.max = positions[0];
    for (const auto& p : positions) bounds.Expand(p);
}

void MeshData::ComputeNormals() {
    normals.assign(positions.size(), Vec3(0, 0, 0));
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const u32 i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
        if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size()) continue;
        const Vec3 n = Cross(positions[i1] - positions[i0], positions[i2] - positions[i0]);
        normals[i0] += n;
        normals[i1] += n;
        normals[i2] += n;
    }
    for (auto& n : normals) n = Normalize(n);
}

bool Mesh::Create(ID3D11Device* device, const MeshData& data) {
    m_data = data;
    if (m_data.normals.size() != m_data.positions.size()) m_data.ComputeNormals();
    if (m_data.uvs.size() != m_data.positions.size()) m_data.uvs.resize(m_data.positions.size(), Vec2(0, 0));
    m_data.ComputeBounds();

    std::vector<Vertex> vertices(m_data.positions.size());
    for (size_t i = 0; i < vertices.size(); ++i) {
        vertices[i].position = m_data.positions[i];
        vertices[i].normal = m_data.normals[i];
        vertices[i].uv = m_data.uvs[i];
    }

    D3D11_BUFFER_DESC vb{};
    vb.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(Vertex));
    vb.Usage = D3D11_USAGE_IMMUTABLE;
    vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = vertices.data();
    HRESULT hr = device->CreateBuffer(&vb, &init, &m_vertexBuffer);
    if (FAILED(hr)) {
        LogError("顶点缓冲创建失败 (0x%08X)", static_cast<unsigned>(hr));
        return false;
    }

    D3D11_BUFFER_DESC ib{};
    ib.ByteWidth = static_cast<UINT>(m_data.indices.size() * sizeof(u32));
    ib.Usage = D3D11_USAGE_IMMUTABLE;
    ib.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA iinit{};
    iinit.pSysMem = m_data.indices.data();
    hr = device->CreateBuffer(&ib, &iinit, &m_indexBuffer);
    if (FAILED(hr)) {
        LogError("索引缓冲创建失败 (0x%08X)", static_cast<unsigned>(hr));
        return false;
    }

    m_indexCount = static_cast<u32>(m_data.indices.size());
    if (m_id == 0) m_id = g_nextMeshId.fetch_add(1);
    return true;
}

void Mesh::Shutdown() {
    m_indexBuffer.Reset();
    m_vertexBuffer.Reset();
    m_indexCount = 0;
}

void Mesh::BindVertexBuffers(ID3D11DeviceContext* ctx, ID3D11Buffer* instanceBuffer, UINT instanceStride) const {
    ID3D11Buffer* buffers[2] = {m_vertexBuffer.Get(), instanceBuffer};
    UINT strides[2] = {sizeof(Vertex), instanceStride};
    UINT offsets[2] = {0, 0};
    if (instanceBuffer) {
        ctx->IASetVertexBuffers(0, 2, buffers, strides, offsets);
    } else {
        ctx->IASetVertexBuffers(0, 1, buffers, strides, offsets);
    }
    ctx->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
}

void Mesh::Draw(ID3D11DeviceContext* ctx, UINT instanceCount) const {
    ctx->DrawIndexedInstanced(m_indexCount, instanceCount, 0, 0, 0);
}

} // namespace erm
