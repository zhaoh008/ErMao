// ErMao Engine - 世界查询（射线拾取 / 角色碰撞 / 相机弹簧臂）
//
// 单独成文件是为了不把 World.cpp 继续撑大；查询使用网格或 2D 精灵的世界 AABB，
// 对教学级引擎足够（精确网格射线留给后续的物理模块）。
#include "scene/World.h"
#include "asset/AssetManager.h"
#include "core/Log.h"
#include "scene/Components.h"

#include <cmath>

namespace erm {

namespace {
// 精灵与地面同在 z=0，给 AABB 一点厚度，避免射线与零厚度平面平行时漏拾取。
constexpr float kSpriteHalfThickness = 0.05f;
// 正交 2D 下所有精灵在同一平面，命中距离相等；该容差内按 layer 决定拾取优先级。
constexpr float kSamePlaneEpsilon = 0.01f;

bool BuildWorldAABB(AssetManager* assets, GameObject* object, AABB& out, bool includeSprites) {
    if (auto* renderer = object->GetComponent<MeshRenderer>()) {
        if (!assets) return false;
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
    if (!includeSprites) return false;

    auto* sprite = object->GetComponent<SpriteRendererComponent>();
    if (!sprite) return false;

    // 与 BuildRenderScene 的 2D 路径保持一致：中心 = 世界位置 + offset，
    // 尺寸取 sprite.size，旋转取 sprite.rotation + Transform 的 Z 旋转。
    const Vec3 position = object->WorldPosition();
    float      yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    object->Rotation().ToEuler(yaw, pitch, roll);
    const float angle = sprite->rotation + roll;
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const Vec2  center(position.x + sprite->offset.x, position.y + sprite->offset.y);
    const float hw = std::fabs(sprite->size.x) * 0.5f;
    const float hh = std::fabs(sprite->size.y) * 0.5f;
    const Vec2  local[4] = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};

    Vec2 p = center + Vec2(local[0].x * c - local[0].y * s, local[0].x * s + local[0].y * c);
    out.min = Vec3(p.x, p.y, position.z - kSpriteHalfThickness);
    out.max = Vec3(p.x, p.y, position.z + kSpriteHalfThickness);
    for (int i = 1; i < 4; ++i) {
        p = center + Vec2(local[i].x * c - local[i].y * s, local[i].x * s + local[i].y * c);
        out.Expand(Vec3(p.x, p.y, position.z));
    }
    return true;
}
} // namespace

bool World::GetObjectBounds(GameObject* object, AABB& out) const {
    if (!object) return false;
    return BuildWorldAABB(m_assets, object, out, true);
}

void World::CollectMeshAABBs(std::vector<std::pair<GameObject*, AABB>>& out) const {
    out.clear();
    for (const auto& root : m_roots) {
        root->ForEach([&](GameObject* object) {
            if (!object->Active()) return;
            AABB bounds;
            if (BuildWorldAABB(m_assets, object, bounds, false)) out.emplace_back(object, bounds);
        });
    }
}

bool World::Raycast(const Ray& ray, float maxDistance, RaycastHit& out, const GameObject* ignore) const {
    out = RaycastHit{};
    float best = maxDistance;
    bool  found = false;
    bool  bestIsSprite = false;
    int   bestLayer = 0;

    for (const auto& root : m_roots) {
        root->ForEach([&](GameObject* object) {
            if (!object->Active()) return;
            // 在构造 AABB / 求交之前跳过，避免起点在自身盒内时 tmin=0 先占住最近命中。
            if (object == ignore) return;
            AABB bounds;
            if (!BuildWorldAABB(m_assets, object, bounds, true)) return;

            float t = 0.0f;
            Vec3  normal;
            if (!IntersectRayAABB(ray, bounds, t, &normal)) return;

            auto* sprite = object->GetComponent<SpriteRendererComponent>();
            const int layer = sprite ? sprite->layer : 0;

            bool replace = false;
            if (!found) {
                replace = t < maxDistance;
            } else if (sprite && bestIsSprite && std::fabs(t - best) <= kSamePlaneEpsilon) {
                // 同一平面（2D 精灵）时按 layer 优先；距离明显不同仍取最近命中。
                replace = layer > bestLayer;
            } else {
                replace = t < best;
            }
            if (!replace) return;

            best = t;
            bestIsSprite = sprite != nullptr;
            bestLayer = layer;
            found = true;
            out.object = object;
            out.distance = t;
            out.point = ray.At(t);
            out.normal = normal;
        });
    }
    return found;
}

} // namespace erm
