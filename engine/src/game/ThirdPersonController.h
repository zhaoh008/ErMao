// ErMao Engine - 第三人称角色控制器（代码优先的玩法示例）
//
// 组成：胶囊角色 + 重力/跳跃 + 球-盒碰撞 + 轨道相机 + 弹簧臂。
// 相机不是角色的子节点，而是由控制器每帧摆放——这样相机可以自由旋转，
// 也能做"撞墙时拉近"的弹簧臂，这是第三人称最常见的手感处理。
#pragma once

#include "core/Math.h"
#include "scene/Component.h"

namespace erm {

class GameObject;

class ThirdPersonController : public Component {
public:
    ER_COMPONENT(ThirdPersonController, "Third Person Controller")

    float moveSpeed = 4.6f;
    float runSpeed = 7.8f;
    float jumpSpeed = 7.0f;
    float gravity = -22.0f;
    float turnSpeed = 14.0f;
    float radius = 0.4f;
    float height = 1.8f;
    float stepHeight = 0.45f; // 可直接迈上的台阶高度

    std::string cameraName = "主相机";
    float cameraDistance = 5.8f;
    float cameraMinDistance = 2.0f;
    float cameraMaxDistance = 9.0f;
    float cameraHeight = 1.35f;
    float mouseSensitivity = 0.16f;
    bool  invertY = false; // 反转俯仰（默认：鼠标下移 = 视角下压）
    float minPitch = -55.0f;
    float maxPitch = 35.0f;

    // 运行时状态（Inspector 可见，便于调试）
    float yawDegrees = 0.0f;
    float pitchDegrees = -12.0f;
    bool  grounded = false;
    Vec3  velocity{0.0f, 0.0f, 0.0f};

    void OnStart() override;
    void OnUpdate(float dt) override;
    void OnFixedUpdate(float dt) override;

    // 编辑器：只有视口获得焦点时才把输入交给角色
    static void SetInputEnabled(bool enabled);
    static bool InputEnabled();

    // 自动化验证：绕过键盘，直接给一段"向前走 + 跳"的输入
    static void SetSimulatedInput(const Vec2& move, bool jump, bool run);
    static void ClearSimulatedInput();

private:
    void UpdateCamera();
    void ResolveCollisions();
    void ResolveCamera();

    GameObject* m_cameraObject = nullptr;
    u64         m_cameraObjectId = 0; // 缓存指针失效校验（World::FindById）
    float       m_verticalVelocity = 0.0f;
    bool        m_simulatedJumpPrev = false;
};

} // namespace erm
