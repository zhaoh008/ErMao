// ErMao Engine - 2D 幸存者玩法（类《吸血鬼幸存者》）
//
// 设计（代码优先）：
//   * 2D 世界是 XY 平面，精灵组件负责显示，玩法组件负责逻辑；
//   * 玩家自动攻击最近的敌人，敌人成波刷新，击杀掉落经验宝石；
//   * 主菜单 / 暂停 / 失败界面用 ImGui，HUD 显示生命、经验、时间、击杀数。
#pragma once

#include "core/Math.h"
#include "core/Types.h"
#include "scene/Component.h"

namespace erm {

class World;
class GameObject;
class AssetManager;
struct RenderScene;
struct Camera2D;

// ---------------------------------------------------------------- 玩家
class SurvivorsPlayerComponent : public Component {
public:
    ER_COMPONENT(SurvivorsPlayerComponent, "Survivors Player")

    float speed = 4.6f;
    float maxHp = 100.0f;
    float hp = 100.0f;
    int   level = 1;
    float xp = 0.0f;
    float xpNext = 12.0f;

    float attackDamage = 12.0f;
    float attackInterval = 0.75f;
    float attackRange = 9.0f;
    float projectileSpeed = 14.0f;

    // 基础值：升级/击杀加成都乘在它们上面，重开一局时恢复
    float baseAttackDamage = 12.0f;
    float baseAttackInterval = 0.75f;
    float baseMaxHp = 100.0f;

    // 击杀越多，攻速越快（每击杀 +3%，递减叠加）
    int killCount = 0;

    bool  alive = true;
    float invulnerable = 0.0f;
    float hitFlash = 0.0f;
    Vec2  arenaHalf{28.0f, 15.0f};

    void OnUpdate(float dt) override;
    void AddXp(float amount);
    void ApplyKillCount(int kills);
    float KillAttackSpeedBonus() const; // 0.5 表示 +50% 攻速
    void ResetCombatStats();

    // 菜单打开时禁止移动（和 3D 的 SetInputEnabled 同思路）
    static void SetInputEnabled(bool enabled);
    static bool InputEnabled();

private:
    float m_attackTimer = 0.0f;
    float m_levelIntervalScale = 1.0f;
    float m_killIntervalScale = 1.0f;
    void  FireAtNearestEnemy(const Vec2& origin);
    void  RecomputeAttackInterval();
};

// ---------------------------------------------------------------- 敌人
class SurvivorsEnemyComponent : public Component {
public:
    ER_COMPONENT(SurvivorsEnemyComponent, "Survivors Enemy")

    float speed = 1.9f;
    float hp = 18.0f;
    float maxHp = 18.0f;
    float damage = 8.0f;
    float attackInterval = 0.8f;
    float xpValue = 3.0f;
    float hitFlash = 0.0f;

    void OnUpdate(float dt) override;
    void OnDestroy() override;

private:
    float m_attackTimer = 0.0f;
};

// ---------------------------------------------------------------- 投射物
class SurvivorsProjectileComponent : public Component {
public:
    ER_COMPONENT(SurvivorsProjectileComponent, "Survivors Projectile")

    Vec2  velocity{0, 0};
    float damage = 10.0f;
    float life = 1.6f;

    void OnUpdate(float dt) override;
};

// ---------------------------------------------------------------- 经验宝石
class SurvivorsGemComponent : public Component {
public:
    ER_COMPONENT(SurvivorsGemComponent, "Survivors Gem")

    float value = 3.0f;
    float magnetRange = 4.5f;
    float age = 0.0f;

    void OnUpdate(float dt) override;
};

// ---------------------------------------------------------------- 导演（波次/菜单/HUD）
class SurvivorsDirector {
public:
    enum class State { Menu, Playing, Paused, GameOver };

    void Reset(World& world);
    void Update(World& world, float dt);
    void DrawUI(World& world, float viewportX, float viewportY, float viewportWidth, float viewportHeight);
    // 头顶血条（世界坐标 → 屏幕）+ 漂浮伤害数字（2D 版）
    void DrawCombatFeedback(World& world, const Camera2D& camera, float viewportX, float viewportY,
                            float viewportWidth, float viewportHeight);

    State GetState() const { return m_state; }
    void  ReturnToMenu() { m_state = State::Menu; }
    bool  WantsQuit() const { return m_wantsQuit; }

    int   Kills() const { return m_kills; }
    float Time() const { return m_time; }
    void  AddKill() { ++m_kills; }

    // 关卡代码设置：把程序化宝石贴图注入（避免每次清场重建）
    void SetGemTexture(const std::string& name) { m_gemTexture = name; }

private:
    void SpawnWave(World& world);
    void SpawnEnemy(World& world, const Vec2& position, float hpScale, float speedScale);

    State       m_state = State::Menu;
    int         m_kills = 0;
    float       m_time = 0.0f;
    float       m_spawnTimer = 2.0f;
    float       m_difficulty = 0.0f;
    std::string m_gemTexture = "gem_proc";
    bool        m_wantsQuit = false;
};

// 反射：在 World 初始化后调用，保证新进程读档/脚本能找到全部玩法组件
void RegisterSurvivorsComponents();

// 关卡
void BuildSurvivorsLevel(World& world, AssetManager& assets);

} // namespace erm
