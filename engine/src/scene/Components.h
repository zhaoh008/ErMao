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
    bool IsCosmetic() const override { return true; }
};

// 触发全部内置组件的反射注册（读档前调用，保证按类型名能找到工厂）
void RegisterAllComponents();


// ---------------------------------------------------------------- 2D 精灵（第 23 讲）
// 2D 渲染和 3D 走同一条 RenderScene 通道：这里只负责提供"纹理 + 目标矩形 + uv"。
class SpriteRendererComponent : public Component {
public:
    ER_COMPONENT(SpriteRendererComponent, "Sprite Renderer")

    std::string texture = "sprites/soldier_idle.png"; // assets/ 下的相对路径
    Vec4        uv{0.0f, 0.0f, 1.0f, 1.0f};          // u0, v0, u1, v1
    Vec2        size{1.2f, 1.2f};                     // 世界单位
    Vec2        offset{0.0f, 0.0f};
    Vec4        color{1.0f, 1.0f, 1.0f, 1.0f};
    bool        flipX = false;
    int         layer = 0;                            // 越小越先画
    float       rotation = 0.0f;                      // 绕中心旋转（弧度，2D）

    // 2D 朝向约定：角色/怪物贴图统一面朝 +X（右）。移动时只调用本函数翻转，
    // 不要在玩法代码里各自猜素材默认朝向；素材导入阶段负责把所有角色/怪物图镜像成朝右。
    void FaceMovementX(float movementX) {
        if (movementX > 1e-4f) {
            flipX = false;
        } else if (movementX < -1e-4f) {
            flipX = true;
        }
    }
};

// 2D 精灵动画：把一张横向排列的序列帧贴图按 fps 播放
class SpriteAnimatorComponent : public Component {
public:
    ER_COMPONENT(SpriteAnimatorComponent, "Sprite Animator")

    float fps = 10.0f;
    bool  playing = true;
    bool  loop = true;
    int   columnsOverride = 0; // 0 = 按贴图宽高自动推断（正方形帧）
    // 序列帧通常有透明留白：用帧内子矩形裁掉空白（归一化到单帧 0..1）
    Vec2  frameMin{0.0f, 0.0f};
    Vec2  frameMax{1.0f, 1.0f};

    void OnUpdate(float dt) override;
    bool IsCosmetic() const override { return true; }
    void Play(float newFps, bool newLoop = true) {
        fps = newFps;
        loop = newLoop;
        m_time = 0.0f;
        playing = true;
    }
    int CurrentFrame() const { return m_frame; }

private:
    float m_time = 0.0f;
    int   m_frame = 0;
};

} // namespace erm
