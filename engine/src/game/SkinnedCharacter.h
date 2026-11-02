// ErMao Engine - 蒙皮角色组件（GAMES104 第 8/9 讲）
//
// 每帧做三件事：
//   1. 采样两个动画片段（A/B），得到每根骨骼的局部 TRS；
//   2. 按 blend 做姿势混合（cross-fade）；
//   3. 局部 TRS -> 世界矩阵 -> 蒙皮矩阵 S_i = P_i * B_i^{-1}，交给渲染器。
#pragma once

#include "animation/Skeleton.h"
#include "scene/Component.h"

namespace erm {

class SkinnedMesh;

class SkinnedCharacterComponent : public Component {
public:
    ER_COMPONENT(SkinnedCharacterComponent, "Skinned Character")

    // 关卡代码设置（不参与序列化：指针没有意义）
    const SkinnedMesh* mesh = nullptr;
    Skeleton           skeleton;
    SkeletonClip      clipA;
    SkeletonClip      clipB;

    // 可序列化参数
    float blend = 0.35f;      // 0=A，1=B
    float speedA = 1.0f;
    float speedB = 1.0f;
    float phase = 0.0f;       // 起始相位（让多个角色不同步）
    Vec4  albedo{0.55f, 0.62f, 0.78f, 1.0f};
    float roughness = 0.45f;
    float metallic = 0.05f;

    void OnUpdate(float dt) override;

    // 填充渲染数据（世界矩阵由 GameObject 提供）
    void FillDraw(const Mat4& world, struct SkinnedDraw& out) const;

    const std::vector<Mat4>& BoneMatrices() const { return m_bones; }

private:
    float              m_timeA = 0.0f;
    float              m_timeB = 0.0f;
    std::vector<Mat4>  m_bones;
    std::vector<BoneTransform> m_poseA;
    std::vector<BoneTransform> m_poseB;
    std::vector<BoneTransform> m_pose;
    std::vector<Mat4>          m_worldMatrices;
};

} // namespace erm
