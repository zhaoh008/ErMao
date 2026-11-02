// ErMao Engine - 蒙皮网格的 CPU 输入数据（GAMES104 第 8 讲）
//
// 这是 game/animation 层交给 renderer 的 DTO：只描述顶点、索引和包围盒，
// 不依赖 D3D。放在 renderer 层，renderer 就不必反向 include animation/Skeleton.h。
#pragma once

#include "core/Math.h"
#include "core/Types.h"

#include <vector>

namespace erm {

// 与 shaders/Skinned.hlsl 的顶点布局严格对应：
//   POSITION(0) NORMAL(12) TEXCOORD0(24) BLENDINDICES(32, uint4) BLENDWEIGHT(48, float4)
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
