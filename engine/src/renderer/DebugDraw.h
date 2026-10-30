// ErMao Engine - 调试绘制（线段批处理，深度测试/覆盖两种模式）
#pragma once

#include "core/Math.h"
#include "core/Types.h"

#include <d3d11.h>

namespace erm {

// 所有调试几何都退化成线段：AABB、球、网格、射线、路径
// 每帧流程：Begin() → 若干 Line/Box/Sphere → Flush(ctx) → Draw(ctx, depthTest)
class DebugDraw {
public:
    bool Init(ID3D11Device* device, u32 initialVertices = 65536);
    void Shutdown();

    void Begin();
    void Line(const Vec3& a, const Vec3& b, const Vec4& color, bool depthTest = true);
    void Box(const AABB& box, const Vec4& color, bool depthTest = true);
    void BoxOriented(const Mat4& transform, const Vec3& extents, const Vec4& color, bool depthTest = true);
    void Sphere(const Vec3& center, float radius, const Vec4& color, int segments = 16, bool depthTest = true);
    void Circle(const Vec3& center, const Vec3& normal, float radius, const Vec4& color, int segments = 32,
                bool depthTest = true);
    void RayLine(const Ray& ray, float length, const Vec4& color, bool depthTest = true);
    void Grid(float halfSize, float step, const Vec4& colorXZ, const Vec4& colorMajor, bool depthTest = true);
    void Cross(const Vec3& center, float size, const Vec4& color, bool depthTest = true);
    void Arrow(const Vec3& from, const Vec3& to, const Vec4& color, bool depthTest = true);

    // 把 CPU 线段上传到动态顶点缓冲
    void Flush(ID3D11DeviceContext* ctx);
    // 绘制某一批（调用前需已绑好 shader / 输入布局 / 常量缓冲）
    void Draw(ID3D11DeviceContext* ctx, bool depthTest) const;

    int LineCount() const { return static_cast<int>(m_depthLines.size() / 2 + m_overlayLines.size() / 2); }

private:
    struct LineVertex {
        Vec3 pos;
        Vec4 color;
    };

    bool UpdateBuffer(ID3D11DeviceContext* ctx, std::vector<LineVertex>& verts, ID3D11Buffer*& buffer,
                      u32& capacity, u32& count);
    void DrawBuffer(ID3D11DeviceContext* ctx, ID3D11Buffer* buffer, u32 count) const;

    ID3D11Device* m_device = nullptr;
    std::vector<LineVertex> m_depthLines;
    std::vector<LineVertex> m_overlayLines;
    ID3D11Buffer* m_depthBuffer = nullptr;
    ID3D11Buffer* m_overlayBuffer = nullptr;
    u32 m_depthCapacity = 0;
    u32 m_overlayCapacity = 0;
    u32 m_depthCount = 0;
    u32 m_overlayCount = 0;
};

} // namespace erm
