// ErMao Engine - 蒙皮演示关卡（GAMES104 第 8/9 讲）
//
// 资产全部由代码生成：
//   * 6 根骨骼的链（每段 0.5m），沿 +Y 方向
//   * 管状蒙皮网格：每个环的顶点按"离关节的距离"权重分配给相邻两根骨骼
//   * 两个动画片段：wave（绕 Z 摆动）与 sway（绕 X 扭动 + 根旋转）
// 演示点：线性混合蒙皮（LBS）、关键帧采样、姿势混合（cross-fade）、蒙皮矩阵 P*B^{-1}
#include "game/SkinningDemo.h"

#include "core/Log.h"
#include "game/SkinnedCharacter.h"
#include "renderer/RenderScene.h"
#include "renderer/Renderer.h"
#include "renderer/SkinnedMesh.h"
#include "scene/Components.h"
#include "scene/World.h"

#include <cmath>

namespace erm::game {

namespace {

constexpr int   kSegments = 6;
constexpr float kSegmentLength = 0.5f;
constexpr int   kSides = 12;

Skeleton BuildChainSkeleton() {
    Skeleton skeleton;
    for (int i = 0; i < kSegments; ++i) {
        BoneTransform local;
        local.translation = (i == 0) ? Vec3(0, 0, 0) : Vec3(0, kSegmentLength, 0);
        skeleton.AddBone("bone_" + std::to_string(i), i - 1, local);
    }
    skeleton.FinalizeBindPose();
    return skeleton;
}

// 顶点权重：离关节越近，两根骨骼越接近 50/50，弯折才平滑
void AssignWeights(float y, u32 outIndices[4], float outWeights[4]) {
    for (int i = 0; i < 4; ++i) {
        outIndices[i] = 0;
        outWeights[i] = 0.0f;
    }

    const float f = y / kSegmentLength;
    int         b = static_cast<int>(std::floor(f));
    b = (std::max)(0, (std::min)(b, kSegments - 1));
    const float t = Clamp(f - static_cast<float>(b), 0.0f, 1.0f);

    int   other = b;
    float w = 0.0f;
    if (t < 0.5f) {
        other = (std::max)(b - 1, 0);
        w = 0.5f * (1.0f - t / 0.5f); // t=0(关节处) -> 0.5，t=0.5 -> 0
    } else {
        other = (std::min)(b + 1, kSegments - 1);
        w = 0.5f * ((t - 0.5f) / 0.5f); // t=0.5 -> 0，t=1(关节处) -> 0.5
    }

    outIndices[0] = static_cast<u32>(b);
    outWeights[0] = 1.0f - w;
    outIndices[1] = static_cast<u32>(other);
    outWeights[1] = w;
}

SkinnedMeshData BuildTubeMesh(const Skeleton& skeleton) {
    (void)skeleton;
    SkinnedMeshData mesh;

    const float totalHeight = kSegmentLength * kSegments;
    const int   ringCount = kSegments + 1;

    for (int ring = 0; ring < ringCount; ++ring) {
        const float y = static_cast<float>(ring) * kSegmentLength;
        const float radius = 0.24f - 0.02f * static_cast<float>(ring);
        u32         indices[4];
        float       weights[4];
        AssignWeights(y, indices, weights);

        for (int s = 0; s < kSides; ++s) {
            const float angle = static_cast<float>(s) / kSides * kTwoPi;
            const Vec3  normal(std::cos(angle), 0.0f, std::sin(angle));

            SkinVertex vertex;
            vertex.position = normal * radius + Vec3(0, y, 0);
            vertex.normal = normal;
            vertex.uv = Vec2(static_cast<float>(s) / kSides, y / totalHeight);
            for (int k = 0; k < 4; ++k) {
                vertex.boneIndices[k] = indices[k];
                vertex.boneWeights[k] = weights[k];
            }
            mesh.vertices.push_back(vertex);
        }
    }

    for (int ring = 0; ring + 1 < ringCount; ++ring) {
        for (int s = 0; s < kSides; ++s) {
            const int next = (s + 1) % kSides;
            const u32 a = static_cast<u32>(ring * kSides + s);
            const u32 b = static_cast<u32>(ring * kSides + next);
            const u32 c = static_cast<u32>((ring + 1) * kSides + s);
            const u32 d = static_cast<u32>((ring + 1) * kSides + next);
            mesh.indices.push_back(a);
            mesh.indices.push_back(c);
            mesh.indices.push_back(b);
            mesh.indices.push_back(b);
            mesh.indices.push_back(c);
            mesh.indices.push_back(d);
        }
    }

    mesh.ComputeBounds();
    return mesh;
}

SkeletonClip BuildWaveClip(const Skeleton& skeleton, float duration) {
    SkeletonClip clip;
    clip.name = "wave";
    clip.duration = duration;
    clip.loop = true;

    const int keyCount = 9;
    for (int b = 1; b < skeleton.BoneCount(); ++b) {
        BoneTrack track;
        track.bone = b;
        for (int k = 0; k <= keyCount; ++k) {
            const float t = duration * static_cast<float>(k) / keyCount;
            const float phase = static_cast<float>(k) / keyCount * kTwoPi;
            BoneKey     key;
            key.time = t;
            key.value.translation = skeleton.bones[b].bindLocal.translation;
            key.value.rotation = Quat::FromEuler(0.0f, 0.0f, std::sin(phase + b * 0.7f) * 0.22f);
            key.value.scale = Vec3(1, 1, 1);
            track.keys.push_back(key);
        }
        clip.tracks.push_back(std::move(track));
    }
    return clip;
}

SkeletonClip BuildSwayClip(const Skeleton& skeleton, float duration) {
    SkeletonClip clip;
    clip.name = "sway";
    clip.duration = duration;
    clip.loop = true;

    const int keyCount = 7;
    for (int b = 0; b < skeleton.BoneCount(); ++b) {
        BoneTrack track;
        track.bone = b;
        for (int k = 0; k <= keyCount; ++k) {
            const float t = duration * static_cast<float>(k) / keyCount;
            const float phase = static_cast<float>(k) / keyCount * kTwoPi;
            BoneKey     key;
            key.time = t;
            key.value.translation = skeleton.bones[b].bindLocal.translation;
            if (b == 0) {
                key.value.rotation = Quat::FromEuler(std::sin(phase) * 0.25f, 0.0f, 0.0f);
            } else {
                key.value.rotation =
                    Quat::FromEuler(std::sin(phase + b * 0.5f) * 0.10f, 0.0f, std::sin(phase * 0.5f) * 0.06f);
            }
            key.value.scale = Vec3(1, 1, 1);
            track.keys.push_back(key);
        }
        clip.tracks.push_back(std::move(track));
    }
    return clip;
}

void SpawnCharacter(World& world, const std::string& name, const Vec3& position, const Skeleton& skeleton,
                    const SkinnedMesh* mesh, const SkeletonClip& clipA, const SkeletonClip& clipB, float phase,
                    float blend, const Vec4& color) {
    GameObject* object = world.CreateGameObject(name);
    object->SetPosition(position);
    auto* character = object->AddComponent<SkinnedCharacterComponent>();
    character->mesh = mesh;
    character->skeleton = skeleton;
    character->clipA = clipA;
    character->clipB = clipB;
    character->phase = phase;
    character->blend = blend;
    character->albedo = color;
}

} // namespace

void BuildSkinningLevel(World& world, Renderer& renderer) {
    world.Clear();

    GameObject* sun = world.CreateGameObject("太阳");
    sun->SetRotation(Quat::FromEuler(ToRadians(-42.0f), ToRadians(-35.0f), 0.0f));
    auto* light = sun->AddComponent<DirectionalLightComponent>();
    light->color = Vec4(1.0f, 0.95f, 0.86f, 1.0f);
    light->intensity = 2.4f;

    world.ambientUp = Vec3(0.22f, 0.30f, 0.48f);
    world.ambientDown = Vec3(0.05f, 0.05f, 0.06f);
    world.ambientIntensity = 0.65f;

    GameObject* cameraObject = world.CreateGameObject("主相机");
    cameraObject->SetPosition(Vec3(0.0f, 2.6f, 6.5f));
    cameraObject->SetRotationEulerDegrees(Vec3(-12.0f, 0.0f, 0.0f));
    auto* camera = cameraObject->AddComponent<CameraComponent>();
    camera->fovDegrees = 50.0f;
    camera->primary = true;

    GameObject* ground = world.CreateGameObject("地面");
    ground->SetScale(Vec3(30, 1, 30));
    auto* groundRenderer = ground->AddComponent<MeshRenderer>();
    groundRenderer->mesh = "plane";
    groundRenderer->albedo = Vec4(0.72f, 0.74f, 0.78f, 1.0f);
    groundRenderer->texture = "grid";
    groundRenderer->uvScale = 15.0f;
    auto* groundBody = ground->AddComponent<RigidBodyComponent>();
    groundBody->shape = static_cast<int>(RigidBodyComponent::Shape::Box);
    groundBody->halfExtents = Vec3(15, 0.5f, 15);
    groundBody->isStatic = true;

    // ---- 生成资产（骨架 + 蒙皮网格 + 两个片段）
    const Skeleton skeleton = BuildChainSkeleton();
    const SkinnedMeshData meshData = BuildTubeMesh(skeleton);

    const SkinnedMesh* skinnedMesh = renderer.CreateSkinnedMesh(meshData);
    if (!skinnedMesh) {
        LogError("蒙皮演示：网格创建失败");
        return;
    }

    const SkeletonClip wave = BuildWaveClip(skeleton, 2.0f);
    const SkeletonClip sway = BuildSwayClip(skeleton, 3.0f);

    // ---- 放 4 个角色：不同相位 / 不同混合权重
    SpawnCharacter(world, "蒙皮角色_A", Vec3(-3.0f, 0.5f, 0.0f), skeleton, skinnedMesh, wave, sway, 0.0f, 0.0f,
                   Vec4(0.55f, 0.62f, 0.80f, 1.0f));
    SpawnCharacter(world, "蒙皮角色_B", Vec3(-1.0f, 0.5f, 0.0f), skeleton, skinnedMesh, wave, sway, 0.7f, 0.35f,
                   Vec4(0.78f, 0.55f, 0.45f, 1.0f));
    SpawnCharacter(world, "蒙皮角色_C", Vec3(1.0f, 0.5f, 0.0f), skeleton, skinnedMesh, wave, sway, 1.4f, 0.70f,
                   Vec4(0.50f, 0.72f, 0.55f, 1.0f));
    SpawnCharacter(world, "蒙皮角色_D", Vec3(3.0f, 0.5f, 0.0f), skeleton, skinnedMesh, wave, sway, 2.1f, 1.0f,
                   Vec4(0.72f, 0.60f, 0.80f, 1.0f));

    world.RebuildNavigation();
    LogInfo("蒙皮演示关卡构建完成：%zu 个对象，%d 根骨骼，%zu 顶点", world.ObjectCount(), skeleton.BoneCount(),
            meshData.vertices.size());
}

void CollectSkinnedDraws(World& world, RenderScene& scene) {
    scene.skinned.clear(); // scene 是复用的，必须先清空（否则每帧累加）
    for (GameObject* root : world.RootObjects()) {
        root->ForEach([&scene](GameObject* object) {
            auto* character = object->GetComponent<SkinnedCharacterComponent>();
            if (!character || !character->mesh) return;
            SkinnedDraw draw;
            character->FillDraw(object->WorldMatrix(), draw);
            if (draw.mesh) scene.skinned.push_back(std::move(draw));
        });
    }
}

} // namespace erm::game
