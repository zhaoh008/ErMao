// ErMao Engine - 第三人称角色控制器实现
#include "game/ThirdPersonController.h"
#include "core/Log.h"
#include "platform/Input.h"
#include "scene/Components.h"
#include "scene/GameObject.h"
#include "scene/World.h"

#include <windows.h>

namespace erm {

namespace {
bool g_inputEnabled = true;
bool g_simulated = false;
Vec2 g_simulatedMove{0, 0};
bool g_simulatedJump = false;
bool g_simulatedRun = false;
} // namespace

void ThirdPersonController::SetInputEnabled(bool enabled) { g_inputEnabled = enabled; }
bool ThirdPersonController::InputEnabled() { return g_inputEnabled; }

void ThirdPersonController::SetSimulatedInput(const Vec2& move, bool jump, bool run) {
    g_simulated = true;
    g_simulatedMove = move;
    g_simulatedJump = jump;
    g_simulatedRun = run;
}

void ThirdPersonController::ClearSimulatedInput() { g_simulated = false; }

void ThirdPersonController::OnStart() {
    if (!m_owner) return;
    World* world = GetWorld();
    if (world) {
        m_cameraObject = world->FindByName(cameraName);
        if (!m_cameraObject) {
            // 退而求其次：找场景里第一个主相机
            for (const auto& root : world->RootObjects()) {
                root->ForEach([&](GameObject* object) {
                    auto* camera = object->GetComponent<CameraComponent>();
                    if (camera && camera->primary && !m_cameraObject) m_cameraObject = object;
                });
            }
        }
    }
    m_lastPosition = m_owner->Position();
    LogInfo("第三人称控制器就绪（相机: %s）", m_cameraObject ? m_cameraObject->Name().c_str() : "<未找到>");
}

void ThirdPersonController::OnUpdate(float dt) {
    if (!m_owner || dt <= 0.0f) return;
    World* world = GetWorld();
    if (!world) return;

    // ------------------------------------------------ 鼠标：轨道相机
    if (g_inputEnabled && Input::IsMouseDown(1)) {
        const Vec2 delta = Input::MouseDelta();
        yawDegrees += delta.x * mouseSensitivity;
        pitchDegrees -= delta.y * mouseSensitivity;
    }
    const float wheel = Input::WheelDelta();
    if (g_inputEnabled && std::fabs(wheel) > 0.001f) {
        cameraDistance = Clamp(cameraDistance - wheel * 0.6f, cameraMinDistance, cameraMaxDistance);
    }
    pitchDegrees = Clamp(pitchDegrees, minPitch, maxPitch);

    // ------------------------------------------------ 输入：相对相机朝向移动
    const Quat  yawRotation = Quat::FromEuler(ToRadians(yawDegrees), 0.0f, 0.0f);
    const Vec3  cameraForward = yawRotation.RotateVector(Vec3(0, 0, -1));
    const Vec3  cameraRight = yawRotation.RotateVector(Vec3(1, 0, 0));

    Vec3 wish{0, 0, 0};
    if (g_simulated) {
        wish = cameraForward * g_simulatedMove.y + cameraRight * g_simulatedMove.x;
    } else if (g_inputEnabled) {
        if (Input::IsKeyDown('W')) wish += cameraForward;
        if (Input::IsKeyDown('S')) wish -= cameraForward;
        if (Input::IsKeyDown('D')) wish += cameraRight;
        if (Input::IsKeyDown('A')) wish -= cameraRight;
    }
    if (LengthSq(wish) > 0.0f) wish = Normalize(wish);

    const bool  running = g_simulated ? g_simulatedRun : Input::IsKeyDown(VK_SHIFT);
    const bool  jumpPressed = g_simulated ? g_simulatedJump : (g_inputEnabled && Input::IsKeyDown(VK_SPACE));
    const float speed = running ? runSpeed : moveSpeed;

    // 水平速度直接给定（教学级角色：不做加速度/摩擦模型，手感更"跟手"）
    velocity.x = wish.x * speed;
    velocity.z = wish.z * speed;

    // ------------------------------------------------ 重力与跳跃
    if (grounded && jumpPressed) {
        m_verticalVelocity = jumpSpeed;
        grounded = false;
    }
    m_verticalVelocity += gravity * dt;
    if (m_verticalVelocity < -30.0f) m_verticalVelocity = -30.0f;

    Vec3 position = m_owner->Position();
    position += Vec3(velocity.x * dt, m_verticalVelocity * dt, velocity.z * dt);
    m_owner->SetPosition(position);

    ResolveCollisions();

    // ------------------------------------------------ 地面检测（射线向下）
    position = m_owner->Position();
    Ray groundRay;
    groundRay.origin = position + Vec3(0, 0.15f, 0);
    groundRay.direction = Vec3(0, -1, 0);
    World::RaycastHit hit;
    const float groundProbe = height * 0.5f + 0.35f;
    if (world->Raycast(groundRay, groundProbe, hit) && hit.object != m_owner) {
        const float targetY = hit.point.y + height * 0.5f;
        if (position.y <= targetY + 0.05f) {
            position.y = targetY;
            m_verticalVelocity = 0.0f;
            grounded = true;
            m_owner->SetPosition(position);
        }
    } else if (position.y - height * 0.5f <= 0.0f) {
        position.y = height * 0.5f;
        m_verticalVelocity = 0.0f;
        grounded = true;
        m_owner->SetPosition(position);
    } else {
        grounded = false;
    }

    // ------------------------------------------------ 朝向
    if (LengthSq(wish) > 0.001f) {
        const float targetYaw = std::atan2(-wish.x, -wish.z);
        float currentYaw = 0.0f, pitch = 0.0f, roll = 0.0f;
        m_owner->Rotation().ToEuler(currentYaw, pitch, roll);
        float delta = targetYaw - currentYaw;
        while (delta > kPi) delta -= kTwoPi;
        while (delta < -kPi) delta += kTwoPi;
        const float step = turnSpeed * dt;
        delta = Clamp(delta, -step, step);
        m_owner->SetRotation(Quat::FromEuler(currentYaw + delta, 0.0f, 0.0f));
    }

    UpdateCamera();
    m_lastPosition = m_owner->Position();
}

void ThirdPersonController::ResolveCollisions() {
    World* world = GetWorld();
    if (!world) return;

    std::vector<std::pair<GameObject*, AABB>> boxes;
    world->CollectMeshAABBs(boxes);

    Vec3 position = m_owner->Position();
    const Vec3 center = position;

    for (const auto& entry : boxes) {
        if (entry.first == m_owner) continue;
        if (entry.first->Parent() == m_owner) continue;

        const AABB& box = entry.second;
        const Vec3 closest{Clamp(center.x, box.min.x, box.max.x), Clamp(center.y - height * 0.1f, box.min.y, box.max.y),
                           Clamp(center.z, box.min.z, box.max.z)};
        Vec3 delta = Vec3(center.x, center.y - height * 0.1f, center.z) - closest;
        const float distance = Length(delta);

        // 只在 XZ 上做推离：竖直方向交给地面检测，避免和"站上平台"打架
        if (distance < radius && distance > 1e-4f) {
            delta.y = 0.0f;
            if (LengthSq(delta) < 1e-6f) continue;
            const Vec3 normal = Normalize(delta);
            const float penetration = radius - distance;
            position += normal * penetration;
            // 速度沿法线分量清零，避免持续"挤墙"
            const float vn = Dot(Vec3(velocity.x, 0, velocity.z), normal);
            if (vn < 0.0f) {
                velocity.x -= normal.x * vn;
                velocity.z -= normal.z * vn;
            }
        }
    }
    m_owner->SetPosition(position);
}

void ThirdPersonController::UpdateCamera() {
    if (!m_cameraObject || !m_owner) return;
    World* world = GetWorld();

    const Quat rotation = Quat::FromEuler(ToRadians(yawDegrees), ToRadians(pitchDegrees), 0.0f);
    const Vec3 forward = rotation.RotateVector(Vec3(0, 0, -1));
    const Vec3 head = m_owner->Position() + Vec3(0, cameraHeight, 0);

    float distance = cameraDistance;
    if (world) {
        // 弹簧臂：从头部向相机方向打一条射线，撞到东西就把相机拉近
        Ray armRay;
        armRay.origin = head;
        armRay.direction = -forward;
        World::RaycastHit hit;
        if (world->Raycast(armRay, distance + 0.3f, hit) && hit.object != m_owner) {
            distance = (std::max)(cameraMinDistance, hit.distance - 0.35f);
        }
    }

    const Vec3 desired = head - forward * distance;
    m_cameraObject->SetPosition(desired);
    m_cameraObject->SetRotation(rotation);
}

// ---------------------------------------------------------------- 反射
void ThirdPersonController::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<ThirdPersonController>(); };
    t.Field("moveSpeed", FieldType::Float, &ThirdPersonController::moveSpeed, "移动速度", 0.5f, 20.0f);
    t.Field("runSpeed", FieldType::Float, &ThirdPersonController::runSpeed, "奔跑速度", 0.5f, 30.0f);
    t.Field("jumpSpeed", FieldType::Float, &ThirdPersonController::jumpSpeed, "跳跃速度", 0.0f, 20.0f);
    t.Field("gravity", FieldType::Float, &ThirdPersonController::gravity, "重力", -60.0f, 0.0f);
    t.Field("turnSpeed", FieldType::Float, &ThirdPersonController::turnSpeed, "转身速度", 1.0f, 40.0f);
    t.Field("radius", FieldType::Float, &ThirdPersonController::radius, "碰撞半径", 0.1f, 2.0f);
    t.Field("height", FieldType::Float, &ThirdPersonController::height, "身高", 0.5f, 4.0f);
    t.Field("cameraName", FieldType::String, &ThirdPersonController::cameraName, "相机对象");
    t.Field("cameraDistance", FieldType::Float, &ThirdPersonController::cameraDistance, "相机距离", 1.0f, 20.0f);
    t.Field("cameraHeight", FieldType::Float, &ThirdPersonController::cameraHeight, "相机高度", 0.0f, 4.0f);
    t.Field("mouseSensitivity", FieldType::Float, &ThirdPersonController::mouseSensitivity, "鼠标灵敏度", 0.01f, 1.0f);
    t.Field("yawDegrees", FieldType::Float, &ThirdPersonController::yawDegrees, "相机偏航", -360.0f, 360.0f);
    t.Field("pitchDegrees", FieldType::Float, &ThirdPersonController::pitchDegrees, "相机俯仰", -80.0f, 80.0f);
    t.Field("grounded", FieldType::Bool, &ThirdPersonController::grounded, "着地");
}

} // namespace erm
