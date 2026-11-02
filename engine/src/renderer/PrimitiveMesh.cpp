// ErMao Engine - 程序化几何体
//
// 所有三角形的绕序统一为「从外侧看逆时针（CCW）」，配合 FrontCounterClockwise=TRUE 的
// 光栅化状态，导出法线朝外。改这里的话记得用截图验证一下剔除方向。
#include "renderer/Mesh.h"
#include "core/Math.h"

namespace erm {
namespace primitives {

namespace {
// 添加一个四边形（p0..p3 从外侧看逆时针），uv 沿 (p1-p0) 为 U、(p3-p0) 为 V
void AddQuad(MeshData& md, const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, const Vec3& normal,
             float uvScale = 1.0f) {
    const u32 base = static_cast<u32>(md.positions.size());
    md.positions.push_back(p0);
    md.positions.push_back(p1);
    md.positions.push_back(p2);
    md.positions.push_back(p3);
    for (int i = 0; i < 4; ++i) md.normals.push_back(normal);
    const float u = uvScale;
    md.uvs.push_back({0.0f, 1.0f});
    md.uvs.push_back({0.0f, 0.0f});
    md.uvs.push_back({u, 0.0f});
    md.uvs.push_back({u, 1.0f});
    md.indices.insert(md.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}
} // namespace

MeshData Cube(float size) {
    MeshData md;
    const float h = size * 0.5f;
    // +X: 从 +X 方向看，顺序为 (h,-h,-h) -> (h,h,-h) -> (h,h,h) -> (h,-h,h)
    AddQuad(md, {h, -h, -h}, {h, h, -h}, {h, h, h}, {h, -h, h}, {1, 0, 0});
    // -X
    AddQuad(md, {-h, -h, h}, {-h, h, h}, {-h, h, -h}, {-h, -h, -h}, {-1, 0, 0});
    // +Y
    AddQuad(md, {-h, h, -h}, {-h, h, h}, {h, h, h}, {h, h, -h}, {0, 1, 0});
    // -Y
    AddQuad(md, {h, -h, h}, {-h, -h, h}, {-h, -h, -h}, {h, -h, -h}, {0, -1, 0});
    // +Z
    AddQuad(md, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}, {0, 0, 1});
    // -Z
    AddQuad(md, {h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}, {0, 0, -1});
    md.ComputeBounds();
    return md;
}

MeshData Plane(float size, float uvTiling) {
    MeshData md;
    const float h = size * 0.5f;
    const u32 seg = 8; // 细分一下，方便将来做顶点动画/风
    for (u32 z = 0; z <= seg; ++z) {
        for (u32 x = 0; x <= seg; ++x) {
            const float fx = static_cast<float>(x) / seg;
            const float fz = static_cast<float>(z) / seg;
            md.positions.push_back({-h + fx * size, 0.0f, -h + fz * size});
            md.normals.push_back({0, 1, 0});
            md.uvs.push_back({fx * uvTiling, fz * uvTiling});
        }
    }
    const u32 stride = seg + 1;
    for (u32 z = 0; z < seg; ++z) {
        for (u32 x = 0; x < seg; ++x) {
            const u32 i0 = z * stride + x;
            const u32 i1 = i0 + 1;
            const u32 i2 = i0 + stride + 1;
            const u32 i3 = i0 + stride;
            // 从上方看逆时针
            md.indices.insert(md.indices.end(), {i0, i3, i2, i0, i2, i1});
        }
    }
    md.ComputeBounds();
    return md;
}

MeshData Sphere(float radius, u32 segments, u32 rings) {
    MeshData md;
    segments = (std::max)(segments, 3u);
    rings = (std::max)(rings, 2u);
    for (u32 y = 0; y <= rings; ++y) {
        const float v = static_cast<float>(y) / rings;
        const float phi = v * kPi; // 0..pi
        for (u32 x = 0; x <= segments; ++x) {
            const float u = static_cast<float>(x) / segments;
            const float theta = u * kTwoPi;
            const Vec3 n{std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)};
            md.positions.push_back(n * radius);
            md.normals.push_back(n);
            md.uvs.push_back({u, v});
        }
    }
    const u32 stride = segments + 1;
    for (u32 y = 0; y < rings; ++y) {
        for (u32 x = 0; x < segments; ++x) {
            const u32 a = y * stride + x;
            const u32 b = a + 1;
            const u32 c = a + stride + 1;
            const u32 d = a + stride;
            md.indices.insert(md.indices.end(), {a, d, c, a, c, b});
        }
    }
    md.ComputeBounds();
    return md;
}

MeshData Cylinder(float radius, float height, u32 segments) {
    MeshData md;
    const float h = height * 0.5f;
    segments = (std::max)(segments, 3u);
    // 侧面
    for (u32 i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / segments;
        const float theta = t * kTwoPi;
        const Vec3 n{std::cos(theta), 0.0f, std::sin(theta)};
        md.positions.push_back({n.x * radius, -h, n.z * radius});
        md.normals.push_back(n);
        md.uvs.push_back({t, 1.0f});
        md.positions.push_back({n.x * radius, h, n.z * radius});
        md.normals.push_back(n);
        md.uvs.push_back({t, 0.0f});
    }
    for (u32 i = 0; i < segments; ++i) {
        const u32 a = i * 2, b = a + 1, c = a + 2, d = a + 3;
        md.indices.insert(md.indices.end(), {a, b, c, b, d, c});
    }
    // 顶盖
    const u32 topCenter = static_cast<u32>(md.positions.size());
    md.positions.push_back({0, h, 0});
    md.normals.push_back({0, 1, 0});
    md.uvs.push_back({0.5f, 0.5f});
    for (u32 i = 0; i <= segments; ++i) {
        const float theta = static_cast<float>(i) / segments * kTwoPi;
        md.positions.push_back({std::cos(theta) * radius, h, std::sin(theta) * radius});
        md.normals.push_back({0, 1, 0});
        md.uvs.push_back({std::cos(theta) * 0.5f + 0.5f, std::sin(theta) * 0.5f + 0.5f});
    }
    for (u32 i = 0; i < segments; ++i) {
        md.indices.insert(md.indices.end(), {topCenter, topCenter + 2 + i, topCenter + 1 + i});
    }
    // 底盖
    const u32 bottomCenter = static_cast<u32>(md.positions.size());
    md.positions.push_back({0, -h, 0});
    md.normals.push_back({0, -1, 0});
    md.uvs.push_back({0.5f, 0.5f});
    for (u32 i = 0; i <= segments; ++i) {
        const float theta = static_cast<float>(i) / segments * kTwoPi;
        md.positions.push_back({std::cos(theta) * radius, -h, std::sin(theta) * radius});
        md.normals.push_back({0, -1, 0});
        md.uvs.push_back({std::cos(theta) * 0.5f + 0.5f, std::sin(theta) * 0.5f + 0.5f});
    }
    for (u32 i = 0; i < segments; ++i) {
        md.indices.insert(md.indices.end(), {bottomCenter, bottomCenter + 1 + i, bottomCenter + 2 + i});
    }
    md.ComputeBounds();
    return md;
}

MeshData Torus(float majorRadius, float minorRadius, u32 majorSegments, u32 minorSegments) {
    MeshData md;
    majorSegments = (std::max)(majorSegments, 3u);
    minorSegments = (std::max)(minorSegments, 3u);
    for (u32 i = 0; i <= majorSegments; ++i) {
        const float u = static_cast<float>(i) / majorSegments;
        const float theta = u * kTwoPi;
        const Vec3 center{std::cos(theta) * majorRadius, 0.0f, std::sin(theta) * majorRadius};
        const Vec3 outward{std::cos(theta), 0.0f, std::sin(theta)};
        for (u32 j = 0; j <= minorSegments; ++j) {
            const float v = static_cast<float>(j) / minorSegments;
            const float phi = v * kTwoPi;
            const Vec3 n = outward * std::cos(phi) + Vec3(0, std::sin(phi), 0);
            md.positions.push_back(center + n * minorRadius);
            md.normals.push_back(n);
            md.uvs.push_back({u, v});
        }
    }
    const u32 stride = minorSegments + 1;
    for (u32 i = 0; i < majorSegments; ++i) {
        for (u32 j = 0; j < minorSegments; ++j) {
            const u32 a = i * stride + j;
            const u32 b = a + stride;
            const u32 c = b + 1;
            const u32 d = a + 1;
            md.indices.insert(md.indices.end(), {a, b, c, a, c, d});
        }
    }
    md.ComputeBounds();
    return md;
}

MeshData Capsule(float radius, float height, u32 segments, u32 rings) {
    // 中间圆柱 + 两个半球，用 UV 球面参数化拼接；仅用于角色演示
    MeshData md;
    const float halfH = (std::max)(height * 0.5f - radius, 0.01f);
    const u32 totalRings = rings * 2 + 2;
    for (u32 y = 0; y <= totalRings; ++y) {
        const float t = static_cast<float>(y) / totalRings;
        const float phi = t * kPi;
        for (u32 x = 0; x <= segments; ++x) {
            const float u = static_cast<float>(x) / segments;
            const float theta = u * kTwoPi;
            const float sinPhi = std::sin(phi);
            const float cosPhi = std::cos(phi);
            Vec3 p{sinPhi * std::cos(theta) * radius, cosPhi * radius, sinPhi * std::sin(theta) * radius};
            p.y += (cosPhi > 0.0f) ? halfH : -halfH;
            Vec3 n = Normalize(p - Vec3(0, (cosPhi > 0.0f) ? halfH : -halfH, 0));
            md.positions.push_back(p);
            md.normals.push_back(n);
            md.uvs.push_back({u, t});
        }
    }
    const u32 stride = segments + 1;
    for (u32 y = 0; y < totalRings; ++y) {
        for (u32 x = 0; x < segments; ++x) {
            const u32 a = y * stride + x;
            const u32 b = a + 1;
            const u32 c = a + stride + 1;
            const u32 d = a + stride;
            md.indices.insert(md.indices.end(), {a, d, c, a, c, b});
        }
    }
    md.ComputeBounds();
    return md;
}

} // namespace primitives
} // namespace erm
