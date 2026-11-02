// ErMao Engine - 相机实现
#include "renderer/Camera.h"
#include "platform/Input.h"

#include <windows.h>

namespace erm {

Ray Camera::ScreenToRay(const Vec2& screen, const Vec2& screenSize) const {
    const float ndcX = (2.0f * screen.x / screenSize.x) - 1.0f;
    const float ndcY = 1.0f - (2.0f * screen.y / screenSize.y);

    const Mat4 invVP = ViewProjection().Inverse();
    Vec4 p0 = invVP * Vec4(ndcX, ndcY, 0.0f, 1.0f);
    Vec4 p1 = invVP * Vec4(ndcX, ndcY, 1.0f, 1.0f);
    if (std::fabs(p0.w) < 1e-6f || std::fabs(p1.w) < 1e-6f) return Ray{position, Forward()};
    p0 = p0 / p0.w;
    p1 = p1 / p1.w;

    Ray ray;
    ray.origin = position;
    ray.direction = Normalize(p1.xyz() - p0.xyz());
    return ray;
}

bool Camera::WorldToScreen(const Vec3& world, const Vec2& screenSize, Vec2& outScreen) const {
    const Vec4 clip = ViewProjection() * Vec4(world, 1.0f);
    if (clip.w <= 1e-5f) return false;
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    outScreen.x = (ndcX * 0.5f + 0.5f) * screenSize.x;
    outScreen.y = (0.5f - ndcY * 0.5f) * screenSize.y;
    return true;
}

void Camera::SetFromMatrix(const Mat4& world) {
    position = world.TransformPoint(Vec3(0, 0, 0));
    const Vec3 forward = Normalize(world.TransformVector(Vec3(0, 0, -1)));
    pitch = std::asin(Clamp(forward.y, -1.0f, 1.0f));
    yaw = std::atan2(-forward.x, -forward.z);
}

void Camera::UpdateFly(float dt, bool mouseCaptured, const Vec2& mouseDelta) {
    if (mouseCaptured) {
        const float sensitivity = 0.0025f;
        yaw -= mouseDelta.x * sensitivity;
        pitch -= mouseDelta.y * sensitivity;
        pitch = Clamp(pitch, -kHalfPi + 0.01f, kHalfPi - 0.01f);
    }

    float speed = moveSpeed;
    if (Input::IsKeyDown(VK_SHIFT)) speed *= 4.0f;
    const Vec3 f = Forward();
    const Vec3 r = Right();
    Vec3 move{0, 0, 0};
    if (Input::IsKeyDown('W')) move += f;
    if (Input::IsKeyDown('S')) move -= f;
    if (Input::IsKeyDown('D')) move += r;
    if (Input::IsKeyDown('A')) move -= r;
    if (Input::IsKeyDown('E')) move += Vec3(0, 1, 0);
    if (Input::IsKeyDown('Q')) move -= Vec3(0, 1, 0);
    if (LengthSq(move) > 0.0f) position += Normalize(move) * speed * dt;
}

} // namespace erm
