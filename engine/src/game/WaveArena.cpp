// ErMao Engine - 刷怪 Demo 的竞技场关卡（代码优先）
#include "game/Levels.h"
#include "core/Log.h"
#include "game/ThirdPersonController.h"
#include "game/ParticleEmitter.h"
#include "game/WaveGame.h"
#include "scene/Components.h"
#include "scene/World.h"

namespace erm::game {

namespace {

GameObject* SpawnMesh(World& world, const std::string& name, const std::string& mesh, const Vec3& position,
                      const Vec3& scale, const Vec4& color, const char* texture = nullptr, float uvScale = 0.0f,
                      float roughness = 0.6f) {
    GameObject* object = world.CreateGameObject(name);
    object->SetPosition(position);
    object->SetScale(scale);
    auto* renderer = object->AddComponent<MeshRenderer>();
    renderer->mesh = mesh;
    renderer->albedo = color;
    renderer->texture = texture ? texture : "";
    renderer->uvScale = uvScale;
    renderer->roughness = roughness;
    return object;
}

GameObject* SpawnBlock(World& world, const std::string& name, const Vec3& position, const Vec3& scale,
                       const Vec4& color, const char* texture = nullptr, float uvScale = 0.0f) {
    GameObject* object = SpawnMesh(world, name, "cube", position, scale, color, texture, uvScale, 0.7f);
    auto* body = object->AddComponent<RigidBodyComponent>();
    body->shape = static_cast<int>(RigidBodyComponent::Shape::Box);
    body->halfExtents = scale * 0.5f;
    body->isStatic = true;
    return object;
}

} // namespace

void BuildWaveArena(World& world) {
    world.Clear();

    // ------------------------------------------------ 光照
    GameObject* sun = world.CreateGameObject("太阳");
    sun->SetRotation(Quat::FromEuler(ToRadians(-46.0f), ToRadians(-38.0f), 0.0f));
    auto* light = sun->AddComponent<DirectionalLightComponent>();
    light->color = Vec4(1.0f, 0.94f, 0.85f, 1.0f);
    light->intensity = 2.2f;

    world.ambientUp = Vec3(0.20f, 0.30f, 0.52f);
    world.ambientDown = Vec3(0.05f, 0.05f, 0.055f);
    world.ambientIntensity = 0.62f;

    // ------------------------------------------------ 相机
    GameObject* cameraObject = world.CreateGameObject("主相机");
    cameraObject->SetPosition(Vec3(0.0f, 3.0f, 8.0f));
    cameraObject->SetRotationEulerDegrees(Vec3(-14.0f, 0.0f, 0.0f));
    auto* camera = cameraObject->AddComponent<CameraComponent>();
    camera->fovDegrees = 55.0f;
    camera->primary = true;

    // ------------------------------------------------ 竞技场
    SpawnMesh(world, "地面", "plane", Vec3(0, 0, 0), Vec3(52, 1, 52), Vec4(0.80f, 0.82f, 0.86f, 1.0f), "grid", 26.0f,
              0.9f);

    // 四周高墙（也是敌人不会跑出场的保证）
    const Vec4 wallColor(0.42f, 0.45f, 0.52f, 1.0f);
    SpawnBlock(world, "北墙", Vec3(0, 2.5f, -26), Vec3(52, 5, 1.5f), wallColor, "noise", 8.0f);
    SpawnBlock(world, "南墙", Vec3(0, 2.5f, 26), Vec3(52, 5, 1.5f), wallColor, "noise", 8.0f);
    SpawnBlock(world, "西墙", Vec3(-26, 2.5f, 0), Vec3(1.5f, 5, 52), wallColor, "noise", 8.0f);
    SpawnBlock(world, "东墙", Vec3(26, 2.5f, 0), Vec3(1.5f, 5, 52), wallColor, "noise", 8.0f);

    // 掩体（同时用于演示血量条被遮挡）
    SpawnBlock(world, "立柱_A", Vec3(-7, 2.0f, -7), Vec3(2, 4, 2), Vec4(0.38f, 0.42f, 0.50f, 1), "noise", 2.0f);
    SpawnBlock(world, "立柱_B", Vec3(7, 2.0f, -7), Vec3(2, 4, 2), Vec4(0.38f, 0.42f, 0.50f, 1), "noise", 2.0f);
    SpawnBlock(world, "立柱_C", Vec3(-7, 2.0f, 7), Vec3(2, 4, 2), Vec4(0.38f, 0.42f, 0.50f, 1), "noise", 2.0f);
    SpawnBlock(world, "立柱_D", Vec3(7, 2.0f, 7), Vec3(2, 4, 2), Vec4(0.38f, 0.42f, 0.50f, 1), "noise", 2.0f);
    SpawnBlock(world, "矮墙_A", Vec3(0, 0.6f, -13), Vec3(8, 1.2f, 1), Vec4(0.44f, 0.46f, 0.52f, 1), "noise", 2.0f);
    SpawnBlock(world, "矮墙_B", Vec3(0, 0.6f, 13), Vec3(8, 1.2f, 1), Vec4(0.44f, 0.46f, 0.52f, 1), "noise", 2.0f);
    SpawnBlock(world, "木箱_A", Vec3(-12, 0.5f, 2), Vec3(1, 1, 1), Vec4(0.55f, 0.36f, 0.22f, 1), "noise", 1.0f);
    SpawnBlock(world, "木箱_B", Vec3(12, 0.5f, -2), Vec3(1, 1, 1), Vec4(0.55f, 0.36f, 0.22f, 1), "noise", 1.0f);

    // ------------------------------------------------ 玩家
    GameObject* player = world.CreateGameObject("玩家");
    player->SetPosition(Vec3(0.0f, 1.0f, 0.0f));
    player->SetScale(Vec3(0.9f, 0.95f, 0.9f));

    auto* renderer = player->AddComponent<MeshRenderer>();
    renderer->mesh = "capsule";
    renderer->albedo = Vec4(0.24f, 0.52f, 0.95f, 1.0f);
    renderer->roughness = 0.4f;

    auto* controller = player->AddComponent<ThirdPersonController>();
    controller->height = 1.9f;
    controller->radius = 0.42f;
    controller->cameraDistance = 6.0f;
    controller->pitchDegrees = -12.0f;

    auto* health = player->AddComponent<HealthComponent>();
    health->maxHealth = 100.0f;
    health->health = 100.0f;
    health->isPlayer = true;
    health->barOffsetY = 1.9f;
    health->barWidth = 1.4f;
    health->color = Vec4(0.35f, 0.85f, 0.45f, 1.0f);
    health->showBar = false; // 玩家血量已经在 HUD 里，头顶不再重复

    auto* combat = player->AddComponent<CombatComponent>();
    combat->damage = 30.0f;
    combat->fireInterval = 0.18f;

    // ------------------------------------------------ 灯
    GameObject* lamp = world.CreateGameObject("场地灯");
    lamp->SetPosition(Vec3(0.0f, 6.0f, 0.0f));
    auto* point = lamp->AddComponent<PointLightComponent>();
    point->color = Vec4(0.55f, 0.70f, 1.0f, 1.0f);
    point->intensity = 4.0f;
    point->range = 22.0f;

    // ------------------------------------------------ 火把（粒子）
    auto addTorch = [&world](const std::string& name, const Vec3& position, const Vec4& color) {
        GameObject* torch = world.CreateGameObject(name);
        torch->SetPosition(position);
        auto* emitter = torch->AddComponent<ParticleEmitterComponent>();
        emitter->rate = 55.0f;
        emitter->lifetime = 1.1f;
        emitter->initialVelocity = Vec3(0.0f, 1.4f, 0.0f);
        emitter->spread = 0.3f;
        emitter->gravity = Vec3(0.0f, 1.2f, 0.0f);
        emitter->colorStart = color;
        emitter->colorEnd = Vec4(color.x * 0.4f, color.y * 0.2f, color.z * 0.1f, 0.0f);
        emitter->sizeStart = 0.30f;
        emitter->sizeEnd = 0.03f;
    };
    addTorch("火把_A", Vec3(-3.5f, 0.15f, -10.0f), Vec4(1.0f, 0.62f, 0.22f, 1.0f));
    addTorch("火把_B", Vec3(3.5f, 0.15f, -10.0f), Vec4(1.0f, 0.62f, 0.22f, 1.0f));
    addTorch("传送门粒子", Vec3(0.0f, 0.2f, -22.0f), Vec4(0.45f, 0.65f, 1.0f, 1.0f));

    world.RebuildNavigation();
    LogInfo("刷怪竞技场构建完成：%zu 个对象", world.ObjectCount());
}

} // namespace erm::game
