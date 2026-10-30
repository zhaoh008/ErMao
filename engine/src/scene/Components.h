// ErMao Engine - 内置组件（GAMES104 第 3 讲：用组件搭建游戏世界）
#pragma once

#include "renderer/Camera.h"
#include "scene/Component.h"

namespace erm {

// ---------------------------------------------------------------- 渲染
class MeshRenderer : public Component {
public:
    ER_COMPONENT(MeshRenderer, "Mesh Renderer")

    std::string mesh = "cube";
    Vec4        albedo{0.8f, 0.8f, 0.82f, 1.0f};
    float       roughness = 0.5f;
    float       metallic = 0.0f;
    float       emissive = 0.0f;
    float       uvScale = 0.0f;
    std::string texture;
    bool        castShadow = true;
};

class CameraComponent : public Component {
public:
    ER_COMPONENT(CameraComponent, "Camera")

    float  fovDegrees = 60.0f;
    float  nearZ = 0.1f;
    float  farZ = 500.0f;
    bool   primary = true;

    // 运行时相机（由 GameObject 的世界变换驱动）
    Camera camera;

    void SyncFromTransform();
};

// ---------------------------------------------------------------- 光照
class DirectionalLightComponent : public Component {
public:
    ER_COMPONENT(DirectionalLightComponent, "Directional Light")

    Vec4  color{1.0f, 0.95f, 0.88f, 1.0f};
    float intensity = 3.2f;
};

class PointLightComponent : public Component {
public:
    ER_COMPONENT(PointLightComponent, "Point Light")

    Vec4  color{1.0f, 0.7f, 0.4f, 1.0f};
    float intensity = 6.0f;
    float range = 8.0f;
};

// ---------------------------------------------------------------- 物理
class RigidBodyComponent : public Component {
public:
    ER_COMPONENT(RigidBodyComponent, "Rigid Body")

    enum class Shape { Sphere = 0, Box = 1 };

    int   shape = 0; // 0=球 1=盒，Inspector 用下拉框
    float radius = 0.5f;
    Vec3  halfExtents{0.5f, 0.5f, 0.5f};
    float mass = 1.0f;
    bool  isStatic = false;
    bool  useGravity = true;
    float restitution = 0.45f;
    float friction = 0.55f;
    Vec3  velocity{0.0f, 0.0f, 0.0f};

    // 运行时状态（不参与序列化）
    float sleepTimer = 0.0f;
    bool  grounded = false;
};

// ---------------------------------------------------------------- 玩法
// 简单脚本组件：绕轴自转，演示"组件即行为"
class SpinnerComponent : public Component {
public:
    ER_COMPONENT(SpinnerComponent, "Spinner")

    Vec3  axis{0.0f, 1.0f, 0.0f};
    float degreesPerSecond = 45.0f;

    void OnUpdate(float dt) override;
};

// 触发全部内置组件的反射注册（读档前调用，保证按类型名能找到工厂）
void RegisterAllComponents();

} // namespace erm
