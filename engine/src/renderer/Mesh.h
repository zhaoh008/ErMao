// ErMao Engine - 网格（CPU 数据 + GPU 缓冲）
#pragma once

#include "core/Math.h"
#include "core/Types.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace erm {

using Microsoft::WRL::ComPtr;

// 与 HLSL 顶点输入严格对应：POSITION / NORMAL / TEXCOORD0
struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
};

// CPU 侧网格数据（OBJ 加载器、程序化生成器都产出它）
struct MeshData {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;
    std::vector<u32>  indices;
    AABB              bounds;

    void ComputeBounds();
    void ComputeNormals();
    u32  VertexCount() const { return static_cast<u32>(positions.size()); }
    u32  IndexCount() const { return static_cast<u32>(indices.size()); }
};

// GPU 网格：不可拷贝（ComPtr + 禁用拷贝 + 析构自动释放），修复审核 P2-5/P3-7
class Mesh {
public:
    Mesh() = default;
    ~Mesh() { Shutdown(); }
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&&) = default;
    Mesh& operator=(Mesh&&) = default;

    bool Create(ID3D11Device* device, const MeshData& data);
    void Shutdown();

    void BindVertexBuffers(ID3D11DeviceContext* ctx, ID3D11Buffer* instanceBuffer, UINT instanceStride) const;
    void Draw(ID3D11DeviceContext* ctx, UINT instanceCount = 1) const;

    const MeshData& CPU() const { return m_data; }
    const AABB&     Bounds() const { return m_data.bounds; }
    u32             IndexCount() const { return m_indexCount; }
    bool            IsValid() const { return m_vertexBuffer != nullptr; }
    u32             Id() const { return m_id; } // 用于稳定排序（P2-3）

    ID3D11Buffer* VertexBuffer() const { return m_vertexBuffer.Get(); }
    ID3D11Buffer* IndexBuffer() const { return m_indexBuffer.Get(); }

private:
    MeshData m_data;
    ComPtr<ID3D11Buffer> m_vertexBuffer;
    ComPtr<ID3D11Buffer> m_indexBuffer;
    u32 m_indexCount = 0;
    u32 m_id = 0;
};

// 程序化几何体（保证零外部资源也能演示）
namespace primitives {
MeshData Cube(float size = 1.0f);
MeshData Plane(float size = 1.0f, float uvTiling = 1.0f);
MeshData Sphere(float radius = 0.5f, u32 segments = 32, u32 rings = 20);
MeshData Cylinder(float radius = 0.5f, float height = 1.0f, u32 segments = 24);
MeshData Torus(float majorRadius = 0.7f, float minorRadius = 0.25f, u32 majorSegments = 32,
               u32 minorSegments = 16);
MeshData Capsule(float radius = 0.35f, float height = 1.2f, u32 segments = 16, u32 rings = 12);
} // namespace primitives

} // namespace erm
