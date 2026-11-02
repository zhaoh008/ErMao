// ErMao Engine - 蒙皮角色组件实现
#include "game/SkinnedCharacter.h"

#include "renderer/RenderScene.h"
#include "renderer/SkinnedMesh.h"
#include "scene/GameObject.h"

#include <cmath>

namespace erm {

void SkinnedCharacterComponent::OnUpdate(float dt) {
    if (!m_owner || skeleton.bones.empty()) return;

    m_timeA += dt * speedA;
    m_timeB += dt * speedB;

    clipA.Sample(m_timeA + phase, skeleton, m_poseA);
    clipB.Sample(m_timeB + phase, skeleton, m_poseB);
    BlendPoses(m_poseA, m_poseB, Clamp01(blend), m_pose);

    skeleton.ComputeWorld(m_pose, m_worldMatrices);
    skeleton.ComputeSkinning(m_worldMatrices, m_bones);
}

void SkinnedCharacterComponent::FillDraw(const Mat4& world, SkinnedDraw& out) const {
    out.mesh = mesh;
    out.world = world;
    out.bones = m_bones;
    out.albedo = albedo;
    out.roughness = roughness;
    out.metallic = metallic;
}

void SkinnedCharacterComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<SkinnedCharacterComponent>(); };
    t.Field("blend", FieldType::Float, &SkinnedCharacterComponent::blend, "混合权重 A→B", 0.0f, 1.0f);
    t.Field("speedA", FieldType::Float, &SkinnedCharacterComponent::speedA, "片段 A 速度", 0.0f, 4.0f);
    t.Field("speedB", FieldType::Float, &SkinnedCharacterComponent::speedB, "片段 B 速度", 0.0f, 4.0f);
    t.Field("phase", FieldType::Float, &SkinnedCharacterComponent::phase, "相位", -6.28f, 6.28f);
    t.Field("albedo", FieldType::Color, &SkinnedCharacterComponent::albedo, "基础色");
    t.Field("roughness", FieldType::Float, &SkinnedCharacterComponent::roughness, "粗糙度", 0.04f, 1.0f);
}

} // namespace erm
