// ErMao Engine - 3D 幸存者玩法（类《吸血鬼幸存者》的 3D 版）
//
// 与 2D 版的区别：
//   * 世界是 XZ 平面，角色是外部 OBJ 模型（Kenney blocky / graveyard）；
//   * 相机是第三人称俯视角，跟随玩家；
//   * 敌人模型来自 Kenney Graveyard Kit（骷髅/僵尸/吸血鬼），贴图用 colormap.png；
//   * 击杀特效使用 Kenney Particle Pack 的火焰/火花贴图（加法混合）。
#pragma once

#include "core/Math.h"
#include "core/Types.h"
#include "scene/Component.h"

namespace erm {

class World;
class GameObject;
class AssetManager;

// ---------------------------------------------------------------- 玩家
class Survivors3DPlayerComponent : public Component {
public:
    ER_COMPONENT(Survivors3DPlayerComponent, "Survivors3D Player")

    float speed = 5.2f;
    float maxHp = 100.0f;
    float hp = 100.0f;
    int   level = 1;
    float xp = 0.0f;
    float xpNext = 12.0f;

    float attackDamage = 14.0f;
    float attackInterval = 0.7f;
    float attackRange = 11.0f;
    float projectileSpeed = 18.0f;

    bool  alive = true;
    float invulnerable = 0.0f;
    float hitFlash = 0.0f;
    Vec2  arenaHalf{30.0f, 30.0f};

    void OnUpdate(float dt) override;
    void AddXp(float amount);

    static void SetInputEnabled(bool enabled);
    static bool InputEnabled();

private:
    float m_attackTimer = 0.0f;
    void  FireAtNearestEnemy();
};

// ---------------------------------------------------------------- 敌人
class Survivors3DEnemyComponent : public Component {
public:
    ER_COMPONENT(Survivors3DEnemyComponent, "Survivors3D Enemy")

    float speed = 2.2f;
    float hp = 22.0f;
    float damage = 9.0f;
    float attackInterval = 0.9f;
    float xpValue = 3.0f;
    float hitFlash = 0.0f;

    void OnUpdate(float dt) override;

private:
    float m_attackTimer = 0.0f;
};

// ---------------------------------------------------------------- 投射物
class Survivors3DProjectileComponent : public Component {
public:
    ER_COMPONENT(Survivors3DProjectileComponent, "Survivors3D Projectile")

    Vec3  velocity{0, 0, 0};
    float damage = 10.0f;
    float life = 1.8f;

    void OnUpdate(float dt) override;
};

// ---------------------------------------------------------------- 经验宝石
class Survivors3DGemComponent : public Component {
public:
    ER_COMPONENT(Survivors3DGemComponent, "Survivors3D Gem")

    float value = 3.0f;
    float magnetRange = 5.0f;
    float age = 0.0f;

    void OnUpdate(float dt) override;
};

// ---------------------------------------------------------------- 导演
class Survivors3DDirector {
public:
    enum class State { Menu, Playing, Paused, GameOver };

    void Reset(World& world);
    void Update(World& world, float dt);
    void DrawUI(World& world, float viewportX, float viewportY, float viewportWidth, float viewportHeight);

    State GetState() const { return m_state; }
    void  ReturnToMenu() { m_state = State::Menu; }
    bool  WantsQuit() const { return m_wantsQuit; }

    int   Kills() const { return m_kills; }
    float Time() const { return m_time; }

private:
    void SpawnWave(World& world);
    void SpawnEnemy(World& world, const Vec2& position, float hpScale, float speedScale);

    State m_state = State::Menu;
    int   m_kills = 0;
    float m_time = 0.0f;
    float m_spawnTimer = 1.2f;
    bool  m_wantsQuit = false;
};

void BuildSurvivors3DLevel(World& world, AssetManager& assets);

} // namespace erm
