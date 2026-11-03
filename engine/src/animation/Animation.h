// ErMao Engine - 关键帧动画（GAMES104 第 8/9 讲）
//
// 这里实现的是"节点动画"：TRS 关键帧 + 线性/球面插值，驱动 GameObject 层级。
// 骨骼蒙皮（Skinning）需要顶点属性与骨骼调色板，属于下一阶段；文档里给出了
// 从节点动画到蒙皮的扩展路径，两者共用同一套采样器。
#pragma once

#include "core/Math.h"
#include "core/Types.h"
#include "scene/Component.h"

namespace erm {

struct TransformKey {
    float time = 0.0f;
    Vec3  position{0, 0, 0};
    Quat  rotation{};
    Vec3  scale{1, 1, 1};
};

struct NodeTrack {
    std::string             target; // 子对象名字
    std::vector<TransformKey> keys;
};

class AnimationClip {
public:
    std::string            name;
    float                  duration = 1.0f;
    bool                   loop = true;
    std::vector<NodeTrack> tracks;

    // 在 time 处采样，对每个 track 调用 apply(目标名, 插值后的变换)
    void Sample(float time, const std::function<void(const std::string&, const Vec3&, const Quat&, const Vec3&)>& apply) const;

    // 程序化生成几个内置 clip（避免二进制动画资源）
    static AnimationClip MakeBob(const std::string& name, const std::string& target, float height, float duration);
    static AnimationClip MakeWindmill(const std::string& name, const std::string& target, float degreesPerSecond,
                                      float duration);
};

class AnimatorComponent : public Component {
public:
    ER_COMPONENT(AnimatorComponent, "Animator")

    std::string clip = "bob"; // bob | windmill
    std::string target;       // 目标子对象名（留空 = 第一个子对象）
    float       speed = 1.0f;
    float       time = 0.0f;
    bool        loop = true;
    bool        playing = true;

    void OnStart() override;
    void OnUpdate(float dt) override;

private:
    bool ResolveTarget();
    void RebuildClip();

    AnimationClip m_clip;
    class GameObject* m_targetObject = nullptr;
    u64               m_targetObjectId = 0; // 缓存指针失效校验（World::FindById）
};

} // namespace erm
