// ErMao Engine - 关卡实现（代码优先）
//
// 关卡就是一个普通 C++ 函数：用一组小工具把物体、碰撞、灯光、AI 摆进去。
// 好处：可 diff、可参数化、可被自动化测试/程序化生成复用。
// 编辑器只是"看得见、改得动"的辅助，不是关卡的唯一来源。
#include "game/Levels.h"
#include "ai/Agents.h"
#include "animation/Animation.h"
#include "core/Log.h"
#include "game/ThirdPersonController.h"
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

GameObject* SpawnStaticBox(World& world, const std::string& name, const Vec3& position, const Vec3& scale,
                           const Vec4& color, const char* texture = nullptr, float uvScale = 0.0f) {
    GameObject* object = SpawnMesh(world, name, "cube", position, scale, color, texture, uvScale, 0.7f);
    auto* body = object->AddComponent<RigidBodyComponent>();
    body->shape = static_cast<int>(RigidBodyComponent::Shape::Box);
    body->halfExtents = scale * 0.5f;
    body->isStatic = true;
    return object;
}

void SpawnLamp(World& world, const std::string& name, const Vec3& position, const Vec4& color, float intensity,
               float range) {
    GameObject* light = world.CreateGameObject(name);
    light->SetPosition(position);
    auto* point = light->AddComponent<PointLightComponent>();
    point->color = color;
    point->intensity = intensity;
    point->range = range;

    GameObject* bulb = world.CreateGameObject(name + "_灯泡");
    bulb->SetPosition(position);
    bulb->SetScale(Vec3(0.22f, 0.22f, 0.22f));
    auto* renderer = bulb->AddComponent<MeshRenderer>();
    renderer->mesh = "sphere";
    renderer->albedo = color;
    renderer->emissive = 3.0f;
}

void SpawnAi(World& world, const std::string& name, const Vec3& position, float patrolRadius) {
    GameObject* agent = world.CreateGameObject(name);
    agent->SetPosition(position);
    agent->SetScale(Vec3(0.85f, 0.85f, 0.85f));
    auto* renderer = agent->AddComponent<MeshRenderer>();
    renderer->mesh = "capsule";
    renderer->albedo = Vec4(0.80f, 0.26f, 0.28f, 1.0f);
    renderer->roughness = 0.5f;
    auto* nav = agent->AddComponent<NavAgentComponent>();
    nav->patrolCenter = position;
    nav->patrolRadius = patrolRadius;
    nav->moveSpeed = 2.3f;
    nav->sightRange = 12.0f;
}

// 用外部 OBJ（assets/models/crystal.obj）做装饰，顺便验证资源链路
void SpawnCrystals(World& world, const Vec3& base, int count, float radius) {
    for (int i = 0; i < count; ++i) {
        const float angle = static_cast<float>(i) / count * kTwoPi;
        const Vec3  offset{std::cos(angle) * radius, 0.0f, std::sin(angle) * radius};
        const float scale = 0.5f + 0.25f * static_cast<float>(i % 3);
        GameObject* crystal = SpawnMesh(world, "水晶_" + std::to_string(i), "models/crystal.obj",
                                        base + offset + Vec3(0, 0.9f + 0.3f * (i % 2), 0),
                                        Vec3(scale, scale * 1.6f, scale), Vec4(0.35f, 0.75f, 0.95f, 1.0f), nullptr,
                                        0.0f, 0.15f);
        if (auto* renderer = crystal->GetComponent<MeshRenderer>()) renderer->emissive = 0.8f;
        auto* spinner = crystal->AddComponent<SpinnerComponent>();
        spinner->axis = Vec3(0.0f, 1.0f, 0.0f);
        spinner->degreesPerSecond = 30.0f + i * 12.0f;
    }
}

} // namespace

void BuildThirdPersonLevel(World& world) {
    world.Clear();

    // ---------------------------------------------------------------- 光照
    GameObject* sunObject = world.CreateGameObject("太阳");
    sunObject->SetRotation(Quat::FromEuler(ToRadians(-38.0f), ToRadians(-32.0f), 0.0f));
    auto* sun = sunObject->AddComponent<DirectionalLightComponent>();
    sun->color = Vec4(1.0f, 0.94f, 0.84f, 1.0f);
    sun->intensity = 2.0f;

    world.ambientUp = Vec3(0.20f, 0.32f, 0.56f);
    world.ambientDown = Vec3(0.05f, 0.05f, 0.055f);
    world.ambientIntensity = 0.60f;

    // ---------------------------------------------------------------- 相机（由控制器驱动）
    GameObject* cameraObject = world.CreateGameObject("主相机");
    cameraObject->SetPosition(Vec3(0.0f, 3.0f, 16.0f));
    cameraObject->SetRotationEulerDegrees(Vec3(-12.0f, 0.0f, 0.0f));
    auto* camera = cameraObject->AddComponent<CameraComponent>();
    camera->fovDegrees = 55.0f;
    camera->primary = true;

    // ---------------------------------------------------------------- 地面
    SpawnMesh(world, "地面", "plane", Vec3(0, 0, 0), Vec3(70, 1, 70), Vec4(0.78f, 0.80f, 0.84f, 1.0f), "grid", 35.0f,
              0.9f);

    // ---------------------------------------------------------------- 起点台阶
    SpawnStaticBox(world, "台阶_0", Vec3(0.0f, 0.15f, 9.4f), Vec3(6.0f, 0.3f, 1.2f), Vec4(0.42f, 0.44f, 0.48f, 1));
    SpawnStaticBox(world, "台阶_1", Vec3(0.0f, 0.35f, 8.4f), Vec3(6.0f, 0.7f, 1.2f), Vec4(0.45f, 0.47f, 0.51f, 1));
    SpawnStaticBox(world, "台阶_2", Vec3(0.0f, 0.55f, 7.4f), Vec3(6.0f, 1.1f, 1.2f), Vec4(0.48f, 0.50f, 0.54f, 1));

    // ---------------------------------------------------------------- 主平台链（需要跳跃）
    SpawnStaticBox(world, "平台A", Vec3(0.0f, 0.6f, 1.0f), Vec3(8.0f, 1.2f, 8.0f), Vec4(0.40f, 0.44f, 0.52f, 1));
    SpawnStaticBox(world, "平台B", Vec3(0.0f, 1.6f, -8.5f), Vec3(6.0f, 3.2f, 5.0f), Vec4(0.36f, 0.42f, 0.50f, 1));
    SpawnStaticBox(world, "平台C", Vec3(0.0f, 2.6f, -17.0f), Vec3(5.0f, 5.2f, 5.0f), Vec4(0.33f, 0.40f, 0.48f, 1));

    // 侧面长廊 + 终点
    SpawnStaticBox(world, "空中长廊", Vec3(7.0f, 5.05f, -17.0f), Vec3(9.0f, 0.3f, 3.0f), Vec4(0.40f, 0.37f, 0.33f, 1),
                   "noise", 3.0f);
    SpawnStaticBox(world, "终点台", Vec3(13.0f, 4.8f, -17.0f), Vec3(4.0f, 1.2f, 6.0f), Vec4(0.43f, 0.40f, 0.35f, 1),
                   "noise", 2.0f);

    GameObject* goal = SpawnMesh(world, "终点光环", "torus", Vec3(13.0f, 6.4f, -17.0f), Vec3(1.4f, 1.4f, 1.4f),
                                 Vec4(1.0f, 0.72f, 0.26f, 1.0f), nullptr, 0.0f, 0.2f);
    if (auto* goalRenderer = goal->GetComponent<MeshRenderer>()) goalRenderer->emissive = 1.8f;
    auto* goalSpinner = goal->AddComponent<SpinnerComponent>();
    goalSpinner->axis = Vec3(0.0f, 1.0f, 0.0f);
    goalSpinner->degreesPerSecond = 60.0f;

    // ---------------------------------------------------------------- 障碍与掩体（也用于演示相机弹簧臂）
    SpawnStaticBox(world, "立柱_0", Vec3(-4.6f, 4.0f, -4.0f), Vec3(1.2f, 8.0f, 1.2f), Vec4(0.46f, 0.40f, 0.33f, 1),
                   "noise", 2.0f);
    SpawnStaticBox(world, "立柱_1", Vec3(4.6f, 4.0f, -12.0f), Vec3(1.2f, 8.0f, 1.2f), Vec4(0.46f, 0.40f, 0.33f, 1),
                   "noise", 2.0f);
    SpawnStaticBox(world, "矮墙", Vec3(-9.0f, 0.8f, 2.0f), Vec3(0.5f, 1.6f, 10.0f), Vec4(0.44f, 0.46f, 0.50f, 1));

    // 地面木箱（AI 会绕行）
    SpawnStaticBox(world, "木箱_0", Vec3(-7.0f, 0.5f, -3.0f), Vec3(1.0f, 1.0f, 1.0f), Vec4(0.55f, 0.33f, 0.21f, 1),
                   "noise", 1.0f);
    SpawnStaticBox(world, "木箱_1", Vec3(-6.2f, 1.5f, -3.2f), Vec3(0.9f, 0.9f, 0.9f), Vec4(0.58f, 0.36f, 0.23f, 1),
                   "noise", 1.0f);
    SpawnStaticBox(world, "木箱_2", Vec3(8.5f, 0.5f, 1.5f), Vec3(1.1f, 1.0f, 1.1f), Vec4(0.32f, 0.40f, 0.31f, 1),
                   "noise", 1.0f);

    // 油桶
    for (int i = 0; i < 3; ++i) {
        SpawnMesh(world, "油桶_" + std::to_string(i), "cylinder", Vec3(4.5f + i * 0.9f, 0.55f, 5.0f - i * 0.4f),
                  Vec3(0.7f, 1.1f, 0.7f), Vec4(0.28f, 0.38f, 0.46f, 1.0f), "noise", 1.0f, 0.45f);
    }

    // 外部 OBJ 资产：终点台周围的悬浮水晶
    SpawnCrystals(world, Vec3(13.0f, 7.2f, -17.0f), 5, 2.4f);

    // ---------------------------------------------------------------- 灯光
    SpawnLamp(world, "路灯_0", Vec3(-4.0f, 3.2f, 5.0f), Vec4(1.0f, 0.62f, 0.30f, 1), 7.0f, 9.0f);
    SpawnLamp(world, "路灯_1", Vec3(10.5f, 3.2f, -13.0f), Vec4(0.40f, 0.70f, 1.0f, 1), 6.0f, 8.0f);

    // ---------------------------------------------------------------- 玩家
    GameObject* player = world.CreateGameObject("玩家");
    player->SetPosition(Vec3(0.0f, 1.0f, 12.5f));
    player->SetScale(Vec3(0.9f, 0.95f, 0.9f));
    auto* playerRenderer = player->AddComponent<MeshRenderer>();
    playerRenderer->mesh = "capsule";
    playerRenderer->albedo = Vec4(0.24f, 0.52f, 0.95f, 1.0f);
    playerRenderer->roughness = 0.4f;
    auto* controller = player->AddComponent<ThirdPersonController>();
    controller->height = 1.9f;
    controller->radius = 0.42f;
    controller->cameraName = "主相机";
    controller->cameraDistance = 6.2f;
    controller->pitchDegrees = -14.0f;

    // ---------------------------------------------------------------- AI
    SpawnAi(world, "巡逻者_0", Vec3(-8.0f, 1.0f, -8.0f), 4.0f);
    SpawnAi(world, "巡逻者_1", Vec3(7.0f, 1.0f, -4.0f), 3.5f);
    SpawnAi(world, "巡逻者_2", Vec3(-3.0f, 1.0f, -13.0f), 3.0f);

    // ---------------------------------------------------------------- 装饰：风车
    GameObject* rotor = world.CreateGameObject("风车");
    rotor->SetPosition(Vec3(-11.0f, 3.0f, -1.0f));
    auto* animator = rotor->AddComponent<AnimatorComponent>();
    animator->clip = "windmill";
    animator->target = "叶片";
    GameObject* blade = world.CreateGameObject("叶片", rotor);
    blade->SetScale(Vec3(0.15f, 0.15f, 3.0f));
    auto* bladeRenderer = blade->AddComponent<MeshRenderer>();
    bladeRenderer->mesh = "cube";
    bladeRenderer->albedo = Vec4(0.66f, 0.58f, 0.34f, 1.0f);

    world.RebuildNavigation();
    LogInfo("第三人称关卡构建完成：%zu 个对象", world.ObjectCount());
}

void BuildDemoScene(World& world) { world.CreateDemoScene(); }

} // namespace erm::game
