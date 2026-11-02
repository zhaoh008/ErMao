// ErMao Engine - 简化物理实现（世界空间版本）
//
// 审核修复：
//   P2-11 统一用世界坐标计算，写回时若是子节点再转局部；
//   P2-12 多个静态盒的推出量累加后一次性应用，不再互相覆盖；
//   P3-25 休眠体跳过积分，sleeping 统计的是"当前帧休眠体数量"。
#include "physics/PhysicsWorld.h"
#include "core/Log.h"
#include "scene/Components.h"
#include "scene/GameObject.h"
#include "scene/World.h"

namespace erm {

namespace {

// 球 vs AABB（最近的表面点）
bool SphereVsBox(const Vec3& center, float radius, const AABB& box, Vec3& normal, float& depth) {
    const Vec3 closest{Clamp(center.x, box.min.x, box.max.x), Clamp(center.y, box.min.y, box.max.y),
                       Clamp(center.z, box.min.z, box.max.z)};
    const Vec3  diff = center - closest;
    const float distSq = Dot(diff, diff);
    if (distSq > radius * radius) return false;

    if (distSq > 1e-8f) {
        const float dist = std::sqrt(distSq);
        normal = diff / dist;
        depth = radius - dist;
    } else {
        const Vec3  ext = box.Extents();
        const Vec3  c = box.Center();
        const Vec3  d = center - c;
        const float px = ext.x + radius - std::fabs(d.x);
        const float py = ext.y + radius - std::fabs(d.y);
        const float pz = ext.z + radius - std::fabs(d.z);
        if (px < py && px < pz) {
            normal = Vec3(Sign(d.x), 0, 0);
            depth = px;
        } else if (py < pz) {
            normal = Vec3(0, Sign(d.y), 0);
            depth = py;
        } else {
            normal = Vec3(0, 0, Sign(d.z));
            depth = pz;
        }
    }
    return true;
}

bool SphereVsSphere(const Vec3& a, float ra, const Vec3& b, float rb, Vec3& normal, float& depth) {
    const Vec3  delta = a - b;
    const float distSq = Dot(delta, delta);
    const float sum = ra + rb;
    if (distSq > sum * sum || distSq < 1e-8f) return false;
    const float dist = std::sqrt(distSq);
    normal = delta / dist;
    depth = sum - dist;
    return true;
}

// 物理统一在世界空间计算；写回时如果是子节点再转回局部
void SetWorldPosition(GameObject* object, const Vec3& world) {
    if (GameObject* parent = object->Parent()) {
        const Mat4 inverse = parent->WorldMatrix().Inverse();
        object->SetPosition(inverse.TransformPoint(world));
    } else {
        object->SetPosition(world);
    }
}

} // namespace

bool PhysicsWorld::RaySphere(const Ray& ray, const Sphere& sphere, float& t) {
    return IntersectRaySphere(ray, sphere, t);
}

void PhysicsWorld::Step(World& world, float dt) {
    if (dt <= 0.0f) return;
    m_stats = PhysicsStats{};
    Integrate(world, dt);
    for (int i = 0; i < m_iterations; ++i) Solve(world);
}

void PhysicsWorld::Integrate(World& world, float dt) {
    for (GameObject* root : world.RootObjects()) {
        root->ForEach([&](GameObject* object) {
            auto* body = object->GetComponent<RigidBodyComponent>();
            if (!body) return;
            if (body->isStatic) {
                ++m_stats.staticBodies;
                return;
            }
            ++m_stats.dynamicBodies;

            // 休眠体直接跳过积分（P3-25）
            if (body->grounded && body->sleepTimer > 0.6f) {
                body->velocity = Vec3(0, 0, 0);
                ++m_stats.sleeping;
                return;
            }

            if (body->useGravity) body->velocity.y += m_gravity * dt;
            body->velocity *= (1.0f - 0.02f); // 线性阻尼
            Vec3 pos = object->WorldPosition();
            pos += body->velocity * dt;
            SetWorldPosition(object, pos);
        });
    }
}

void PhysicsWorld::Solve(World& world) {
    // 球-球碰撞先做（P2-12 的同类问题：多次推出要一次性应用）
    ResolveSpherePairs(world);

    for (GameObject* root : world.RootObjects()) {
        root->ForEach([&](GameObject* object) {
            auto* body = object->GetComponent<RigidBodyComponent>();
            if (!body || body->isStatic) return;
            ResolveGround(object, body);
            ResolveStaticBoxes(world, object, body);
        });
    }
}

void PhysicsWorld::ResolveGround(GameObject* object, RigidBodyComponent* body) {
    Vec3 pos = object->WorldPosition();
    if (body->shape == static_cast<int>(RigidBodyComponent::Shape::Sphere)) {
        const float bottom = pos.y - body->radius;
        if (bottom < 0.0f) {
            SetWorldPosition(object, Vec3(pos.x, body->radius, pos.z));
            if (body->velocity.y < 0.0f) {
                body->velocity.y = -body->velocity.y * body->restitution;
                if (std::fabs(body->velocity.y) < 0.35f) body->velocity.y = 0.0f;
            }
            const float damping = (std::max)(0.0f, 1.0f - body->friction * 6.0f * (1.0f / 60.0f));
            body->velocity.x *= damping;
            body->velocity.z *= damping;
            body->grounded = true;
            ++m_stats.contacts;
        } else {
            body->grounded = false;
        }
    } else {
        const float bottom = pos.y - body->halfExtents.y;
        if (bottom < 0.0f) {
            SetWorldPosition(object, Vec3(pos.x, body->halfExtents.y, pos.z));
            if (body->velocity.y < 0.0f) {
                body->velocity.y = -body->velocity.y * body->restitution;
                if (std::fabs(body->velocity.y) < 0.5f) body->velocity.y = 0.0f;
            }
            body->velocity.x *= 0.92f;
            body->velocity.z *= 0.92f;
            body->grounded = true;
            ++m_stats.contacts;
        } else {
            body->grounded = false;
        }
    }

    // 休眠：速度足够小且贴地时停止积分
    const float speedSq = LengthSq(body->velocity);
    if (body->grounded && speedSq < 0.02f) {
        body->sleepTimer += 1.0f / 60.0f;
        if (body->sleepTimer > 0.6f) body->velocity = Vec3(0, 0, 0);
    } else {
        body->sleepTimer = 0.0f;
    }
}

void PhysicsWorld::ResolveStaticBoxes(World& world, GameObject* object, RigidBodyComponent* body) {
    const Vec3  startPos = object->WorldPosition();
    Vec3        position = startPos;
    bool        touched = false;

    for (GameObject* root : world.RootObjects()) {
        root->ForEach([&](GameObject* other) {
            auto* staticBody = other->GetComponent<RigidBodyComponent>();
            if (!staticBody || !staticBody->isStatic) return;
            if (other == object) return;

            const AABB box = AABB::FromCenterExtents(other->WorldPosition(), staticBody->halfExtents);

            if (body->shape == static_cast<int>(RigidBodyComponent::Shape::Sphere)) {
                Vec3  normal;
                float depth = 0.0f;
                if (SphereVsBox(position, body->radius, box, normal, depth)) {
                    position += normal * depth;
                    const float vn = Dot(body->velocity, normal);
                    if (vn < 0.0f) {
                        body->velocity -= normal * (vn * (1.0f + body->restitution));
                        const Vec3 tangent = body->velocity - normal * Dot(body->velocity, normal);
                        body->velocity -= tangent * body->friction;
                    }
                    touched = true;
                    ++m_stats.contacts;
                }
            } else {
                const AABB self = AABB::FromCenterExtents(position, body->halfExtents);
                if (!(self.min.x < box.max.x && self.max.x > box.min.x && self.min.y < box.max.y &&
                      self.max.y > box.min.y && self.min.z < box.max.z && self.max.z > box.min.z)) {
                    return;
                }
                const Vec3 overlap{std::fmin(self.max.x, box.max.x) - std::fmax(self.min.x, box.min.x),
                                   std::fmin(self.max.y, box.max.y) - std::fmax(self.min.y, box.min.y),
                                   std::fmin(self.max.z, box.max.z) - std::fmax(self.min.z, box.min.z)};
                int        axis = 0;
                if (overlap.y < overlap.x) axis = 1;
                if (overlap.z < overlap[axis]) axis = 2;
                const float sign = (position[axis] > other->WorldPosition()[axis]) ? 1.0f : -1.0f;
                Vec3        push(0, 0, 0);
                push[axis] = overlap[axis] * sign;
                position += push;
                if (body->velocity[axis] * sign < 0.0f) {
                    body->velocity[axis] = -body->velocity[axis] * body->restitution;
                }
                touched = true;
                ++m_stats.contacts;
            }
        });
    }

    if (touched) SetWorldPosition(object, position);
}

void PhysicsWorld::ResolveSpherePairs(World& world) {
    struct Entry {
        GameObject*          object;
        RigidBodyComponent*  body;
    };
    std::vector<Entry> spheres;
    for (GameObject* root : world.RootObjects()) {
        root->ForEach([&](GameObject* object) {
            auto* body = object->GetComponent<RigidBodyComponent>();
            if (!body || body->isStatic) return;
            if (body->shape != static_cast<int>(RigidBodyComponent::Shape::Sphere)) return;
            spheres.push_back({object, body});
        });
    }

    // 每个物体先累加位移，最后一次性写回，避免多个碰撞互相覆盖
    std::vector<Vec3> corrections(spheres.size(), Vec3(0, 0, 0));

    for (size_t i = 0; i < spheres.size(); ++i) {
        for (size_t j = i + 1; j < spheres.size(); ++j) {
            auto*       a = spheres[i].body;
            auto*       b = spheres[j].body;
            GameObject* ga = spheres[i].object;
            GameObject* gb = spheres[j].object;

            const float invA = a->mass > 0.0f ? 1.0f / a->mass : 0.0f;
            const float invB = b->mass > 0.0f ? 1.0f / b->mass : 0.0f;
            const float invSum = invA + invB;
            if (invSum <= 0.0f) continue;

            Vec3  normal;
            float depth = 0.0f;
            if (!SphereVsSphere(ga->WorldPosition(), a->radius, gb->WorldPosition(), b->radius, normal, depth))
                continue;

            corrections[i] += normal * (depth * invA / invSum);
            corrections[j] -= normal * (depth * invB / invSum);

            const Vec3  relative = b->velocity - a->velocity;
            const float vn = Dot(relative, normal);
            if (vn < 0.0f) {
                const float e = (std::min)(a->restitution, b->restitution);
                const float impulse = -(1.0f + e) * vn / invSum;
                a->velocity -= normal * (impulse * invA);
                b->velocity += normal * (impulse * invB);
                ++m_stats.contacts;
            }
        }
    }

    for (size_t i = 0; i < spheres.size(); ++i) {
        if (LengthSq(corrections[i]) > 0.0f) {
            SetWorldPosition(spheres[i].object, spheres[i].object->WorldPosition() + corrections[i]);
        }
    }
}

} // namespace erm
