// ErMao Engine - 蒙皮网格的 GPU 缓冲（GAMES104 第 8 讲）
#pragma once

#include "core/Math.h"
#include "core/Types.h"
#include "renderer/SkinnedMeshData.h"

#include <d3d11.h>
#include <wrl/client.h>

namespace erm {

using Microsoft::WRL::ComPtr;

// 顶点布局与 shaders/Skinned.hlsl 严格对应：
//   POSITION(0) NORMAL(12) TEXCOORD0(24) BLENDINDICES(32, uint4) BLENDWEIGHT(48, float4)
class SkinnedMesh {
public:
    SkinnedMesh() = default;
    ~SkinnedMesh() { Shutdown(); }
    SkinnedMesh(const SkinnedMesh&) = delete;
    SkinnedMesh& operator=(const SkinnedMesh&) = delete;

    bool Create(ID3D11Device* device, const SkinnedMeshData& data);
    void Shutdown();

    void Bind(ID3D11DeviceContext* ctx) const;
    void Draw(ID3D11DeviceContext* ctx) const;

    bool        IsValid() const { return m_vertexBuffer != nullptr; }
    u32         IndexCount() const { return m_indexCount; }
    u32         Id() const { return m_id; }
    const AABB& Bounds() const { return m_data.bounds; }

private:
    SkinnedMeshData      m_data;
    ComPtr<ID3D11Buffer> m_vertexBuffer;
    ComPtr<ID3D11Buffer> m_indexBuffer;
    u32                  m_indexCount = 0;
    u32                  m_id = 0;
};

} // namespace erm
