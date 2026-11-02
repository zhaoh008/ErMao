// ErMao Engine - 调试绘制实现
#include "renderer/DebugDraw.h"
#include "core/Log.h"

namespace erm {

bool DebugDraw::Init(ID3D11Device* device, u32 initialVertices) {
    m_device = device;
    m_depthCapacity = initialVertices;
    m_overlayCapacity = initialVertices;

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = initialVertices * sizeof(LineVertex);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateBuffer(&desc, nullptr, &m_depthBuffer))) {
        LogError("调试线顶点缓冲创建失败");
        return false;
    }
    if (FAILED(device->CreateBuffer(&desc, nullptr, &m_overlayBuffer))) return false;
    return true;
}

void DebugDraw::Shutdown() {
    if (m_depthBuffer) { m_depthBuffer->Release(); m_depthBuffer = nullptr; }
    if (m_overlayBuffer) { m_overlayBuffer->Release(); m_overlayBuffer = nullptr; }
    m_depthCapacity = m_overlayCapacity = 0;
    m_depthCount = m_overlayCount = 0;
}

void DebugDraw::Begin() {
    m_depthLines.clear();
    m_overlayLines.clear();
}

void DebugDraw::Line(const Vec3& a, const Vec3& b, const Vec4& color, bool depthTest) {
    auto& list = depthTest ? m_depthLines : m_overlayLines;
    list.push_back({a, color});
    list.push_back({b, color});
}

void DebugDraw::Box(const AABB& box, const Vec4& color, bool depthTest) {
    const Vec3 c[8] = {{box.min.x, box.min.y, box.min.z}, {box.max.x, box.min.y, box.min.z},
                       {box.max.x, box.max.y, box.min.z}, {box.min.x, box.max.y, box.min.z},
                       {box.min.x, box.min.y, box.max.z}, {box.max.x, box.min.y, box.max.z},
                       {box.max.x, box.max.y, box.max.z}, {box.min.x, box.max.y, box.max.z}};
    const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                              {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (auto& e : edges) Line(c[e[0]], c[e[1]], color, depthTest);
}

void DebugDraw::BoxOriented(const Mat4& transform, const Vec3& extents, const Vec4& color, bool depthTest) {
    const Vec3 c[8] = {{-extents.x, -extents.y, -extents.z}, {extents.x, -extents.y, -extents.z},
                       {extents.x, extents.y, -extents.z},  {-extents.x, extents.y, -extents.z},
                       {-extents.x, -extents.y, extents.z}, {extents.x, -extents.y, extents.z},
                       {extents.x, extents.y, extents.z},   {-extents.x, extents.y, extents.z}};
    Vec3 w[8];
    for (int i = 0; i < 8; ++i) w[i] = transform.TransformPoint(c[i]);
    const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                              {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (auto& e : edges) Line(w[e[0]], w[e[1]], color, depthTest);
}

void DebugDraw::Sphere(const Vec3& center, float radius, const Vec4& color, int segments, bool depthTest) {
    Circle(center, {1, 0, 0}, radius, color, segments, depthTest);
    Circle(center, {0, 1, 0}, radius, color, segments, depthTest);
    Circle(center, {0, 0, 1}, radius, color, segments, depthTest);
}

void DebugDraw::Circle(const Vec3& center, const Vec3& normal, float radius, const Vec4& color, int segments,
                       bool depthTest) {
    const Vec3 n = Normalize(normal);
    const Vec3 tangent = Normalize(std::fabs(n.y) > 0.99f ? erm::Cross(n, Vec3(1, 0, 0)) : erm::Cross(n, Vec3(0, 1, 0)));
    const Vec3 bitangent = erm::Cross(n, tangent);
    Vec3 prev = center + tangent * radius;
    for (int i = 1; i <= segments; ++i) {
        const float a = static_cast<float>(i) / segments * kTwoPi;
        const Vec3 p = center + (tangent * std::cos(a) + bitangent * std::sin(a)) * radius;
        Line(prev, p, color, depthTest);
        prev = p;
    }
}

void DebugDraw::RayLine(const Ray& ray, float length, const Vec4& color, bool depthTest) {
    Line(ray.origin, ray.origin + ray.direction * length, color, depthTest);
}

void DebugDraw::Grid(float halfSize, float step, const Vec4& colorXZ, const Vec4& colorMajor, bool depthTest) {
    const int count = static_cast<int>(halfSize / step);
    for (int i = -count; i <= count; ++i) {
        const float p = i * step;
        const Vec4& color = (i == 0) ? colorMajor : colorXZ;
        Line({p, 0, -halfSize}, {p, 0, halfSize}, color, depthTest);
        Line({-halfSize, 0, p}, {halfSize, 0, p}, color, depthTest);
    }
}

void DebugDraw::Cross(const Vec3& center, float size, const Vec4& color, bool depthTest) {
    Line(center - Vec3(size, 0, 0), center + Vec3(size, 0, 0), color, depthTest);
    Line(center - Vec3(0, size, 0), center + Vec3(0, size, 0), color, depthTest);
    Line(center - Vec3(0, 0, size), center + Vec3(0, 0, size), color, depthTest);
}

void DebugDraw::Arrow(const Vec3& from, const Vec3& to, const Vec4& color, bool depthTest) {
    Line(from, to, color, depthTest);
    const Vec3 dir = Normalize(to - from);
    if (LengthSq(dir) < 0.5f) return;
    Vec3 side = Normalize(std::fabs(dir.y) > 0.9f ? erm::Cross(dir, Vec3(1, 0, 0)) : erm::Cross(dir, Vec3(0, 1, 0)));
    const Vec3 back = to - dir * (Length(to - from) * 0.18f);
    const float w = Length(to - from) * 0.07f;
    Line(to, back + side * w, color, depthTest);
    Line(to, back - side * w, color, depthTest);
    Line(to, back + erm::Cross(dir, side) * w, color, depthTest);
    Line(to, back - erm::Cross(dir, side) * w, color, depthTest);
}

bool DebugDraw::UpdateBuffer(ID3D11DeviceContext* ctx, std::vector<LineVertex>& verts, ID3D11Buffer*& buffer,
                             u32& capacity, u32& count) {
    count = static_cast<u32>(verts.size());
    if (count == 0) return true;

    if (capacity < count) {
        if (buffer) { buffer->Release(); buffer = nullptr; }
        capacity = count + count / 2 + 1024;
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = capacity * sizeof(LineVertex);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(m_device->CreateBuffer(&desc, nullptr, &buffer))) {
            LogError("调试线缓冲扩容失败");
            count = 0;
            return false;
        }
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
    std::memcpy(mapped.pData, verts.data(), count * sizeof(LineVertex));
    ctx->Unmap(buffer, 0);
    return true;
}

void DebugDraw::Flush(ID3D11DeviceContext* ctx) {
    UpdateBuffer(ctx, m_depthLines, m_depthBuffer, m_depthCapacity, m_depthCount);
    UpdateBuffer(ctx, m_overlayLines, m_overlayBuffer, m_overlayCapacity, m_overlayCount);
}

void DebugDraw::DrawBuffer(ID3D11DeviceContext* ctx, ID3D11Buffer* buffer, u32 count) const {
    if (!buffer || count == 0) return;
    UINT stride = sizeof(LineVertex);
    UINT offset = 0;
    ctx->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    ctx->Draw(count, 0);
}

void DebugDraw::Draw(ID3D11DeviceContext* ctx, bool depthTest) const {
    if (depthTest) {
        DrawBuffer(ctx, m_depthBuffer, m_depthCount);
    } else {
        DrawBuffer(ctx, m_overlayBuffer, m_overlayCount);
    }
}

} // namespace erm
