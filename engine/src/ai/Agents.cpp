// ErMao Engine - AI 组件实现
#include "ai/Agents.h"
#include "ai/NavGrid.h"
#include "core/Log.h"
#include "platform/Input.h"
#include "scene/Components.h"
#include "scene/GameObject.h"
#include "scene/World.h"

#include <windows.h>

#include <cstdlib>

namespace erm {

namespace {
float RandomRange(float lo, float hi) {
    const float t = static_cast<float>(std::rand() % 10000) / 10000.0f;
    return lo + (hi - lo) * t;
}
} // namespace

void NavAgentComponent::OnStart() {
    if (patrolCenter == Vec3(0, 0, 0) && m_owner) patrolCenter = m_owner->Position();
}

GameObject* NavAgentComponent::FindPlayer(World* world) const {
    if (!world) return nullptr;
    for (GameObject* root : world->RootObjects()) {
        GameObject* found = nullptr;
        root->ForEach([&](GameObject* object) {
            if (!found && object->GetComponent<PlayerController>()) found = object;
        });
        if (found) return found;
    }
    return world->FindByName("玩家");
}

bool NavAgentComponent::CanSeePlayer(World* world, Vec3& playerPos) const {
    GameObject* player = FindPlayer(world);
    if (!player || !m_owner) return false;
    playerPos = player->WorldPosition();
    const Vec3 eye = m_owner->WorldPosition() + Vec3(0, 0.8f, 0);
    const Vec3 toPlayer = playerPos - eye;
    const float distance = Length(toPlayer);
    if (distance > sightRange || distance < 0.001f) return false;

    const Vec3 dir = toPlayer / distance;
    const Vec3 forward = Normalize(m_owner->Forward());
    const float cosAngle = Dot(forward, dir);
    if (cosAngle < std::cos(ToRadians(sightAngleDegrees))) return false;

    // 视线遮挡：用导航网格做 2D 射线检测（地形/墙体阻挡）
    if (world->Nav().IsValid()) {
        if (!world->Nav().HasLineOfSight(eye, playerPos)) return false;
    }
    return true;
}

void NavAgentComponent::RequestPath(World* world, const Vec3& goal) {
    if (!m_owner) return;
    std::vector<Vec3> newPath;
    if (world->Nav().FindPath(m_owner->WorldPosition(), goal, newPath)) {
        path = std::move(newPath);
        pathIndex = 0;
    }
}

void NavAgentComponent::OnUpdate(float dt) {
    if (!m_owner) return;
    World* world = GetWorld();
    if (!world) return;

    Vec3 playerPos;
    const bool seesPlayer = CanSeePlayer(world, playerPos);
    state = seesPlayer ? 1 : 0;

    repathTimer -= dt;
    const float distanceToPlayer = Length(playerPos - m_owner->WorldPosition());

    if (seesPlayer) {
        // 追击：玩家移动超过一定距离就重新寻路
        if (repathTimer <= 0.0f) {
            RequestPath(world, playerPos);
            repathTimer = repathInterval;
        }
        waitTimer = 0.0f;
    } else {
        // 巡逻：到达后随机换一个目标点
        const bool atEnd = path.empty() || pathIndex >= path.size();
        if (atEnd) {
            waitTimer -= dt;
            if (waitTimer <= 0.0f) {
                if (repathTimer <= 0.0f) {
                    Vec3 target = patrolCenter + Vec3(RandomRange(-patrolRadius, patrolRadius), 0.0f,
                                                      RandomRange(-patrolRadius, patrolRadius));
                    RequestPath(world, target);
                    repathTimer = 1.2f;
                    waitTimer = RandomRange(0.5f, 1.8f);
                }
            }
        }
    }

    if (path.empty() || pathIndex >= path.size()) return;

    const Vec3 current = m_owner->Position();
    Vec3 target = path[pathIndex];
    target.y = current.y;
    const Vec3 toTarget = target - current;
    const float distance = Length(toTarget);
    if (distance < 0.35f) {
        ++pathIndex;
        return;
    }

    const Vec3 direction = toTarget / (distance > 0.0001f ? distance : 1.0f);
    const float speed = seesPlayer ? moveSpeed * (distanceToPlayer < 2.5f ? 0.2f : 1.35f) : moveSpeed;
    Vec3 position = current + direction * (speed * dt);
    position.y = current.y;
    m_owner->SetPosition(position);

    // 朝向平滑插值
    const float targetYaw = std::atan2(-direction.x, -direction.z);
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    m_owner->Rotation().ToEuler(yaw, pitch, roll);
    float delta = targetYaw - yaw;
    while (delta > kPi) delta -= kTwoPi;
    while (delta < -kPi) delta += kTwoPi;
    const float maxStep = turnSpeed * dt;
    if (delta > maxStep) delta = maxStep;
    if (delta < -maxStep) delta = -maxStep;
    m_owner->SetRotation(Quat::FromEuler(yaw + delta, 0.0f, 0.0f));
}

void PlayerController::OnUpdate(float dt) {
    if (!m_owner) return;
    World* world = GetWorld();
    if (!world) return;

    Vec3 forward(0, 0, -1);
    Vec3 right(1, 0, 0);
    if (Camera* camera = world->GetActiveCamera()) {
        forward = camera->Forward();
        forward.y = 0.0f;
        if (LengthSq(forward) > 0.0001f) forward = Normalize(forward);
        // 相机右向量 = forward × up（右手系，Y 向上）；forward.z/-x 的旧式叉乘方向相反，会让 A/D 左右颠倒。
        right = Vec3(-forward.z, 0.0f, forward.x);
    }

    Vec3 move{0, 0, 0};
    if (Input::IsKeyDown('W')) move += forward;
    if (Input::IsKeyDown('S')) move -= forward;
    if (Input::IsKeyDown('D')) move += right;
    if (Input::IsKeyDown('A')) move -= right;

    Vec3 position = m_owner->Position();
    if (LengthSq(move) > 0.0f) {
        move = Normalize(move);
        float speed = moveSpeed;
        if (Input::IsKeyDown(VK_SHIFT)) speed *= runMultiplier;
        position += move * speed * dt;
        position.y = height;

        // 朝向移动方向
        const float targetYaw = std::atan2(-move.x, -move.z);
        float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
        m_owner->Rotation().ToEuler(yaw, pitch, roll);
        float delta = targetYaw - yaw;
        while (delta > kPi) delta -= kTwoPi;
        while (delta < -kPi) delta += kTwoPi;
        const float maxStep = turnSpeed * dt;
        if (delta > maxStep) delta = maxStep;
        if (delta < -maxStep) delta = -maxStep;
        m_owner->SetRotation(Quat::FromEuler(yaw + delta, 0.0f, 0.0f));
    }
    m_owner->SetPosition(position);
}

// ---------------------------------------------------------------- 反射
void NavAgentComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<NavAgentComponent>(); };
    t.Field("moveSpeed", FieldType::Float, &NavAgentComponent::moveSpeed, "移动速度", 0.1f, 12.0f);
    t.Field("turnSpeed", FieldType::Float, &NavAgentComponent::turnSpeed, "转向速度", 0.5f, 30.0f);
    t.Field("patrolCenter", FieldType::Vec3, &NavAgentComponent::patrolCenter, "巡逻中心");
    t.Field("patrolRadius", FieldType::Float, &NavAgentComponent::patrolRadius, "巡逻半径", 0.0f, 30.0f);
    t.Field("sightRange", FieldType::Float, &NavAgentComponent::sightRange, "视野距离", 1.0f, 40.0f);
    t.Field("sightAngleDegrees", FieldType::Float, &NavAgentComponent::sightAngleDegrees, "视野角", 10.0f, 360.0f);
    t.Field("state", FieldType::Int, &NavAgentComponent::state, "状态(0巡逻/1追击)", 0.0f, 1.0f);
}

void PlayerController::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<PlayerController>(); };
    t.Field("moveSpeed", FieldType::Float, &PlayerController::moveSpeed, "移动速度", 0.5f, 20.0f);
    t.Field("runMultiplier", FieldType::Float, &PlayerController::runMultiplier, "奔跑倍率", 1.0f, 4.0f);
    t.Field("turnSpeed", FieldType::Float, &PlayerController::turnSpeed, "转向速度", 1.0f, 40.0f);
    t.Field("height", FieldType::Float, &PlayerController::height, "离地高度", 0.0f, 5.0f);
}

void RegisterAgentComponents() {
    Reflection::Register<NavAgentComponent>("Nav Agent");
    Reflection::Register<PlayerController>("Player Controller");
}

} // namespace erm
