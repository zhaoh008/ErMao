// ErMao Engine - 轻量反射（GAMES104 第 3/13 讲）
//
// 目标：一份字段描述同时驱动「编辑器 Inspector」与「JSON 序列化」。
// 做法：每个 Component 在 .cpp 里实现 Describe(TypeInfo&)，用成员指针注册字段。
//      成员指针避免了 offsetof 对非标准布局类型的限制，也天然带类型信息。
#pragma once

#include "core/Math.h"
#include "core/Types.h"

#include <functional>

namespace erm {

class GameObject;

enum class FieldType {
    Bool,
    Int,
    Float,
    Vec2,
    Vec3,
    Vec4,
    Color,       // Vec4，按颜色显示
    String,
    MeshAsset,   // 程序化网格名下拉框
    TextureAsset // 贴图路径
};

struct FieldDesc {
    std::string                 name;  // 序列化 key
    std::string                 label; // UI 显示名
    FieldType                   type = FieldType::Float;
    std::function<void*(void*)> access;
    float                       min = 0.0f;
    float                       max = 1.0f;
    bool                        hidden = false;
};

struct TypeInfo {
    std::string            name;
    size_t                 size = 0;
    std::vector<FieldDesc> fields;
    std::function<class Component*(GameObject*)> factory;

    // 注册字段：访问器由成员指针生成
    template <typename C, typename T>
    FieldDesc& Field(const char* key, FieldType type, T C::*member, const char* label = nullptr, float min = 0.0f,
                     float max = 1.0f) {
        FieldDesc desc;
        desc.name = key;
        desc.label = label ? label : key;
        desc.type = type;
        desc.min = min;
        desc.max = max;
        desc.access = [member](void* object) -> void* { return &(static_cast<C*>(object)->*member); };
        fields.push_back(std::move(desc));
        return fields.back();
    }

    FieldDesc* FindField(const std::string& key) {
        for (auto& f : fields)
            if (f.name == key) return &f;
        return nullptr;
    }
};

// 组件基类
class Component {
public:
    virtual ~Component() = default;

    virtual const TypeInfo& GetTypeInfo() const = 0;
    // 纯表现组件：联机客户端不跑完整世界模拟，但动画/粒子/旋转仍要在本地推进。
    virtual bool IsCosmetic() const { return false; }
    virtual void             OnStart() {}
    virtual void             OnUpdate(float /*dt*/) {}
    virtual void             OnFixedUpdate(float /*dt*/) {}
    virtual void             OnDestroy() {}

    GameObject* Owner() const { return m_owner; }
    class World* GetWorld() const;

    bool enabled = true;

protected:
    GameObject* m_owner = nullptr;
    friend class GameObject;
};

// 全局类型表
class Reflection {
public:
    static std::vector<TypeInfo*>& AllMutable() {
        // inline 函数内的静态变量在所有 TU 之间共享（C++17）
        static std::vector<TypeInfo*> list;
        return list;
    }
    static const std::vector<TypeInfo*>& All() { return AllMutable(); }

    template <typename T>
    static TypeInfo& Register(const char* name) {
        static TypeInfo info = [name]() {
            TypeInfo t;
            t.name = name;
            t.size = sizeof(T);
            return t;
        }();
        static bool described = []() {
            T::Describe(info);
            AllMutable().push_back(&info);
            return true;
        }();
        (void)described;
        return info;
    }

    static TypeInfo* Find(const std::string& name);
};

// 在组件类里声明反射支持
#define ER_COMPONENT(Class, DisplayName)                                    \
public:                                                                     \
    static const char* StaticName() { return DisplayName; }                 \
    static void        Describe(::erm::TypeInfo& type);                     \
    const ::erm::TypeInfo& GetTypeInfo() const override {                   \
        return ::erm::Reflection::Register<Class>(DisplayName);             \
    }

} // namespace erm
