// ErMao Engine - 关键帧动画实现
#include "animation/Animation.h"
#include "core/Log.h"
#include "scene/GameObject.h"
#include "scene/World.h"

namespace erm {

namespace {
const TransformKey* FindSegment(const std::vector<TransformKey>& keys, float time, size_t& index) {
    if (keys.empty()) return nullptr;
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        if (time >= keys[i].time && time <= keys[i + 1].time) {
            index = i;
            return &keys[i];
        }
    }
    index = keys.size() - 1;
    return &keys.back();
}
} // namespace

void AnimationClip::Sample(
    float time,
    const std::function<void(const std::string&, const Vec3&, const Quat&, const Vec3&)>& apply) const {
    float t = time;
    if (loop && duration > 0.0f) {
        t = std::fmod(t, duration);
        if (t < 0.0f) t += duration;
    } else {
        t = Clamp(t, 0.0f, duration);
    }

    for (const auto& track : tracks) {
        if (track.keys.empty()) continue;
        size_t index = 0;
        const TransformKey* key = FindSegment(track.keys, t, index);
        if (!key) continue;

        Vec3 position = key->position;
        Quat rotation = key->rotation;
        Vec3 scale = key->scale;

        if (index + 1 < track.keys.size()) {
            const TransformKey& next = track.keys[index + 1];
            const float span = next.time - key->time;
            const float alpha = span > 1e-5f ? Clamp01((t - key->time) / span) : 0.0f;
            position = Lerp(key->position, next.position, alpha);
            rotation = Quat::Slerp(key->rotation, next.rotation, alpha);
            scale = Lerp(key->scale, next.scale, alpha);
        }
        apply(track.target, position, rotation, scale);
    }
}

AnimationClip AnimationClip::MakeBob(const std::string& name, const std::string& target, float height,
                                     float duration) {
    AnimationClip clip;
    clip.name = name;
    clip.duration = duration;
    clip.loop = true;

    NodeTrack track;
    track.target = target;
    const int   samples = 4;
    for (int i = 0; i <= samples; ++i) {
        const float t = duration * static_cast<float>(i) / samples;
        const float phase = static_cast<float>(i) / samples * kTwoPi;
        TransformKey key;
        key.time = t;
        key.position = Vec3(0.0f, std::sin(phase) * height, 0.0f);
        key.rotation = Quat::Identity(); // 旋转轨道留空，避免循环回绕时 90° 弹跳
        key.scale = Vec3(1, 1, 1);
        track.keys.push_back(key);
    }
    clip.tracks.push_back(track);
    return clip;
}

AnimationClip AnimationClip::MakeWindmill(const std::string& name, const std::string& target, float degreesPerSecond,
                                          float duration) {
    AnimationClip clip;
    clip.name = name;
    clip.duration = duration;
    clip.loop = true;

    NodeTrack track;
    track.target = target;
    const int samples = 8;
    for (int i = 0; i <= samples; ++i) {
        const float t = duration * static_cast<float>(i) / samples;
        TransformKey key;
        key.time = t;
        const float angle = ToRadians(degreesPerSecond) * t;
        key.position = Vec3(0, 0, 0);
        key.rotation = Quat::FromAxisAngle({0, 1, 0}, angle);
        key.scale = Vec3(1, 1, 1);
        track.keys.push_back(key);
    }
    clip.tracks.push_back(track);
    return clip;
}

bool AnimatorComponent::ResolveTarget() {
    m_targetObject = nullptr;
    m_targetObjectId = 0;
    if (!m_owner) return false;
    if (!target.empty()) {
        for (auto* child : m_owner->Children()) {
            if (child->Name() == target) {
                m_targetObject = child;
                break;
            }
        }
    }
    if (!m_targetObject && !m_owner->Children().empty()) {
        m_targetObject = m_owner->Children().front();
        target = m_targetObject->Name();
    }
    if (!m_targetObject) return false;
    m_targetObjectId = m_targetObject->Id();
    return true;
}

void AnimatorComponent::OnStart() {
    // 一次性初始化：目标解析失败（例如对象暂时没有子物体）也只执行到这里，
    // 后续由 OnUpdate 做轻量重试，不再重复 RebuildClip/写启动日志。
    ResolveTarget();
    RebuildClip();
    LogInfo("Animator 启动：clip=%s target=%s", clip.c_str(), target.c_str());
}

void AnimatorComponent::RebuildClip() {
    if (clip == "windmill") {
        m_clip = AnimationClip::MakeWindmill("windmill", target, 90.0f, 4.0f);
    } else {
        m_clip = AnimationClip::MakeBob("bob", target, 0.45f, 2.0f);
    }
    m_clip.loop = loop;
}

void AnimatorComponent::OnUpdate(float dt) {
    // 目标被删除后重新解析（审核 P2-15 / ERMAO-AUDIT-014）：World 在帧尾释放对象，
    // 裸指针会悬空，因此用 id 向 World 查询确认它仍然存活。
    World* world = GetWorld();
    if (m_targetObject && (!world || world->FindById(m_targetObjectId) != m_targetObject)) {
        m_targetObject = nullptr;
        m_targetObjectId = 0;
    }
    if (!m_targetObject) {
        // 目标缺失时只做轻量解析；解析成功后才重建 clip 并记录一次恢复日志。
        // 不能在这里调用 OnStart()：它包含一次性初始化、RebuildClip 与日志，
        // 无子物体的对象会每帧重跑，导致重复分配和日志膨胀。
        if (!ResolveTarget()) return;
        RebuildClip();
        LogInfo("Animator 目标恢复：clip=%s target=%s", clip.c_str(), target.c_str());
    }
    if (!playing) return;
    time += dt * speed;
    m_clip.loop = loop;
    m_clip.Sample(time, [&](const std::string& name, const Vec3& position, const Quat& rotation, const Vec3& scale) {
        if (name != m_targetObject->Name()) return;
        m_targetObject->SetPosition(position);
        m_targetObject->SetRotation(rotation);
        // 注意：不覆盖缩放。动画只驱动"运动"，物体的美术缩放应该保留，
        // 这也是 UE 里动画轨道与 StaticMesh 缩放分离的原因。
        (void)scale;
    });
}

// ---------------------------------------------------------------- 反射
void AnimatorComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<AnimatorComponent>(); };
    t.Field("clip", FieldType::String, &AnimatorComponent::clip, "动画(bob/windmill)");
    t.Field("target", FieldType::String, &AnimatorComponent::target, "目标子节点");
    t.Field("speed", FieldType::Float, &AnimatorComponent::speed, "播放速度", -4.0f, 4.0f);
    t.Field("time", FieldType::Float, &AnimatorComponent::time, "当前时间", 0.0f, 100.0f);
    t.Field("loop", FieldType::Bool, &AnimatorComponent::loop, "循环");
    t.Field("playing", FieldType::Bool, &AnimatorComponent::playing, "播放");
}

} // namespace erm
