// ErMao Engine - 相机（GAMES104 第 4 讲：观察空间与投影）
#pragma once

#include "core/Math.h"
#include "core/Types.h"

namespace erm {

class Camera {
public:
    Vec3  position{0.0f, 2.5f, 9.0f};
    float yaw = -kHalfPi;   // 绕 Y，弧度；-90° 时朝向 +Z
    float pitch = -0.15f;   // 绕 X
    float fovY = ToRadians(60.0f);
    float nearZ = 0.1f;
    float farZ = 500.0f;
    float aspect = 16.0f / 9.0f;
    float moveSpeed = 8.0f;

    Vec3 Forward() const { return Quat::FromEuler(yaw, pitch, 0.0f).RotateVector({0, 0, -1}); }
    Vec3 Right() const { return Quat::FromEuler(yaw, pitch, 0.0f).RotateVector({1, 0, 0}); }
    Vec3 Up() const { return Quat::FromEuler(yaw, pitch, 0.0f).RotateVector({0, 1, 0}); }

    Mat4 View() const { return Mat4::LookAt(position, position + Forward(), Vec3(0, 1, 0)); }
    Mat4 Projection() const { return Mat4::Perspective(fovY, aspect, nearZ, farZ); }
    Mat4 ViewProjection() const { return Projection() * View(); }

    Frustum GetFrustum() const { return Frustum::FromViewProj(ViewProjection()); }

    void SetViewport(u32 width, u32 height) {
        if (height > 0) aspect = static_cast<float>(width) / static_cast<float>(height);
    }

    // 屏幕坐标（左上原点）→ 世界射线
    Ray ScreenToRay(const Vec2& screen, const Vec2& screenSize) const;

    // 从 GameObject 的世界矩阵同步位置与朝向（CameraComponent 用）
    void SetFromMatrix(const Mat4& world);

    // 世界坐标 → 屏幕坐标（用于 UI 叠加；返回 false 表示在相机背面）
    bool WorldToScreen(const Vec3& world, const Vec2& screenSize, Vec2& outScreen) const;

    // 编辑器自由视角：WASD 移动、QE 升降、Shift 加速、右键转视角
    void UpdateFly(float dt, bool mouseCaptured, const Vec2& mouseDelta);
};

} // namespace erm
