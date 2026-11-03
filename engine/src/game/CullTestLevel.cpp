// ErMao Engine - GPU 剔除压测关卡（GAMES104 第 22 讲）
//
// 80 x 80 = 6400 个立方体，全部共用 "cube" + "noise"，因此在 CPU 侧是 **一个批次**。
// 用途：对比 "CPU 全量提交" 与 "GPU compute 剔除 + 间接绘制" 的实例数差异。
#include "game/Levels.h"
#include "core/Log.h"
#include "scene/Components.h"
#include "scene/World.h"

namespace erm::game {

void BuildCullTestLevel(World& world) {
    world.Clear();

    GameObject* sun = world.CreateGameObject("太阳");
    sun->SetRotation(Quat::FromEuler(ToRadians(-40.0f), ToRadians(-35.0f), 0.0f));
    auto* light = sun->AddComponent<DirectionalLightComponent>();
    light->color = Vec4(1.0f, 0.95f, 0.86f, 1.0f);
    light->intensity = 2.2f;

    world.ambientUp = Vec3(0.22f, 0.30f, 0.48f);
    world.ambientDown = Vec3(0.05f, 0.05f, 0.06f);
    world.ambientIntensity = 0.6f;

    GameObject* cameraObject = world.CreateGameObject("主相机");
    cameraObject->SetPosition(Vec3(0.0f, 8.0f, 22.0f));
    cameraObject->SetRotationEulerDegrees(Vec3(-10.0f, 0.0f, 0.0f));
    auto* camera = cameraObject->AddComponent<CameraComponent>();
    camera->fovDegrees = 55.0f;
    camera->primary = true;

    GameObject* ground = world.CreateGameObject("地面");
    ground->SetScale(Vec3(120, 1, 120));
    auto* groundRenderer = ground->AddComponent<MeshRenderer>();
    groundRenderer->mesh = "plane";
    groundRenderer->albedo = Vec4(0.55f, 0.57f, 0.62f, 1.0f);
    groundRenderer->texture = "grid";
    groundRenderer->uvScale = 60.0f;

    // 6400 个立方体：同一个网格 + 同一张贴图 -> BuildBatches 会合成一个批次
    int index = 0;
    for (int z = 0; z < 80; ++z) {
        for (int x = 0; x < 80; ++x) {
            GameObject* cube = world.CreateGameObject("cube_" + std::to_string(index++));
            cube->SetPosition(Vec3(static_cast<float>(x) * 1.6f - 64.0f, 1.0f, static_cast<float>(-z) * 1.6f - 2.0f));
            cube->SetScale(Vec3(0.8f, 0.8f, 0.8f));
            auto* renderer = cube->AddComponent<MeshRenderer>();
            renderer->mesh = "cube";
            renderer->texture = "noise";
            renderer->uvScale = 1.0f;
            const float shade = 0.35f + 0.5f * static_cast<float>((x * 7 + z * 13) % 17) / 17.0f;
            renderer->albedo = Vec4(shade, shade * 0.9f, shade * 0.8f, 1.0f);
            renderer->roughness = 0.6f;
        }
    }

    LogInfo("GPU 剔除压测关卡：%zu 个对象（6400 个同批次立方体）", world.ObjectCount());
}

} // namespace erm::game