// ErMao Engine - 粒子发射器组件（GAMES104 第 12 讲）
//
// 分层说明：
//   * 组件属于玩法层（game），负责"模拟"；
//   * 渲染器只认识 RenderScene 里的 ParticleRender（位置/大小/颜色），负责"画"。
// 这样粒子模拟不依赖 GPU，渲染器也不认识 GameObject。
#pragma once

#include "core/Math.h"
#include "renderer/RenderScene.h"
#include "scene/Component.h"

#include <vector>

namespace erm {

class ParticleEmitterComponent : public Component {
public:
    ER_COMPONENT(ParticleEmitterComponent, "Particle Emitter")

    // 发射
    float rate = 45.0f; // 每秒发射数
    Vec3  spawnExtent{0.18f, 0.0f, 0.18f};
    float lifetime = 1.3f;
    float lifetimeJitter = 0.3f;
    Vec3  initialVelocity{0.0f, 1.6f, 0.0f};
    float spread = 0.35f;

    // 模拟
    Vec3  gravity{0.0f, 0.8f, 0.0f};
    float drag = 0.4f;

    // 外观
    Vec4  colorStart{1.0f, 0.72f, 0.28f, 1.0f};
    Vec4  colorEnd{0.55f, 0.10f, 0.05f, 0.0f};
    float sizeStart = 0.16f;
    float sizeEnd = 0.03f;
    std::string texture; // 例如 "particles/flame_01.png"；空 = 程序化圆点

    void OnStart() override;
    void OnUpdate(float dt) override;
    void Collect(std::vector<ParticleRender>& out, const class Texture* texture) const;

    u32 AliveCount() const { return static_cast<u32>(m_positions.size()); }

private:
    void Spawn(u32 count);
    float Random01();

    std::vector<Vec3>  m_positions;
    std::vector<Vec3>  m_velocities;
    std::vector<float> m_life;
    std::vector<float> m_maxLife;
    float              m_accumulator = 0.0f;
    u32                m_rng = 0x1234567u;
};

// 收集场景里所有发射器的粒子到 RenderScene
void CollectParticles(World& world, RenderScene& scene);

} // namespace erm
