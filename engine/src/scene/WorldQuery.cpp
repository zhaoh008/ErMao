// ErMao Engine - 世界查询（射线拾取 / 角色碰撞 / 相机弹簧臂）
//
// 单独成文件是为了不把 World.cpp 继续撑大；所有查询都基于网格的世界 AABB，
// 对教学级引擎足够（精确网格射线留给后续的物理模块）。
#include "scene/World.h"
#include "asset/AssetManager.h"
#include "core/Log.h"
#include "scene/Components.h"

namespace erm {

namespace {
bool BuildWorldAABB(AssetManager* assets, GameObject* object, AABB& out) {
    if (!assets) return false;
    auto* renderer = object->GetComponent<MeshRenderer>();
    if (!renderer) return false;
    Mesh* mesh = assets->GetMesh(renderer->mesh);
    if (!mesh || !mesh->IsValid()) return false;

    const AABB& local = mesh->Bounds();
    const Vec3 corners[8] = {{local.min.x, local.min.y, local.min.z}, {local.max.x, local.min.y, local.min.z},
                             {local.max.x, local.max.y, local.min.z}, {local.min.x, local.max.y, local.min.z},
                             {local.min.x, local.min.y, local.max.z}, {local.max.x, local.min.y, local.max.z},
                             {local.max.x, local.max.y, local.max.z}, {local.min.x, local.max.y, local.max.z}};
    const Mat4 world = object->WorldMatrix();
    out.min = world.TransformPoint(corners[0]);
    out.max = out.min;
    for (int i = 1; i < 8; ++i) out.Expand(world.TransformPoint(corners[i]));
    return true;
}
} // namespace

bool World::GetObjectBounds(GameObject* object, AABB& out) const {
    if (!object) return false;
    return BuildWorldAABB(m_assets, object, out);
}

void World::CollectMeshAABBs(std::vector<std::pair<GameObject*, AABB>>& out) const {
    out.clear();
    for (const auto& root : m_roots) {
        root->ForEach([&](GameObject* object) {
            if (!object->Active()) return;
            AABB bounds;
            if (BuildWorldAABB(m_assets, object, bounds)) out.emplace_back(object, bounds);
        });
    }
}

bool World::Raycast(const Ray& ray, float maxDistance, RaycastHit& out) const {
    out = RaycastHit{};
    float best = maxDistance;
    bool  found = false;

    for (const auto& root : m_roots) {
        root->ForEach([&](GameObject* object) {
            if (!object->Active()) return;
            AABB bounds;
            if (!BuildWorldAABB(m_assets, object, bounds)) return;

            float t = 0.0f;
            Vec3  normal;
            if (IntersectRayAABB(ray, bounds, t, &normal) && t < best) {
                best = t;
                found = true;
                out.object = object;
                out.distance = t;
                out.point = ray.At(t);
                out.normal = normal;
            }
        });
    }
    return found;
}

} // namespace erm
