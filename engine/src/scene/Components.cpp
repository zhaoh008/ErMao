// ErMao Engine - 内置组件实现（含反射描述）
#include "scene/Components.h"
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
}

} // namespace erm
