// ErMao Engine - 蒙皮网格 CPU 数据实现
#include "renderer/SkinnedMeshData.h"

namespace erm {

void SkinnedMeshData::ComputeBounds() {
    if (vertices.empty()) {
        bounds = AABB{};
        return;
    }
    bounds.min = vertices[0].position;
    bounds.max = vertices[0].position;
    for (const auto& v : vertices) bounds.Expand(v.position);
}

} // namespace erm
