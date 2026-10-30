// ErMao Engine - GameObject 实现
#include "scene/GameObject.h"
#include "scene/World.h"

namespace erm {

GameObject::GameObject(World* world, u64 id, std::string name)
    : m_world(world), m_id(id), m_name(std::move(name)) {}

GameObject::~GameObject() {
    for (auto& c : m_components) {
        if (c) c->OnDestroy();
    }
    m_components.clear();
}

void GameObject::SetParent(GameObject* parent) {
    if (parent == m_parent) return;
    if (parent == this) return;
    // 禁止把父节点挂到自己的子孙下面（会形成环）
    if (parent && parent->IsDescendantOf(this)) return;

    if (m_parent) {
        auto& siblings = m_parent->m_children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
    } else if (m_world) {
        auto& roots = m_world->m_roots;
        roots.erase(std::remove(roots.begin(), roots.end(), this), roots.end());
    }
    m_parent = parent;
    if (m_parent) {
        m_parent->m_children.push_back(this);
    } else if (m_world) {
        m_world->m_roots.push_back(this);
    }

    m_worldDirty = true;
    for (auto* child : m_children) child->MarkWorldDirty();
}

bool GameObject::IsDescendantOf(const GameObject* other) const {
    if (!other) return false;
    const GameObject* p = m_parent;
    while (p) {
        if (p == other) return true;
        p = p->m_parent;
    }
    return false;
}

void GameObject::SetPosition(const Vec3& p) {
    m_position = p;
    MarkWorldDirty();
}

void GameObject::SetRotation(const Quat& q) {
    m_rotation = q.Normalized();
    MarkWorldDirty();
}

void GameObject::SetRotationEulerDegrees(const Vec3& euler) {
    SetRotation(Quat::FromEuler(ToRadians(euler.y), ToRadians(euler.x), ToRadians(euler.z)));
}

void GameObject::SetScale(const Vec3& s) {
    m_scale = s;
    MarkWorldDirty();
}

void GameObject::MarkWorldDirty() {
    m_worldDirty = true;
    for (auto* child : m_children) child->MarkWorldDirty();
}

Mat4 GameObject::LocalMatrix() const {
    return Mat4::Translation(m_position) * m_rotation.ToMat4() * Mat4::Scale(m_scale);
}

const Mat4& GameObject::WorldMatrix() const {
    // 层级动画/物理会频繁改父级变换，这里选择"每次重算"而不是维护脏标记：
    // 一次 4x4 乘法 + 缓存写入，比维护子树脏标记更不易出错，规模上百个对象完全够用。
    m_worldMatrix = m_parent ? m_parent->WorldMatrix() * LocalMatrix() : LocalMatrix();
    return m_worldMatrix;
}

void GameObject::RemoveComponent(Component* comp) {
    for (auto it = m_components.begin(); it != m_components.end(); ++it) {
        if (it->get() == comp) {
            (*it)->OnDestroy();
            m_components.erase(it);
            return;
        }
    }
}

Component* GameObject::AddComponentByTypeName(const std::string& typeName) {
    TypeInfo* info = Reflection::Find(typeName);
    if (!info || !info->factory) return nullptr;
    return info->factory(this);
}

void GameObject::Start() {
    if (m_started) return;
    m_started = true;
    for (auto& c : m_components) {
        if (c->enabled) c->OnStart();
    }
}

void GameObject::Update(float dt) {
    if (!m_active) return;
    for (auto& c : m_components) {
        if (c->enabled) c->OnUpdate(dt);
    }
}

void GameObject::FixedUpdate(float dt) {
    if (!m_active) return;
    for (auto& c : m_components) {
        if (c->enabled) c->OnFixedUpdate(dt);
    }
}

} // namespace erm
