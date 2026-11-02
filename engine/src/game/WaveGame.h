// ErMao Engine - 刷怪 Demo（代码优先玩法示例）
//
// 一个最小但完整的"生存/刷怪"循环：
//   * 玩家：第三人称控制 + 左键射击（投射物）
//   * 敌人：朝玩家移动、近身攻击、被击中掉血
//   * 波次：每波数量递增，清空后进入下一波
//   * 3D UI：头顶血条（世界空间 billboard，走渲染器）
//   * 菜单：开始 / 暂停 / 失败重开
#pragma once

#include "ai/BehaviorTree.h"
#include "core/Math.h"
#include "core/Types.h"
#include "scene/Component.h"

namespace erm {

class World;
class GameObject;
struct RenderScene;

// ---------------------------------------------------------------- 生命值
class HealthComponent : public Component {
public:
    ER_COMPONENT(HealthComponent, "Health")

    float maxHealth = 100.0f;
    float health = 100.0f;

    // 头顶血条（世界空间）
    bool  showBar = true;
    float barWidth = 1.3f;
    float barHeight = 0.14f;
    float barOffsetY = 1.35f;

    bool  isPlayer = false;
    Vec4  color{0.90f, 0.25f, 0.28f, 1.0f}; // 血条填充色（玩家会改绿）
    float flash = 0.0f; // 受击闪烁

    void OnUpdate(float dt) override;
    void ApplyDamage(float amount);
};

// ---------------------------------------------------------------- 玩家射击
class CombatComponent : public Component {
public:
    ER_COMPONENT(CombatComponent, "Combat")

    float fireInterval = 0.16f;
    float projectileSpeed = 34.0f;
    float damage = 26.0f;
    float projectileLife = 2.5f;

    void OnUpdate(float dt) override;

    // 无界面演示：自动朝相机方向开火
    static void SetAutoFire(bool enabled);
    static bool AutoFire();

private:
    float m_cooldown = 0.0f;
};

// 投射物：自身朝 velocity 移动，命中敌人后造成伤害
class ProjectileComponent : public Component {
public:
    ER_COMPONENT(ProjectileComponent, "Projectile")

    Vec3  velocity{0, 0, 0};
    float damage = 20.0f;
    float life = 2.5f;

    void OnUpdate(float dt) override;
};

// ---------------------------------------------------------------- 敌人
class EnemyComponent : public Component {
public:
    ER_COMPONENT(EnemyComponent, "Enemy")

    float moveSpeed = 3.2f;
    float attackRange = 1.6f;
    float attackDamage = 9.0f;
    float attackInterval = 0.9f;

    void OnUpdate(float dt) override;

private:
    void BuildTree();
    bool CanSeePlayer() const;
    BTStatus MoveToward(const Vec3& target, float dt, float speed);

    float        m_attackTimer = 0.0f;
    BehaviorTree m_tree;
    bool         m_treeBuilt = false;
    float        m_fleeTimer = 0.0f;
    Vec3         m_patrolTarget{0, 0, 0};
    float        m_patrolTimer = 0.0f;
};

// ---------------------------------------------------------------- 波次导演
class WaveGame {
public:
    enum class State { Menu, Playing, Paused, GameOver };

    void Reset(World& world);
    void Update(World& world, float dt, bool inputEnabled);
    // 3D UI：把世界坐标投影到屏幕，在 ImGui 前景层画头顶血条
    void DrawHealthBars(World& world, const class Camera& camera, float viewportX, float viewportY,
                        float viewportWidth, float viewportHeight);

    // 返回 true 表示本帧吃掉了输入（菜单打开时不要操作角色）
    bool DrawUI(World& world, float viewportX, float viewportY, float viewportWidth, float viewportHeight,
                bool viewportHovered);

    State GetState() const { return m_state; }
    void  ReturnToMenu() { m_state = State::Menu; }
    bool  WantsQuit() const { return m_wantsQuit; }
    int   Score() const { return m_score; }
    int   Wave() const { return m_wave; }

private:
    void StartRun(World& world);
    void SpawnWave(World& world);
    void SpawnEnemy(World& world, const Vec3& position);
    int  CountAlive(World& world) const;
    GameObject* FindPlayer(World& world) const;

    State m_state = State::Menu;
    int   m_wave = 0;
    int   m_score = 0;
    float m_nextWaveTimer = 0.0f;
    bool  m_wantsQuit = false;
    float m_hudFlash = 0.0f;
};

} // namespace erm
