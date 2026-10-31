// ErMao Engine - 粒子发射器实现（GAMES104 第 12 讲）
#include "game/ParticleEmitter.h"

#include "scene/GameObject.h"
#include "scene/World.h"

#include <algorithm>
#include <cmath>

namespace erm {

namespace {
constexpr size_t kMaxParticles = 384;
}

float ParticleEmitterComponent::Random01() {
    // xorshift32：确定性、无依赖，方便复现
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return static_cast<float>(m_rng & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

void ParticleEmitterComponent::OnStart() { m_accumulator = 0.0f; }

void ParticleEmitterComponent::Spawn(u32 count) {
    for (u32 i = 0; i < count; ++i) {
        if (m_positions.size() >= kMaxParticles) return;

        const Vec3 offset{(Random01() * 2.0f - 1.0f) * spawnExtent.x,
                          (Random01() * 2.0f - 1.0f) * spawnExtent.y,
                          (Random01() * 2.0f - 1.0f) * spawnExtent.z};

        const Vec3 velocity = initialVelocity + Vec3((Random01() * 2.0f - 1.0f) * spread,
                                                     (Random01() * 2.0f - 1.0f) * spread * 0.5f,
                                                     (Random01() * 2.0f - 1.0f) * spread);

        const float life = lifetime + (Random01() * 2.0f - 1.0f) * lifetimeJitter;

        m_positions.push_back(offset);
        m_velocities.push_back(velocity);
        m_life.push_back(life);
        m_maxLife.push_back((std::max)(life, 0.05f));
    }
}

void ParticleEmitterComponent::OnUpdate(float dt) {
    if (!m_owner || dt <= 0.0f) return;

    // 1) 发射：累加器 + floor，帧率无关
    m_accumulator += rate * dt;
    const int spawnCount = static_cast<int>(m_accumulator);
    if (spawnCount > 0) {
        m_accumulator -= static_cast<float>(spawnCount);
        Spawn(static_cast<u32>(spawnCount));
    }

    // 2) 模拟：半隐式欧拉 + 线性阻力
    const float damping = (std::max)(0.0f, 1.0f - drag * dt);
    for (size_t i = 0; i < m_positions.size(); ++i) {
        m_life[i] -= dt;
        if (m_life[i] <= 0.0f) continue;

        m_velocities[i] += gravity * dt;
        m_velocities[i] *= damping;
        m_positions[i] += m_velocities[i] * dt;
    }

    // 3) 回收：swap-remove，避免 O(n) 搬移
    for (size_t i = 0; i < m_positions.size();) {
        if (m_life[i] > 0.0f) {
            ++i;
            continue;
        }
        const size_t last = m_positions.size() - 1;
        m_positions[i] = m_positions[last];
        m_velocities[i] = m_velocities[last];
        m_life[i] = m_life[last];
        m_maxLife[i] = m_maxLife[last];
        m_positions.pop_back();
        m_velocities.pop_back();
        m_life.pop_back();
        m_maxLife.pop_back();
    }
}

void ParticleEmitterComponent::Collect(std::vector<ParticleRender>& out) const {
    if (!m_owner) return;
    const Vec3 origin = m_owner->WorldPosition();

    for (size_t i = 0; i < m_positions.size(); ++i) {
        const float t = 1.0f - Clamp01(m_life[i] / m_maxLife[i]); // 0=刚出生，1=死亡
        ParticleRender particle;
        particle.position = origin + m_positions[i];
        particle.size = Lerp(sizeStart, sizeEnd, t);
        particle.color = Lerp(colorStart, colorEnd, t);
        out.push_back(particle);
    }
}

void CollectParticles(World& world, RenderScene& scene) {
    scene.particles.clear();
    for (GameObject* root : world.RootObjects()) {
        root->ForEach([&scene](GameObject* object) {
            auto* emitter = object->GetComponent<ParticleEmitterComponent>();
            if (emitter) emitter->Collect(scene.particles);
        });
    }
}

void ParticleEmitterComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<ParticleEmitterComponent>(); };
    t.Field("rate", FieldType::Float, &ParticleEmitterComponent::rate, "发射率(个/秒)", 0.0f, 500.0f);
    t.Field("lifetime", FieldType::Float, &ParticleEmitterComponent::lifetime, "寿命", 0.1f, 10.0f);
    t.Field("spread", FieldType::Float, &ParticleEmitterComponent::spread, "速度抖动", 0.0f, 3.0f);
    t.Field("drag", FieldType::Float, &ParticleEmitterComponent::drag, "阻力", 0.0f, 5.0f);
    t.Field("sizeStart", FieldType::Float, &ParticleEmitterComponent::sizeStart, "初始大小", 0.01f, 2.0f);
    t.Field("colorStart", FieldType::Color, &ParticleEmitterComponent::colorStart, "起始颜色");
    t.Field("colorEnd", FieldType::Color, &ParticleEmitterComponent::colorEnd, "结束颜色");
}

} // namespace erm
