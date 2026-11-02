// ErMao Engine - 骨骼、动画片段与蒙皮矩阵（GAMES104 第 8/9 讲）
//
// 数学约定（与 core/Math.h 一致）：
//   * 点/向量当作列向量，变换用矩阵左乘：p' = M * p
//   * Mat4 内部按列存储（m[col][row]），传给 HLSL 的 column_major 矩阵可以直接用 mul(M, p)
//   * 骨骼的"绑定姿势"世界矩阵记作 B_i，动画姿势的世界矩阵记作 P_i，
//     那么蒙皮矩阵（skinning matrix）就是  S_i = P_i * B_i^{-1}
//     顶点最终位置 = Σ_j w_j * S_j * p  （Σ w_j = 1）
//   直观理解：S_i 把"绑定空间里的顶点"先搬回骨骼 i 的局部空间（B_i^{-1}），
//   再按当前骨骼的世界变换放回世界（P_i）。
#pragma once

#include "core/Math.h"
#include "core/Types.h"

#include <string>
#include <vector>

namespace erm {

// 单根骨骼的局部变换（TRS）
struct BoneTransform {
    Vec3 translation{0, 0, 0};
    Quat rotation{0, 0, 0, 1};
    Vec3 scale{1, 1, 1};

    Mat4 ToMatrix() const;
    static BoneTransform FromMatrix(const Mat4& m);
};

struct Bone {
    std::string name;
    int         parent = -1;
    Mat4        inverseBind = Mat4::Identity(); // B_i^{-1}
    BoneTransform bindLocal;                    // 绑定姿势下的局部 TRS（生成 inverseBind 用）
};

// 骨架：只存层级与绑定矩阵，运行时姿态单独算
struct Skeleton {
    std::vector<Bone> bones;

    int  AddBone(const std::string& name, int parent, const BoneTransform& localBind);
    // 用绑定姿势计算每个骨骼的逆绑定矩阵（必须在 AddBone 之后调用一次）
    void FinalizeBindPose();
    // 局部 TRS -> 世界矩阵（按 parent 链累乘）
    void ComputeWorld(const std::vector<BoneTransform>& local, std::vector<Mat4>& world) const;
    // 世界矩阵 * 逆绑定 = 蒙皮矩阵
    void ComputeSkinning(const std::vector<Mat4>& world, std::vector<Mat4>& outSkinning) const;
    int  BoneCount() const { return static_cast<int>(bones.size()); }
    int  FindBone(const std::string& name) const;
};

// 关键帧：时间 + 局部 TRS
struct BoneKey {
    float         time = 0.0f;
    BoneTransform value;
};

struct BoneTrack {
    int                   bone = 0;
    std::vector<BoneKey>  keys;

    BoneTransform Sample(float time) const; // 线性插值 + 四元数 slerp
};

// 一个动画片段：时长 + 若干骨骼轨道
struct SkeletonClip {
    std::string           name;
    float                 duration = 1.0f;
    bool                  loop = true;
    std::vector<BoneTrack> tracks;

    // 采样成"每根骨骼的局部 TRS"（未出现的骨骼保持 bind 姿势）
    void Sample(float time, const Skeleton& skeleton, std::vector<BoneTransform>& outLocal) const;
};

// 两个姿势的混合（第 9 讲：cross-fade）
void BlendPoses(const std::vector<BoneTransform>& a, const std::vector<BoneTransform>& b, float alpha,
                std::vector<BoneTransform>& out);

// 程序化生成的蒙皮网格（本工程的"资产"就是代码生成的）
struct SkinVertex {
    Vec3  position;
    Vec3  normal;
    Vec2  uv;
    u32   boneIndices[4] = {0, 0, 0, 0};
    float boneWeights[4] = {0, 0, 0, 0};
};

struct SkinnedMeshData {
    std::vector<SkinVertex> vertices;
    std::vector<u32>        indices;
    AABB                    bounds;
    void ComputeBounds();
};

} // namespace erm
