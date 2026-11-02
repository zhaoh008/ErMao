// ErMao Engine - 简化物理（GAMES104 第 10/11 讲）
//
// 定位：教学级。球/盒 + 静态地面与静态盒体，半隐式欧拉 + 冲量修正 + 休眠。
// 不做：GJK/EPA、CCD、关节、布料、破坏（文档里给出演进路径）。
#pragma once

#include "core/Math.h"
#include "core/Types.h"

namespace erm {

class World;
class GameObject;
class RigidBodyComponent;

struct PhysicsStats {
    int dynamicBodies = 0;
    int staticBodies = 0;
    int contacts = 0;
    int sleeping = 0;
};

class PhysicsWorld {
public:
    void Step(World& world, float dt);

    const PhysicsStats& Stats() const { return m_stats; }
    float Gravity() const { return m_gravity; }
    void  SetGravity(float g) { m_gravity = g; }
    int   SolverIterations() const { return m_iterations; }
    void  SetSolverIterations(int n) { m_iterations = (std::max)(1, n); }

    // 射线 vs 球的解析求交（AI 视线检测用）
    static bool RaySphere(const Ray& ray, const Sphere& sphere, float& t);

private:
    void Integrate(World& world, float dt);
    void Solve(World& world);
    void ResolveGround(GameObject* object, RigidBodyComponent* body);
    void ResolveStaticBoxes(World& world, GameObject* object, RigidBodyComponent* body);
    void ResolveSpherePairs(World& world);

    float         m_gravity = -9.81f;
    int           m_iterations = 4;
    PhysicsStats  m_stats;
};

} // namespace erm
