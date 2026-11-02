// ErMao Engine - 内置组件实现（含反射描述）
#include "scene/Components.h"
#include "asset/AssetManager.h"
#include "scene/World.h"
#include "scene/GameObject.h"
#include "scene/World.h"

namespace erm {

// ---------------------------------------------------------------- 反射注册
TypeInfo* Reflection::Find(const std::string& name) {
    for (auto* info : All())
        if (info->name == name) return info;
    return nullptr;
}

// ---------------------------------------------------------------- Component
World* Component::GetWorld() const { return m_owner ? m_owner->GetWorld() : nullptr; }

// ---------------------------------------------------------------- Camera
void CameraComponent::SyncFromTransform() {
    if (!m_owner) return;
    camera.SetFromMatrix(m_owner->WorldMatrix());
    camera.fovY = ToRadians(fovDegrees);
    camera.nearZ = nearZ;
    camera.farZ = farZ;
}

// ---------------------------------------------------------------- Spinner
void SpinnerComponent::OnUpdate(float dt) {
    if (!m_owner) return;
    const Quat delta = Quat::FromAxisAngle(axis, ToRadians(degreesPerSecond) * dt);
    m_owner->SetRotation((delta * m_owner->Rotation()).Normalized());
}

// ---------------------------------------------------------------- Describe
void MeshRenderer::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<MeshRenderer>(); };
    t.Field("mesh", FieldType::MeshAsset, &MeshRenderer::mesh, "网格");
    t.Field("albedo", FieldType::Color, &MeshRenderer::albedo, "基础色");
    t.Field("roughness", FieldType::Float, &MeshRenderer::roughness, "粗糙度", 0.04f, 1.0f);
    t.Field("metallic", FieldType::Float, &MeshRenderer::metallic, "金属度", 0.0f, 1.0f);
    t.Field("emissive", FieldType::Float, &MeshRenderer::emissive, "自发光", 0.0f, 4.0f);
    t.Field("uvScale", FieldType::Float, &MeshRenderer::uvScale, "UV 缩放", 0.0f, 32.0f);
    t.Field("texture", FieldType::TextureAsset, &MeshRenderer::texture, "贴图");
    t.Field("castShadow", FieldType::Bool, &MeshRenderer::castShadow, "投射阴影");
}

void CameraComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<CameraComponent>(); };
    t.Field("fovDegrees", FieldType::Float, &CameraComponent::fovDegrees, "视场角", 20.0f, 120.0f);
    t.Field("nearZ", FieldType::Float, &CameraComponent::nearZ, "近平面", 0.01f, 10.0f);
    t.Field("farZ", FieldType::Float, &CameraComponent::farZ, "远平面", 10.0f, 2000.0f);
    t.Field("primary", FieldType::Bool, &CameraComponent::primary, "主相机");
}

void DirectionalLightComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<DirectionalLightComponent>(); };
    t.Field("color", FieldType::Color, &DirectionalLightComponent::color, "颜色");
    t.Field("intensity", FieldType::Float, &DirectionalLightComponent::intensity, "强度", 0.0f, 20.0f);
}

void PointLightComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<PointLightComponent>(); };
    t.Field("color", FieldType::Color, &PointLightComponent::color, "颜色");
    t.Field("intensity", FieldType::Float, &PointLightComponent::intensity, "强度", 0.0f, 50.0f);
    t.Field("range", FieldType::Float, &PointLightComponent::range, "半径", 0.1f, 50.0f);
}

void RigidBodyComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<RigidBodyComponent>(); };
    t.Field("shape", FieldType::Int, &RigidBodyComponent::shape, "形状 (0球/1盒)", 0.0f, 1.0f);
    t.Field("radius", FieldType::Float, &RigidBodyComponent::radius, "半径", 0.05f, 20.0f);
    t.Field("halfExtents", FieldType::Vec3, &RigidBodyComponent::halfExtents, "半尺寸");
    t.Field("mass", FieldType::Float, &RigidBodyComponent::mass, "质量", 0.01f, 100.0f);
    t.Field("isStatic", FieldType::Bool, &RigidBodyComponent::isStatic, "静态");
    t.Field("useGravity", FieldType::Bool, &RigidBodyComponent::useGravity, "重力");
    t.Field("restitution", FieldType::Float, &RigidBodyComponent::restitution, "弹性", 0.0f, 1.0f);
    t.Field("friction", FieldType::Float, &RigidBodyComponent::friction, "摩擦", 0.0f, 1.0f);
    t.Field("velocity", FieldType::Vec3, &RigidBodyComponent::velocity, "速度");
}

void SpinnerComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<SpinnerComponent>(); };
    t.Field("axis", FieldType::Vec3, &SpinnerComponent::axis, "旋转轴");
    t.Field("degreesPerSecond", FieldType::Float, &SpinnerComponent::degreesPerSecond, "角速度", -720.0f, 720.0f);
}

void RegisterAllComponents() {
    Reflection::Register<MeshRenderer>("Mesh Renderer");
    Reflection::Register<CameraComponent>("Camera");
    Reflection::Register<DirectionalLightComponent>("Directional Light");
    Reflection::Register<PointLightComponent>("Point Light");
    Reflection::Register<RigidBodyComponent>("Rigid Body");
    Reflection::Register<SpinnerComponent>("Spinner");
    Reflection::Register<SpriteRendererComponent>("Sprite Renderer");
    Reflection::Register<SpriteAnimatorComponent>("Sprite Animator");
}


void SpriteAnimatorComponent::OnUpdate(float dt) {
    if (!m_owner || !playing) return;
    auto* sprite = m_owner->GetComponent<SpriteRendererComponent>();
    if (!sprite) return;

    int columns = columnsOverride;
    if (columns <= 0) {
        Texture* texture = GetWorld() && GetWorld()->Assets() ? GetWorld()->Assets()->GetTextureAsset(sprite->texture)
                                                              : nullptr;
        columns = texture && texture->Height() > 0 ? static_cast<int>(texture->Width() / texture->Height()) : 1;
    }
    if (columns <= 0) return;

    m_time += dt;
    int frame = static_cast<int>(m_time * fps);
    if (loop) {
        frame %= columns;
    } else if (frame >= columns) {
        frame = columns - 1;
        playing = false;
    }
    m_frame = frame;

    const float width = 1.0f / static_cast<float>(columns);
    const float u0 = width * static_cast<float>(m_frame) + width * frameMin.x;
    const float u1 = width * static_cast<float>(m_frame) + width * frameMax.x;
    sprite->uv = Vec4(u0, frameMin.y, u1, frameMax.y);
}

void SpriteAnimatorComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<SpriteAnimatorComponent>(); };
    t.Field("fps", FieldType::Float, &SpriteAnimatorComponent::fps, "帧率", 0.1f, 60.0f);
    t.Field("playing", FieldType::Bool, &SpriteAnimatorComponent::playing, "播放");
    t.Field("loop", FieldType::Bool, &SpriteAnimatorComponent::loop, "循环");
    t.Field("frameMin", FieldType::Vec2, &SpriteAnimatorComponent::frameMin, "帧裁剪最小");
    t.Field("frameMax", FieldType::Vec2, &SpriteAnimatorComponent::frameMax, "帧裁剪最大");
    t.Field("columnsOverride", FieldType::Int, &SpriteAnimatorComponent::columnsOverride, "列数覆盖", 0.0f, 64.0f);
}

void SpriteRendererComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<SpriteRendererComponent>(); };
    t.Field("texture", FieldType::String, &SpriteRendererComponent::texture, "贴图（sprites/…）");
    t.Field("uv", FieldType::Vec4, &SpriteRendererComponent::uv, "UV 矩形");
    t.Field("size", FieldType::Vec2, &SpriteRendererComponent::size, "尺寸");
    t.Field("offset", FieldType::Vec2, &SpriteRendererComponent::offset, "偏移");
    t.Field("color", FieldType::Color, &SpriteRendererComponent::color, "颜色");
    t.Field("flipX", FieldType::Bool, &SpriteRendererComponent::flipX, "水平翻转");
    t.Field("layer", FieldType::Int, &SpriteRendererComponent::layer, "层级");
}
} // namespace erm
