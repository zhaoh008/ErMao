// ErMao Engine - AI 智能体与玩家控制器（GAMES104 第 15/16 讲）
#pragma once

#include "core/Math.h"
#include "scene/Component.h"

namespace erm {

class World;

// 巡逻/追击状态机 + A* 寻路 + 感知（距离/FOV/视线）
class NavAgentComponent : public Component {
public:
    ER_COMPONENT(NavAgentComponent, "Nav Agent")

    float moveSpeed = 2.2f;
    float turnSpeed = 8.0f;
    Vec3  patrolCenter{0.0f, 0.0f, 0.0f};
    float patrolRadius = 7.0f;
    float sightRange = 13.0f;
    float sightAngleDegrees = 75.0f;
    float repathInterval = 0.6f;
    int   state = 0; // 0=巡逻 1=追击（运行时状态，Inspector 可见）

    // 运行时：当前路径（Application 用来画调试线）
    std::vector<Vec3> path;
    size_t            pathIndex = 0;
    float             repathTimer = 0.0f;
    float             waitTimer = 0.0f;

    void OnStart() override;
    void OnUpdate(float dt) override;

private:
    bool  CanSeePlayer(World* world, Vec3& playerPos) const;
    void  RequestPath(World* world, const Vec3& goal);
    class GameObject* FindPlayer(World* world) const;
};

// 玩家：WASD 相对相机移动，Shift 加速；角色朝向平滑转向移动方向
class PlayerController : public Component {
public:
    ER_COMPONENT(PlayerController, "Player Controller")

    float moveSpeed = 4.2f;
    float runMultiplier = 1.8f;
    float turnSpeed = 12.0f;
    float height = 1.0f;

    void OnUpdate(float dt) override;
};

// 触发一次所有组件的反射注册（反序列化前必须调用）
void RegisterAgentComponents();

} // namespace erm
