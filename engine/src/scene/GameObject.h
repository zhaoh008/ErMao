// ErMao Engine - 场景对象（GAMES104 第 3 讲）
#pragma once

#include "core/Math.h"
#include "core/Types.h"
#include "scene/Component.h"

namespace erm {

class World;

// GameObject = 名字 + 层级 Transform + 一组 Component。
// 与 UE 的心智模型一致：组件之间靠 GetComponent 互相访问，生命周期由 World 统一驱动。
class GameObject {
public:
    GameObject(World* world, u64 id, std::string name);
    ~GameObject();

    u64                Id() const { return m_id; }
    const std::string& Name() const { return m_name; }
    void               SetName(const std::string& name) { m_name = name; }

    bool Active() const { return m_active; }
    void SetActive(bool active) { m_active = active; }

    // ------------------------------------------------ 层级
    GameObject*                     Parent() const { return m_parent; }
    const std::vector<GameObject*>& Children() const { return m_children; }
    void                            SetParent(GameObject* parent);
    bool                            IsDescendantOf(const GameObject* other) const;

    // ------------------------------------------------ 变换
    const Vec3& Position() const { return m_position; }
    const Quat& Rotation() const { return m_rotation; }
    const Vec3& Scale() const { return m_scale; }
    void SetPosition(const Vec3& p);
    void SetRotation(const Quat& q);
    void SetRotationEulerDegrees(const Vec3& euler);
    void SetScale(const Vec3& s);

    Mat4 LocalMatrix() const;
    const Mat4& WorldMatrix() const;
    Vec3  WorldPosition() const { return WorldMatrix().TransformPoint(Vec3(0, 0, 0)); }
    Vec3  Forward() const { return WorldMatrix().TransformVector(Vec3(0, 0, -1)); }

    // ------------------------------------------------ 组件
    template <typename T, typename... Args>
    T* AddComponent(Args&&... args) {
        static_assert(std::is_base_of_v<Component, T>, "T 必须是 Component 子类");
        if (T* existing = GetComponent<T>()) return existing;
        auto comp = MakeScope<T>(std::forward<Args>(args)...);
        comp->m_owner = this;
        T* raw = comp.get();
        m_components.push_back(std::move(comp));
        if (m_started) raw->OnStart();
        return raw;
    }

    template <typename T>
    T* GetComponent() const {
        for (auto& c : m_components) {
            if (auto* casted = dynamic_cast<T*>(c.get())) return casted;
        }
        return nullptr;
    }

    template <typename T>
    bool HasComponent() const {
        return GetComponent<T>() != nullptr;
    }

    void                      RemoveComponent(Component* comp);
    const std::vector<Scope<Component>>& Components() const { return m_components; }

    // 用反射名字创建组件（反序列化用）
    Component* AddComponentByTypeName(const std::string& typeName);

    World* GetWorld() const { return m_world; }

    // ------------------------------------------------ 生命周期
    void Start();
    void Update(float dt);
    void FixedUpdate(float dt);
    void MarkForDestroy() { m_pendingDestroy = true; }
    bool IsPendingDestroy() const { return m_pendingDestroy; }

    // 递归遍历（含自身）
    template <typename Fn>
    void ForEach(Fn&& fn) {
        fn(this);
        // 索引遍历避免每帧拷贝 vector（审核 P3-20）；遍历期间不要增删子节点
        for (size_t i = 0; i < m_children.size(); ++i) {
            if (m_children[i]) m_children[i]->ForEach(fn);
        }
    }

private:
    void MarkWorldDirty();

    World*                          m_world = nullptr;
    u64                             m_id = 0;
    std::string                     m_name;
    bool                            m_active = true;
    bool                            m_started = false;
    bool                            m_pendingDestroy = false;

    GameObject*                     m_parent = nullptr;
    std::vector<GameObject*>        m_children;

    Vec3                            m_position{0, 0, 0};
    Quat                            m_rotation{};
    Vec3                            m_scale{1, 1, 1};

    mutable Mat4                    m_worldMatrix;
    mutable bool                    m_worldDirty = true;

    std::vector<Scope<Component>>   m_components;
    friend class World;
};

} // namespace erm
