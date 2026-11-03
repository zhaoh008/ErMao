// ErMao Engine - 2D 幸存者玩法（类《吸血鬼幸存者》）
//
// 设计（代码优先）：
//   * 2D 世界是 XY 平面，精灵组件负责显示，玩法组件负责逻辑；
//   * 玩家自动攻击最近的敌人，敌人成波刷新，击杀掉落经验宝石和技能强化道具；
//   * 箭矢是基础自动技能，击杀掉落可以把它强化成八向散射、加攻速、加攻击、加范围，
//     血瓶直接回血；另外还有闪电 / 回旋刃 / 霜环 / 毒雾 / 火球五个技能；
//   * 八页界面（主菜单/出战准备/HUD/升级三选一/暂停/设置/结算/技能图鉴）用自绘 ImGui 图层，
//     九宫格切图来自用户提供的 ermao-ui-v2 UI 原型；HUD 显示生命、经验、时间、击杀与技能。
#pragma once

#include "core/Math.h"
#include "core/Types.h"
#include "game/SurvivorsUI.h"
#include "scene/Component.h"

#include <string>

namespace erm {

class World;
class GameObject;
class AssetManager;
struct RenderScene;
struct Camera2D;

// ---------------------------------------------------------------- 掉落/强化类型
enum class SurvivorsPickupType : int {
    ArrowSpread = 0,      // 八向散射（一次性解锁）
    ArrowAttackSpeed,     // 箭矢攻速
    ArrowAttackDamage,    // 箭矢攻击力
    ArrowHealthPotion,    // 血瓶
    ArrowRange,           // 箭矢攻击范围
    LightningUpgrade,     // 雷霆强化
    BladeUpgrade,         // 回旋刃强化
    FrostUpgrade,         // 霜环强化
    PoisonUpgrade,        // 毒雾强化
    FireballUpgrade,      // 火球强化
    Count
};

// ---------------------------------------------------------------- 敌人类型
enum class SurvivorsEnemyType : int {
    Orc = 0, // 兽人：基础近战
    Slime,   // 史莱姆：慢、肉
    Ghost,   // 幽灵：快、脆、穿障碍
    Spider,  // 蜘蛛：极快、脆
    Knight,  // 骷髅骑士：高血量
    Elite,   // 兽人精英：高血量高伤害
    Count
};

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
    // 经验已满、等待三选一确认的升级次数（AddXp 只累计，确认后才真正升级）
    int   pendingLevelUps = 0;

    float attackDamage = 12.0f;
    float attackInterval = 0.75f;
    float attackRange = 9.0f;
    float projectileSpeed = 14.0f;

    // 基础值：升级/击杀/掉落加成都乘在它们上面，重开一局时恢复
    float baseAttackDamage = 12.0f;
    float baseAttackInterval = 0.75f;
    float baseMaxHp = 100.0f;
    float baseAttackRange = 9.0f;
    float baseProjectileLife = 1.6f;

    // 击杀越多，攻速越快（每击杀 +3%，递减叠加）
    int killCount = 0;

    // 箭矢的掉落强化
    bool  arrowSpread = false; // 八向散射
    int   arrowSpeedLevel = 0;
    int   arrowDamageLevel = 0;
    int   arrowRangeLevel = 0;
    Vec2  facing{1.0f, 0.0f}; // 没有目标时朝这个方向射

    // 其他技能等级（从 1 开始）
    int lightningLevel = 1;
    int bladeLevel = 1;
    int frostLevel = 1;
    int poisonLevel = 1;
    int fireballLevel = 1;

    // 最近拾取提示
    std::string pickupToast;
    float       pickupToastTimer = 0.0f;

    bool  alive = true;
    float invulnerable = 0.0f;
    float hitFlash = 0.0f;
    Vec2  arenaHalf{43.0f, 25.0f};

    void OnUpdate(float dt) override;
    void AddXp(float amount);
    void ApplyKillCount(int kills);
    float KillAttackSpeedBonus() const; // 0.5 表示 +50% 攻速
    float ArrowSpeedBonus() const;      // 击杀 + 掉落攻速的总加成
    void  ResetCombatStats();
    void  RefreshDerivedStats();
    void  ApplyPickup(int type);

    // 三选一：先应用强化，再由 CompleteLevelUp 结算等级
    void ApplyLevelUpUpgrade(int skill, int sub);
    void CompleteLevelUp();

    // 当前生效数值（HUD 与投射物共用，避免两处公式不一致）
    float ArrowDamage() const;
    float ArrowRange() const;
    float ArrowProjectileLife() const;
    float BladeAngle() const;

    float LightningDamage() const;
    int   LightningTargets() const;
    int   LightningChains() const;
    float LightningRadius() const;
    float LightningCooldown() const;

    float BladeDamage() const;
    int   BladeCount() const;
    float BladeRadius() const;
    float BladeSpeed() const;

    float FrostDamage() const;
    float FrostRadius() const;
    float FrostSlow() const;
    float FrostCooldown() const;

    float PoisonDps() const;
    float PoisonRadius() const;

    float FireballDamage() const;
    int   FireballCount() const;
    float FireballExplosionRadius() const;
    float FireballCooldown() const;

    // 菜单打开时禁止移动（和 3D 的 SetInputEnabled 同思路）
    static void SetInputEnabled(bool enabled);
    static bool InputEnabled();

private:
    float m_attackTimer = 0.0f;
    float m_levelIntervalScale = 1.0f;
    float m_killIntervalScale = 1.0f;
    float m_dropIntervalScale = 1.0f;

    float m_lightningTimer = 0.0f;
    float m_frostTimer = 0.0f;
    float m_fireballTimer = 0.0f;
    float m_poisonTick = 0.0f;
    float m_bladeTick = 0.0f;
    float m_bladeAngle = 0.0f;
    float m_auraSfxTimer = 0.0f;

    void FireAtNearestEnemy(const Vec2& origin);
    void FireArrow(const Vec2& origin, const Vec2& direction, float damageScale);
    void UpdateSkills(float dt);
    bool CastLightning();
    void CastFrost();
    void CastFireball();
    void UpdateBlades(float dt);
    void UpdateAura(float dt);
    void RecomputeAttackInterval();
    void RecomputeAttackDamage();
    void ShowPickupToast(const std::string& text);
};

// ---------------------------------------------------------------- 敌人
class SurvivorsEnemyComponent : public Component {
public:
    ER_COMPONENT(SurvivorsEnemyComponent, "Survivors Enemy")

    int   type = 0; // SurvivorsEnemyType
    float speed = 1.9f;
    float hp = 18.0f;
    float maxHp = 18.0f;
    float damage = 8.0f;
    float attackInterval = 0.8f;
    float xpValue = 3.0f;
    float hitFlash = 0.0f;
    float slowTimer = 0.0f;
    float slowFactor = 1.0f;
    float poisonTimer = 0.0f;
    float bobPhase = 0.0f;
    float contactRadius = 0.75f;
    int   dropCount = 1;
    Vec4  baseColor{1.0f, 1.0f, 1.0f, 1.0f};

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
    float magnetRange = 6.0f;
    float age = 0.0f;

    void OnUpdate(float dt) override;
};

// ---------------------------------------------------------------- 技能强化掉落物
class SurvivorsPickupComponent : public Component {
public:
    ER_COMPONENT(SurvivorsPickupComponent, "Survivors Pickup")

    int   type = 0; // SurvivorsPickupType
    float age = 0.0f;
    float magnetRange = 7.0f;

    void OnUpdate(float dt) override;
    void OnDestroy() override;
};

// ---------------------------------------------------------------- 三选一候选
struct SurvivorsLevelUpChoice {
    int skill = 0; // 0=箭矢 1=雷霆 2=回旋刃 3=霜环 4=毒雾 5=火球
    int sub = 0;   // 箭矢专用：0=八向散射 1=攻速 2=攻击 3=范围
};

// ---------------------------------------------------------------- 导演（波次/菜单/HUD/存档）
class SurvivorsDirector {
public:
    enum class State { Menu, Prepare, Playing, Paused, LevelUp, GameOver, Codex };

    void Reset(World& world);
    void Update(World& world, float dt);
    void DrawUI(World& world, float viewportX, float viewportY, float viewportWidth, float viewportHeight);
    // 头顶血条（世界坐标 → 屏幕）+ 漂浮伤害数字（2D 版）
    void DrawCombatFeedback(World& world, const Camera2D& camera, float viewportX, float viewportY,
                            float viewportWidth, float viewportHeight);

    State GetState() const { return m_state; }
    void  ReturnToMenu() {
        m_state = State::Menu;
        m_showSettings = false;
        m_selectedSkill = -1;
        m_pauseNotice.clear();
        m_confirmExit = false;
    }
    void  TogglePause();
    bool  WantsQuit() const { return m_wantsQuit; }

    int   Kills() const { return m_kills; }
    float Time() const { return m_time; }
    void  AddKill() { ++m_kills; }

    // 存档
    bool HasSave() const;
    bool SaveGame(World& world);
    bool LoadGame(World& world);
    void ShowToast(const std::string& text);

    // UI 切图/字体在关卡构建完成后注入（资源管理器此时已有 floor_big 等程序化贴图）
    void InitializeUI(AssetManager& assets, ImFont* body, ImFont* large, ImFont* display);
    // 截图/走查用：--ui-preview <menu|prepare|hud|levelup|pause|settings|result|codex>
    void ApplyPreviewState(World& world, const std::string& screen);

    // 设置面板数据（模型层只暴露快照，避免把文件读写细节带进 UI）
    SurvivorsSettingsState GetSettings() const;
    void                   ApplySettings(const SurvivorsSettingsState& settings, bool persist = true);

    // 关卡代码设置：把程序化宝石贴图注入（避免每次清场重建）
    void SetGemTexture(const std::string& name) { m_gemTexture = name; }

private:
    void SpawnWave(World& world);
    void SpawnEnemy(World& world, const Vec2& position, int type, float hpScale, float speedScale,
                    float damageScale);
    void SpawnElite(World& world);
    void EnsureMusic();
    void ApplyAudioSettings();

    // UI 各页（实现见 SurvivorsUI.cpp）
    void DrawMenuScreen(World& world, float vx, float vy, float vw, float vh);
    void DrawPrepareScreen(World& world, float vx, float vy, float vw, float vh);
    void DrawHudScreen(World& world, float vx, float vy, float vw, float vh, bool interactive);
    void DrawLevelUpScreen(World& world, float vx, float vy, float vw, float vh);
    void DrawPauseScreen(World& world, float vx, float vy, float vw, float vh);
    void DrawSettingsScreen(float vx, float vy, float vw, float vh);
    void DrawResultScreen(World& world, float vx, float vy, float vw, float vh);
    void DrawCodexScreen(float vx, float vy, float vw, float vh);
    void DrawToast(float vx, float vy, float vw, float vh, const std::string& text, float timer);

    void OpenLevelUp(const SurvivorsPlayerComponent& player);
    void GenerateLevelUpChoices(const SurvivorsPlayerComponent& player);
    void ApplyLevelUpChoice(World& world, int index);

    State       m_state = State::Menu;
    int         m_kills = 0;
    float       m_time = 0.0f;
    float       m_spawnTimer = 2.0f;
    float       m_eliteTimer = 45.0f;
    float       m_difficulty = 0.0f;
    std::string m_gemTexture = "gem_proc";
    bool        m_wantsQuit = false;

    bool        m_showSettings = false;
    State       m_settingsReturn = State::Menu;
    float       m_toastTimer = 0.0f;
    std::string m_toastText;

    SurvivorsUIAssets     m_ui;
    bool                  m_uiReady = false;
    SurvivorsLevelUpChoice m_levelChoices[3];
    int                   m_selectedSkill = -1;
    std::string           m_pauseNotice;
    bool                  m_confirmExit = false;
};

// 反射：在 World 初始化后调用，保证新进程读档/脚本能找到全部玩法组件
void RegisterSurvivorsComponents();

// 关卡
void BuildSurvivorsLevel(World& world, AssetManager& assets);

} // namespace erm
