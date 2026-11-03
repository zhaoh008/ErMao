// ErMao Engine - 2D 幸存者玩法实现
#include "game/Survivors.h"

#include "asset/AssetManager.h"
#include "core/FileSystem.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/Timer.h"
#include "platform/Audio.h"
#include "platform/Input.h"
#include "renderer/RenderScene.h"
#include "scene/Components.h"
#include "scene/GameObject.h"
#include "scene/World.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace erm {

namespace {

// ================================================================ 全局状态
std::vector<GameObject*> g_enemies; // 敌人注册表（技能/投射物用，避免遍历整个场景）
std::vector<GameObject*> g_pickups; // 技能强化掉落物（HUD 标签用）

bool  g_inputEnabled = true;
int   g_kills = 0;
int   g_killsSinceDrop = 0;

// 不要用浮点 LCG：float 乘加后取模会退化成 0 / 0 / 0.655 的短周期，
// 导致掉落类型、刷怪角度和特效几乎固定。mt19937 的 24 位结果足够用。
std::mt19937 g_rng(0x12345u);
std::mt19937 g_fxRng(0x271828u);

struct MapObstacle {
    Vec2 center;
    Vec2 half;
};
std::vector<MapObstacle> g_obstacles;
std::vector<Vec2>        g_torches;

// ---------------------------------------------------------------- 设置
struct SurvivorsSettings {
    bool  musicEnabled = true;
    bool  sfxEnabled = true;
    float masterVolume = 0.8f;
    float musicVolume = 0.45f;
    float sfxVolume = 0.9f;
};

SurvivorsSettings g_settings;
bool              g_settingsLoaded = false;

std::string SettingsPath() {
    return FileSystem::Join(FileSystem::WritableAssetDir(), "survivors_settings.json");
}

std::string SavePath() {
    return FileSystem::Join(FileSystem::Join(FileSystem::WritableAssetDir(), "saves"), "survivors_save.json");
}

void ApplySurvivorsAudioSettings() {
    AudioSystem& audio = AudioSystem::Get();
    audio.SetMasterVolume(g_settings.masterVolume);
    audio.SetMusicEnabled(g_settings.musicEnabled);
    audio.SetSfxEnabled(g_settings.sfxEnabled);
    audio.SetMusicVolume(g_settings.musicVolume);
    audio.SetSfxVolume(g_settings.sfxVolume);
}

void LoadSurvivorsSettings() {
    if (g_settingsLoaded) return;
    g_settingsLoaded = true;

    std::string text;
    if (!FileSystem::ReadTextFile(SettingsPath(), text)) {
        ApplySurvivorsAudioSettings();
        return;
    }
    std::string error;
    JsonValue root = JsonValue::Parse(text, &error);
    if (!error.empty() || !root.IsObject()) {
        LogWarn("[2D] 设置文件解析失败，使用默认设置: %s", error.c_str());
        ApplySurvivorsAudioSettings();
        return;
    }
    g_settings.musicEnabled = root["musicEnabled"].AsBool(g_settings.musicEnabled);
    g_settings.sfxEnabled = root["sfxEnabled"].AsBool(g_settings.sfxEnabled);
    g_settings.masterVolume = Clamp(root["masterVolume"].AsFloat(g_settings.masterVolume), 0.0f, 1.0f);
    g_settings.musicVolume = Clamp(root["musicVolume"].AsFloat(g_settings.musicVolume), 0.0f, 1.0f);
    g_settings.sfxVolume = Clamp(root["sfxVolume"].AsFloat(g_settings.sfxVolume), 0.0f, 1.0f);
    ApplySurvivorsAudioSettings();
}

void SaveSurvivorsSettings() {
    JsonValue root = JsonValue::MakeObject();
    root.Set("musicEnabled", g_settings.musicEnabled);
    root.Set("sfxEnabled", g_settings.sfxEnabled);
    root.Set("masterVolume", g_settings.masterVolume);
    root.Set("musicVolume", g_settings.musicVolume);
    root.Set("sfxVolume", g_settings.sfxVolume);
    if (!FileSystem::WriteTextFile(SettingsPath(), root.Dump(2))) {
        LogWarn("[2D] 设置保存失败: %s", SettingsPath().c_str());
    }
}

// ---------------------------------------------------------------- 随机数
float Random01() { return static_cast<float>(g_rng() & 0x00FFFFFFu) / 16777216.0f; }

float FxRandom01() { return static_cast<float>(g_fxRng() & 0x00FFFFFFu) / 16777216.0f; }

float RandomRange(float lo, float hi) { return lo + (hi - lo) * Random01(); }

Vec2 SafeDir(const Vec2& v) {
    const float len = Length(v);
    return len > 1e-5f ? v / len : Vec2(0.0f, 0.0f);
}

// ---------------------------------------------------------------- 掉落物信息
struct PickupInfo {
    const char* label;
    Vec4        color;
};

PickupInfo GetPickupInfo(int type) {
    switch (static_cast<SurvivorsPickupType>(type)) {
        case SurvivorsPickupType::ArrowSpread: return {"八向散射", Vec4(1.00f, 0.82f, 0.25f, 1.0f)};
        case SurvivorsPickupType::ArrowAttackSpeed: return {"箭矢攻速", Vec4(0.35f, 0.90f, 1.00f, 1.0f)};
        case SurvivorsPickupType::ArrowAttackDamage: return {"箭矢攻击", Vec4(1.00f, 0.40f, 0.35f, 1.0f)};
        case SurvivorsPickupType::ArrowHealthPotion: return {"血瓶", Vec4(1.00f, 0.35f, 0.55f, 1.0f)};
        case SurvivorsPickupType::ArrowRange: return {"箭矢范围", Vec4(0.45f, 1.00f, 0.65f, 1.0f)};
        case SurvivorsPickupType::LightningUpgrade: return {"雷霆强化", Vec4(1.00f, 0.95f, 0.35f, 1.0f)};
        case SurvivorsPickupType::BladeUpgrade: return {"回旋刃强化", Vec4(0.80f, 0.86f, 0.98f, 1.0f)};
        case SurvivorsPickupType::FrostUpgrade: return {"霜环强化", Vec4(0.55f, 0.85f, 1.00f, 1.0f)};
        case SurvivorsPickupType::PoisonUpgrade: return {"毒雾强化", Vec4(0.50f, 1.00f, 0.45f, 1.0f)};
        case SurvivorsPickupType::FireballUpgrade: return {"火球强化", Vec4(1.00f, 0.55f, 0.20f, 1.0f)};
        default: return {"强化", Vec4(1.0f, 1.0f, 1.0f, 1.0f)};
    }
}

ImU32 ToImColor(const Vec4& color, float alphaScale = 1.0f) {
    const int r = static_cast<int>(Clamp01(color.x) * 255.0f);
    const int g = static_cast<int>(Clamp01(color.y) * 255.0f);
    const int b = static_cast<int>(Clamp01(color.z) * 255.0f);
    const int a = static_cast<int>(Clamp01(color.w * alphaScale) * 255.0f);
    return IM_COL32(r, g, b, a);
}

// ---------------------------------------------------------------- 飘字
struct DamageNumber2D {
    Vec2  position;
    Vec2  velocity;
    float age = 0.0f;
    float life = 0.85f;
    float value = 0.0f;
};

std::vector<DamageNumber2D> g_damageNumbers;

void SpawnDamageNumber(const Vec2& position, float value) {
    if (g_damageNumbers.size() >= 128) g_damageNumbers.erase(g_damageNumbers.begin());
    DamageNumber2D number;
    number.position = position;
    number.velocity = Vec2((FxRandom01() - 0.5f) * 1.0f, 2.0f);
    number.life = 0.9f + FxRandom01() * 0.2f;
    number.value = value;
    g_damageNumbers.push_back(number);
}

void UpdateDamageNumbers(float dt) {
    for (DamageNumber2D& number : g_damageNumbers) {
        number.age += dt;
        number.position += number.velocity * dt;
        number.velocity.y -= 2.0f * dt;
    }
    g_damageNumbers.erase(
        std::remove_if(g_damageNumbers.begin(), g_damageNumbers.end(),
                       [](const DamageNumber2D& number) { return number.age >= number.life; }),
        g_damageNumbers.end());
}

// ---------------------------------------------------------------- 技能特效
struct LightningFx {
    std::vector<Vec2> points;
    float             age = 0.0f;
    float             life = 0.30f;
    float             width = 2.5f;
};

struct FrostRingFx {
    Vec2  center;
    float radius = 0.0f;
    float maxRadius = 4.0f;
    float age = 0.0f;
    float life = 0.55f;
};

struct ExplosionFx {
    Vec2  center;
    float radius = 0.0f;
    float maxRadius = 1.6f;
    float age = 0.0f;
    float life = 0.45f;
    Vec4  color{1.0f, 0.55f, 0.2f, 1.0f};
};

struct FireballFx {
    Vec2  position;
    Vec2  velocity;
    u64   targetId = 0;
    float damage = 0.0f;
    float explosionRadius = 1.8f;
    float life = 4.0f;
};

std::vector<LightningFx> g_lightningFx;
std::vector<FrostRingFx> g_frostFx;
std::vector<ExplosionFx> g_explosionFx;
std::vector<FireballFx>  g_fireballs;

void AddLightningBolt(const Vec2& from, const Vec2& to, float width) {
    LightningFx fx;
    fx.width = width;
    const Vec2 delta = to - from;
    const Vec2 perp = SafeDir(Vec2(-delta.y, delta.x));
    const int  segments = 7;
    fx.points.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        Vec2        point = from + delta * t;
        if (i > 0 && i < segments) {
            point += perp * ((FxRandom01() - 0.5f) * 0.9f * std::sin(t * kPi));
        }
        fx.points.push_back(point);
    }
    g_lightningFx.push_back(std::move(fx));
}

void SpawnExplosion(const Vec2& center, float radius, const Vec4& color) {
    ExplosionFx fx;
    fx.center = center;
    fx.maxRadius = radius;
    fx.color = color;
    g_explosionFx.push_back(fx);
}

void UpdateSkillEffects(float dt) {
    for (LightningFx& fx : g_lightningFx) fx.age += dt;
    g_lightningFx.erase(
        std::remove_if(g_lightningFx.begin(), g_lightningFx.end(),
                       [](const LightningFx& fx) { return fx.age >= fx.life; }),
        g_lightningFx.end());

    for (FrostRingFx& fx : g_frostFx) {
        fx.age += dt;
        const float t = Clamp01(fx.age / fx.life);
        fx.radius = fx.maxRadius * (1.0f - (1.0f - t) * (1.0f - t));
    }
    g_frostFx.erase(
        std::remove_if(g_frostFx.begin(), g_frostFx.end(), [](const FrostRingFx& fx) { return fx.age >= fx.life; }),
        g_frostFx.end());

    for (ExplosionFx& fx : g_explosionFx) {
        fx.age += dt;
        const float t = Clamp01(fx.age / fx.life);
        fx.radius = fx.maxRadius * (0.25f + 0.75f * t);
    }
    g_explosionFx.erase(
        std::remove_if(g_explosionFx.begin(), g_explosionFx.end(),
                       [](const ExplosionFx& fx) { return fx.age >= fx.life; }),
        g_explosionFx.end());
}

void ClearSkillEffects() {
    g_lightningFx.clear();
    g_frostFx.clear();
    g_explosionFx.clear();
    g_fireballs.clear();
    g_damageNumbers.clear();
}

// ---------------------------------------------------------------- 场景工具
GameObject* FindPlayer(World& world) { return world.FindByName("玩家"); }

GameObject* SpawnSprite(World& world, const std::string& name, const std::string& texture, const Vec2& position,
                        const Vec2& size, int layer) {
    GameObject* object = world.CreateGameObject(name);
    object->SetPosition(Vec3(position.x, position.y, 0.0f));
    auto* sprite = object->AddComponent<SpriteRendererComponent>();
    sprite->texture = texture;
    sprite->size = size;
    sprite->layer = layer;
    return object;
}

void SpawnGem(World& world, const Vec2& position, float value) {
    GameObject* gem = SpawnSprite(world, "宝石", "gem_proc", position, Vec2(0.5f, 0.5f), 3);
    auto*       component = gem->AddComponent<SurvivorsGemComponent>();
    component->value = value;
    component->magnetRange = 6.0f;
}

void SpawnPickup(World& world, const Vec2& position, int type) {
    GameObject* pickup = SpawnSprite(world, "强化道具", "pickup_proc", position, Vec2(0.85f, 0.85f), 4);
    auto*       sprite = pickup->GetComponent<SpriteRendererComponent>();
    if (sprite) sprite->color = GetPickupInfo(type).color;
    auto* component = pickup->AddComponent<SurvivorsPickupComponent>();
    component->type = type;
    g_pickups.push_back(pickup);
}

int PickPickupType(const SurvivorsPlayerComponent& player) {
    struct WeightedPickup {
        int   type;
        float weight;
    };
    std::vector<WeightedPickup> pool;
    bool spreadOnGround = false;
    for (GameObject* pickup : g_pickups) {
        if (!pickup) continue;
        auto* component = pickup->GetComponent<SurvivorsPickupComponent>();
        if (component && component->type == static_cast<int>(SurvivorsPickupType::ArrowSpread)) {
            spreadOnGround = true;
            break;
        }
    }
    if (!player.arrowSpread && !spreadOnGround) {
        pool.push_back({static_cast<int>(SurvivorsPickupType::ArrowSpread), 14.0f});
    }
    pool.push_back({static_cast<int>(SurvivorsPickupType::ArrowAttackSpeed), 18.0f});
    pool.push_back({static_cast<int>(SurvivorsPickupType::ArrowAttackDamage), 18.0f});
    pool.push_back({static_cast<int>(SurvivorsPickupType::ArrowRange), 14.0f});
    pool.push_back({static_cast<int>(SurvivorsPickupType::ArrowHealthPotion),
                    player.hp < player.maxHp * 0.65f ? 26.0f : 9.0f});
    pool.push_back({static_cast<int>(SurvivorsPickupType::LightningUpgrade), 8.0f});
    pool.push_back({static_cast<int>(SurvivorsPickupType::BladeUpgrade), 8.0f});
    pool.push_back({static_cast<int>(SurvivorsPickupType::FrostUpgrade), 8.0f});
    pool.push_back({static_cast<int>(SurvivorsPickupType::PoisonUpgrade), 8.0f});
    pool.push_back({static_cast<int>(SurvivorsPickupType::FireballUpgrade), 8.0f});

    float total = 0.0f;
    for (const WeightedPickup& entry : pool) total += entry.weight;
    float roll = Random01() * total;
    for (const WeightedPickup& entry : pool) {
        roll -= entry.weight;
        if (roll <= 0.0f) return entry.type;
    }
    return pool.back().type;
}

void SpawnPickupForKill(World& world, const Vec2& position, const SurvivorsEnemyComponent& enemy) {
    GameObject* playerObject = FindPlayer(world);
    auto*       player = playerObject ? playerObject->GetComponent<SurvivorsPlayerComponent>() : nullptr;
    if (!player) return;

    ++g_killsSinceDrop;
    const bool elite = enemy.type == static_cast<int>(SurvivorsEnemyType::Elite);
    const float chance = elite ? 1.0f : 0.36f;
    if (Random01() >= chance && g_killsSinceDrop < 8) return;

    const int drops = (std::max)(1, enemy.dropCount);
    for (int i = 0; i < drops; ++i) {
        const Vec2 offset(RandomRange(-0.45f, 0.45f), RandomRange(-0.35f, 0.35f));
        SpawnPickup(world, position + offset, PickPickupType(*player));
    }
    g_killsSinceDrop = 0;
}

// ---------------------------------------------------------------- 碰撞
bool CircleHitsObstacle(const Vec2& position, float radius) {
    for (const MapObstacle& obstacle : g_obstacles) {
        const float closestX = Clamp(position.x, obstacle.center.x - obstacle.half.x,
                                     obstacle.center.x + obstacle.half.x);
        const float closestY = Clamp(position.y, obstacle.center.y - obstacle.half.y,
                                     obstacle.center.y + obstacle.half.y);
        const float dx = position.x - closestX;
        const float dy = position.y - closestY;
        if (dx * dx + dy * dy < radius * radius) return true;
    }
    return false;
}

Vec2 MoveWithObstacles(const Vec2& position, const Vec2& delta, float radius) {
    if (Length(delta) < 1e-6f) return position;
    const Vec2 target = position + delta;
    if (!CircleHitsObstacle(target, radius)) return target;
    const Vec2 xOnly(position.x + delta.x, position.y);
    if (!CircleHitsObstacle(xOnly, radius)) return xOnly;
    const Vec2 yOnly(position.x, position.y + delta.y);
    if (!CircleHitsObstacle(yOnly, radius)) return yOnly;
    return position;
}

// ---------------------------------------------------------------- 敌人查询/伤害
GameObject* FindNearestEnemy(const Vec2& origin, float range, const std::vector<GameObject*>* exclude = nullptr) {
    GameObject* nearest = nullptr;
    float       bestDistance = range;
    for (GameObject* enemy : g_enemies) {
        if (!enemy || !enemy->Active()) continue;
        if (exclude && std::find(exclude->begin(), exclude->end(), enemy) != exclude->end()) continue;
        const Vec2  position(enemy->Position().x, enemy->Position().y);
        const float distance = Length(position - origin);
        if (distance < bestDistance) {
            bestDistance = distance;
            nearest = enemy;
        }
    }
    return nearest;
}

void GatherEnemiesInRadius(const Vec2& origin, float radius, std::vector<GameObject*>& out) {
    out.clear();
    for (GameObject* enemy : g_enemies) {
        if (!enemy || !enemy->Active()) continue;
        const Vec2 position(enemy->Position().x, enemy->Position().y);
        if (Length(position - origin) <= radius) out.push_back(enemy);
    }
}

double g_lastHitSfxTime = -10.0;
double g_lastGemSfxTime = -10.0;

void PlayHitSfx() {
    const double now = Timer::Now();
    if (now - g_lastHitSfxTime < 0.045) return;
    g_lastHitSfxTime = now;
    PlaySfx("hit", 0.14f, 0.95f + Random01() * 0.12f);
}

void PlayGemSfx() {
    const double now = Timer::Now();
    if (now - g_lastGemSfxTime < 0.07) return;
    g_lastGemSfxTime = now;
    PlaySfx("pickup", 0.20f, 1.10f + Random01() * 0.15f);
}

void DamageEnemy(GameObject* enemy, float damage) {
    if (!enemy || !enemy->Active()) return;
    auto* component = enemy->GetComponent<SurvivorsEnemyComponent>();
    if (!component || component->hp <= 0.0f) return;
    component->hp -= damage;
    component->hitFlash = 0.55f;
    SpawnDamageNumber(Vec2(enemy->Position().x, enemy->Position().y + 0.8f), damage);
    PlayHitSfx();
}

// ---------------------------------------------------------------- 技能特效更新
void UpdateFireballs(World& world, float dt) {
    std::vector<FireballFx> keep;
    keep.reserve(g_fireballs.size());
    for (FireballFx& fireball : g_fireballs) {
        fireball.life -= dt;
        GameObject* target = fireball.targetId != 0 ? world.FindById(fireball.targetId) : nullptr;
        if (target && (!target->Active() || !target->GetComponent<SurvivorsEnemyComponent>())) target = nullptr;

        if (target) {
            const Vec2  targetPosition(target->Position().x, target->Position().y);
            const Vec2  desired = SafeDir(targetPosition - fireball.position) * 9.0f;
            const float follow = 1.0f - std::exp(-7.0f * dt);
            fireball.velocity.x = Lerp(fireball.velocity.x, desired.x, follow);
            fireball.velocity.y = Lerp(fireball.velocity.y, desired.y, follow);
        }

        fireball.position += fireball.velocity * dt;

        bool explode = fireball.life <= 0.0f;
        if (target) {
            const Vec2 targetPosition(target->Position().x, target->Position().y);
            if (Length(targetPosition - fireball.position) < 0.55f) explode = true;
        }

        if (explode) {
            std::vector<GameObject*> enemies;
            GatherEnemiesInRadius(fireball.position, fireball.explosionRadius, enemies);
            for (GameObject* enemy : enemies) DamageEnemy(enemy, fireball.damage);
            SpawnExplosion(fireball.position, fireball.explosionRadius, Vec4(1.0f, 0.55f, 0.20f, 1.0f));
            PlaySfx("hit", 0.42f, 0.70f);
        } else {
            keep.push_back(fireball);
        }
    }
    g_fireballs.swap(keep);
}

// ---------------------------------------------------------------- 敌人外观
void ApplyTileSprite(SpriteRendererComponent* sprite, int row, int col) {
    constexpr float tileWidth = 16.0f / 192.0f;
    constexpr float tileHeight = 16.0f / 176.0f;
    sprite->texture = "sprites/kenney_tilemap.png";
    sprite->uv = Vec4(static_cast<float>(col) * tileWidth, static_cast<float>(row) * tileHeight,
                      static_cast<float>(col + 1) * tileWidth, static_cast<float>(row + 1) * tileHeight);
}

struct EnemyArchetype {
    const char* name;
    float       hp;
    float       speed;
    float       damage;
    float       xp;
    float       scale;
    float       contactRadius;
    float       attackInterval;
    Vec4        color;
    int         tileRow;
    int         tileCol;
    bool        animator;
};

const EnemyArchetype kEnemyArchetypes[] = {
    // name          hp    speed  dmg   xp   scale contact atkInt color                              row col anim
    {"兽人",         18.0f, 1.90f, 8.0f, 3.0f, 1.70f, 0.75f, 0.80f, Vec4(1.00f, 1.00f, 1.00f, 1.00f), -1, -1, true},
    {"史莱姆",       34.0f, 1.15f, 5.0f, 4.0f, 1.45f, 0.70f, 1.10f, Vec4(0.70f, 1.00f, 0.75f, 1.00f),  9,  0, false},
    {"幽灵",         12.0f, 3.10f, 7.0f, 2.5f, 1.50f, 0.65f, 0.70f, Vec4(0.78f, 0.88f, 1.00f, 0.88f), 10,  1, false},
    {"蜘蛛",         10.0f, 3.80f, 5.0f, 2.0f, 1.25f, 0.60f, 0.55f, Vec4(1.00f, 0.86f, 0.76f, 1.00f), 10,  2, false},
    {"骷髅骑士",     62.0f, 1.60f, 12.0f, 8.0f, 1.85f, 0.85f, 0.95f, Vec4(0.82f, 0.88f, 1.00f, 1.00f),  8,  0, false},
    {"兽人精英",    120.0f, 1.45f, 16.0f, 18.0f, 2.60f, 1.10f, 1.00f, Vec4(1.00f, 0.65f, 0.45f, 1.00f), -1, -1, true},
};

int PickEnemyType(float time) {
    struct Weight {
        int   type;
        float weight;
    };
    std::vector<Weight> pool;
    pool.push_back({static_cast<int>(SurvivorsEnemyType::Orc), 1.0f});
    if (time > 15.0f) pool.push_back({static_cast<int>(SurvivorsEnemyType::Slime), 0.85f});
    if (time > 30.0f) pool.push_back({static_cast<int>(SurvivorsEnemyType::Spider), 0.80f});
    if (time > 45.0f) pool.push_back({static_cast<int>(SurvivorsEnemyType::Ghost), 0.70f});
    if (time > 65.0f) pool.push_back({static_cast<int>(SurvivorsEnemyType::Knight), 0.75f});
    if (time > 120.0f) pool.push_back({static_cast<int>(SurvivorsEnemyType::Knight), 0.45f});

    float total = 0.0f;
    for (const Weight& entry : pool) total += entry.weight;
    float roll = Random01() * total;
    for (const Weight& entry : pool) {
        roll -= entry.weight;
        if (roll <= 0.0f) return entry.type;
    }
    return pool.back().type;
}

// ---------------------------------------------------------------- 设置/存档 UI 工具
void DrawToast(const std::string& text, float timer, float viewportX, float viewportY, float viewportWidth,
               float viewportHeight) {
    if (timer <= 0.0f || text.empty()) return;
    const float alpha = Clamp01(timer / 0.35f);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(18.0f, FLT_MAX, 0.0f, text.c_str());
    const ImVec2 center(viewportX + viewportWidth * 0.5f, viewportY + viewportHeight - 78.0f);
    const ImVec2 min(center.x - textSize.x * 0.5f - 16.0f, center.y - textSize.y * 0.5f - 9.0f);
    const ImVec2 max(center.x + textSize.x * 0.5f + 16.0f, center.y + textSize.y * 0.5f + 9.0f);
    draw->AddRectFilled(min, max, IM_COL32(6, 8, 12, static_cast<int>(205 * alpha)), 8.0f);
    draw->AddRect(min, max, IM_COL32(240, 205, 120, static_cast<int>(150 * alpha)), 8.0f);
    draw->AddText(ImGui::GetFont(), 18.0f, ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f),
                  IM_COL32(255, 226, 150, static_cast<int>(255 * alpha)), text.c_str());
}

} // namespace

// ================================================================ 玩家：设置/数值
void SurvivorsPlayerComponent::SetInputEnabled(bool enabled) { g_inputEnabled = enabled; }
bool SurvivorsPlayerComponent::InputEnabled() { return g_inputEnabled; }

void SurvivorsPlayerComponent::RecomputeAttackInterval() {
    m_killIntervalScale = 1.0f / (1.0f + 0.03f * static_cast<float>(killCount));
    m_dropIntervalScale = std::pow(0.93f, static_cast<float>(arrowSpeedLevel));
    attackInterval = Clamp(baseAttackInterval * m_levelIntervalScale * m_killIntervalScale * m_dropIntervalScale,
                           0.09f, baseAttackInterval);
}

void SurvivorsPlayerComponent::RecomputeAttackDamage() { attackDamage = ArrowDamage(); }

void SurvivorsPlayerComponent::RefreshDerivedStats() {
    // 等级带来的攻速缩放可以直接由 level 还原，读档时不会丢掉升级攻速
    m_levelIntervalScale = (std::max)(0.25f, std::pow(0.9f, static_cast<float>(level - 1)));
    RecomputeAttackInterval();
    RecomputeAttackDamage();
}

float SurvivorsPlayerComponent::KillAttackSpeedBonus() const {
    return m_killIntervalScale > 1e-5f ? 1.0f / m_killIntervalScale - 1.0f : 0.0f;
}

float SurvivorsPlayerComponent::ArrowSpeedBonus() const {
    const float scale = m_killIntervalScale * m_dropIntervalScale;
    return scale > 1e-5f ? 1.0f / scale - 1.0f : 0.0f;
}

float SurvivorsPlayerComponent::ArrowDamage() const {
    const float levelBonus = 1.0f + 0.16f * static_cast<float>(arrowDamageLevel);
    return (baseAttackDamage + static_cast<float>(level - 1) * 4.0f) * levelBonus;
}

float SurvivorsPlayerComponent::ArrowRange() const { return baseAttackRange + 1.0f * static_cast<float>(arrowRangeLevel); }

float SurvivorsPlayerComponent::ArrowProjectileLife() const {
    return baseProjectileLife + 0.14f * static_cast<float>(arrowRangeLevel);
}

float SurvivorsPlayerComponent::BladeAngle() const { return m_bladeAngle; }

float SurvivorsPlayerComponent::LightningDamage() const {
    return 16.0f * std::pow(1.22f, static_cast<float>(lightningLevel - 1));
}
int SurvivorsPlayerComponent::LightningTargets() const { return (std::min)(6, 1 + (lightningLevel - 1) / 2); }
int SurvivorsPlayerComponent::LightningChains() const { return (std::min)(4, (lightningLevel - 1) / 3); }
float SurvivorsPlayerComponent::LightningRadius() const { return 7.5f + 0.4f * static_cast<float>(lightningLevel - 1); }
float SurvivorsPlayerComponent::LightningCooldown() const {
    return (std::max)(1.0f, 2.8f * std::pow(0.95f, static_cast<float>(lightningLevel - 1)));
}

float SurvivorsPlayerComponent::BladeDamage() const {
    return 8.0f * std::pow(1.16f, static_cast<float>(bladeLevel - 1));
}
int SurvivorsPlayerComponent::BladeCount() const { return (std::min)(8, 2 + (bladeLevel - 1) / 2); }
float SurvivorsPlayerComponent::BladeRadius() const { return 2.1f + 0.22f * static_cast<float>(bladeLevel - 1); }
float SurvivorsPlayerComponent::BladeSpeed() const { return 4.2f + 0.15f * static_cast<float>(bladeLevel); }

float SurvivorsPlayerComponent::FrostDamage() const {
    return 12.0f * std::pow(1.2f, static_cast<float>(frostLevel - 1));
}
float SurvivorsPlayerComponent::FrostRadius() const { return 4.2f + 0.35f * static_cast<float>(frostLevel - 1); }
float SurvivorsPlayerComponent::FrostSlow() const {
    return (std::min)(0.70f, 0.42f + 0.015f * static_cast<float>(frostLevel));
}
float SurvivorsPlayerComponent::FrostCooldown() const {
    return (std::max)(1.5f, 4.5f * std::pow(0.96f, static_cast<float>(frostLevel - 1)));
}

float SurvivorsPlayerComponent::PoisonDps() const {
    return 7.0f * std::pow(1.18f, static_cast<float>(poisonLevel - 1));
}
float SurvivorsPlayerComponent::PoisonRadius() const { return 2.8f + 0.28f * static_cast<float>(poisonLevel - 1); }

float SurvivorsPlayerComponent::FireballDamage() const {
    return 22.0f * std::pow(1.2f, static_cast<float>(fireballLevel - 1));
}
int SurvivorsPlayerComponent::FireballCount() const { return (std::min)(4, 1 + (fireballLevel - 1) / 3); }
float SurvivorsPlayerComponent::FireballExplosionRadius() const { return 1.8f + 0.1f * static_cast<float>(fireballLevel); }
float SurvivorsPlayerComponent::FireballCooldown() const {
    return (std::max)(1.2f, 3.4f * std::pow(0.95f, static_cast<float>(fireballLevel - 1)));
}

void SurvivorsPlayerComponent::ApplyKillCount(int kills) {
    if (kills == killCount) return;
    killCount = kills;
    RecomputeAttackInterval();
}

void SurvivorsPlayerComponent::ResetCombatStats() {
    level = 1;
    xp = 0.0f;
    xpNext = 12.0f;
    attackDamage = baseAttackDamage;
    maxHp = baseMaxHp;
    hp = maxHp;
    alive = true;
    invulnerable = 0.0f;
    hitFlash = 0.0f;
    killCount = 0;
    arrowSpread = false;
    arrowSpeedLevel = 0;
    arrowDamageLevel = 0;
    arrowRangeLevel = 0;
    facing = Vec2(1.0f, 0.0f);
    lightningLevel = 1;
    bladeLevel = 1;
    frostLevel = 1;
    poisonLevel = 1;
    fireballLevel = 1;
    pickupToast.clear();
    pickupToastTimer = 0.0f;
    m_levelIntervalScale = 1.0f;
    m_killIntervalScale = 1.0f;
    m_dropIntervalScale = 1.0f;
    m_attackTimer = 0.0f;
    m_lightningTimer = 0.8f;
    m_frostTimer = 1.8f;
    m_fireballTimer = 2.4f;
    m_poisonTick = 0.0f;
    m_bladeTick = 0.0f;
    m_bladeAngle = 0.0f;
    m_auraSfxTimer = 0.0f;
    RefreshDerivedStats();
}

void SurvivorsPlayerComponent::ShowPickupToast(const std::string& text) {
    pickupToast = text;
    pickupToastTimer = 2.3f;
}

void SurvivorsPlayerComponent::ApplyPickup(int type) {
    const SurvivorsPickupType pickupType = static_cast<SurvivorsPickupType>(type);
    switch (pickupType) {
        case SurvivorsPickupType::ArrowSpread:
            if (!arrowSpread) {
                arrowSpread = true;
                ShowPickupToast("获得：八向散射！");
                PlaySfx("levelup", 0.55f, 1.15f);
            } else {
                ++arrowDamageLevel;
                RecomputeAttackDamage();
                ShowPickupToast("重复散射：箭矢攻击力提升");
                PlaySfx("pickup", 0.50f, 0.90f);
            }
            break;
        case SurvivorsPickupType::ArrowAttackSpeed:
            ++arrowSpeedLevel;
            RecomputeAttackInterval();
            ShowPickupToast("箭矢攻速提升");
            PlaySfx("pickup", 0.55f, 1.20f);
            break;
        case SurvivorsPickupType::ArrowAttackDamage:
            ++arrowDamageLevel;
            RecomputeAttackDamage();
            ShowPickupToast("箭矢攻击力提升");
            PlaySfx("pickup", 0.55f, 0.92f);
            break;
        case SurvivorsPickupType::ArrowHealthPotion:
            if (hp < maxHp) {
                hp = (std::min)(maxHp, hp + maxHp * 0.35f + 8.0f);
                ShowPickupToast("获得血瓶：恢复生命");
                PlaySfx("pickup", 0.70f, 0.85f);
            } else {
                AddXp(6.0f);
                ShowPickupToast("血瓶：生命已满，转化为经验");
                PlaySfx("pickup", 0.50f, 1.30f);
            }
            break;
        case SurvivorsPickupType::ArrowRange:
            ++arrowRangeLevel;
            ShowPickupToast("箭矢攻击范围提升");
            PlaySfx("pickup", 0.55f, 1.35f);
            break;
        case SurvivorsPickupType::LightningUpgrade:
            ++lightningLevel;
            m_lightningTimer = 0.0f;
            ShowPickupToast("雷霆强化 Lv." + std::to_string(lightningLevel));
            PlaySfx("levelup", 0.50f, 1.22f);
            break;
        case SurvivorsPickupType::BladeUpgrade:
            ++bladeLevel;
            ShowPickupToast("回旋刃强化 Lv." + std::to_string(bladeLevel));
            PlaySfx("levelup", 0.48f, 1.10f);
            break;
        case SurvivorsPickupType::FrostUpgrade:
            ++frostLevel;
            m_frostTimer = 0.0f;
            ShowPickupToast("霜环强化 Lv." + std::to_string(frostLevel));
            PlaySfx("levelup", 0.48f, 1.35f);
            break;
        case SurvivorsPickupType::PoisonUpgrade:
            ++poisonLevel;
            m_poisonTick = 0.0f;
            ShowPickupToast("毒雾强化 Lv." + std::to_string(poisonLevel));
            PlaySfx("levelup", 0.48f, 0.92f);
            break;
        case SurvivorsPickupType::FireballUpgrade:
            ++fireballLevel;
            m_fireballTimer = 0.0f;
            ShowPickupToast("火球强化 Lv." + std::to_string(fireballLevel));
            PlaySfx("levelup", 0.48f, 1.05f);
            break;
        default: break;
    }
    LogInfo("[2D] 拾取强化: %s", GetPickupInfo(type).label);
}

void SurvivorsPlayerComponent::AddXp(float amount) {
    xp += amount;
    while (xp >= xpNext) {
        xp -= xpNext;
        ++level;
        xpNext *= 1.35f;
        maxHp += 8.0f;
        hp = (std::min)(maxHp, hp + 8.0f);
        RefreshDerivedStats();
        PlaySfx("levelup", 0.40f, 1.0f);
    }
}

// ================================================================ 玩家：更新
void SurvivorsPlayerComponent::OnUpdate(float dt) {
    if (!alive || !m_owner) return;

    invulnerable = (std::max)(0.0f, invulnerable - dt);
    hitFlash = (std::max)(0.0f, hitFlash - dt * 3.0f);
    pickupToastTimer = (std::max)(0.0f, pickupToastTimer - dt);

    Vec2 move(0.0f, 0.0f);
    if (g_inputEnabled) {
        if (Input::IsKeyDown('A')) move.x -= 1.0f;
        if (Input::IsKeyDown('D')) move.x += 1.0f;
        if (Input::IsKeyDown('W')) move.y += 1.0f;
        if (Input::IsKeyDown('S')) move.y -= 1.0f;
    }

    auto* sprite = m_owner->GetComponent<SpriteRendererComponent>();
    auto* animator = m_owner->GetComponent<SpriteAnimatorComponent>();
    const bool moving = (move.x * move.x + move.y * move.y) > 0.01f;

    if (moving) {
        const Vec2 direction = SafeDir(move);
        Vec3       position = m_owner->Position();
        const Vec2 oldPosition(position.x, position.y);
        Vec2       next = MoveWithObstacles(oldPosition, direction * speed * dt, 0.45f);
        next.x = Clamp(next.x, -arenaHalf.x, arenaHalf.x);
        next.y = Clamp(next.y, -arenaHalf.y, arenaHalf.y);
        position.x = next.x;
        position.y = next.y;
        m_owner->SetPosition(position);
        if (Length(next - oldPosition) > 0.001f) facing = SafeDir(next - oldPosition);

        if (sprite && sprite->texture != "sprites/soldier_walk.png") {
            sprite->texture = "sprites/soldier_walk.png";
            if (animator) animator->Play(12.0f);
        }
        if (sprite) sprite->flipX = facing.x < 0.0f;
    } else if (sprite && sprite->texture != "sprites/soldier_idle.png") {
        sprite->texture = "sprites/soldier_idle.png";
        if (animator) animator->Play(6.0f);
    }

    // 自动攻击：有敌人时锁定最近目标；没有目标时朝面朝方向射击
    m_attackTimer -= dt;
    if (m_attackTimer <= 0.0f) {
        FireAtNearestEnemy(Vec2(m_owner->Position().x, m_owner->Position().y));
        m_attackTimer = attackInterval;
    }

    UpdateSkills(dt);

    if (hitFlash > 0.0f && sprite) {
        sprite->color = Vec4(1.0f, 0.45f, 0.45f, 1.0f);
    } else if (sprite) {
        sprite->color = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

void SurvivorsPlayerComponent::FireArrow(const Vec2& origin, const Vec2& direction, float damageScale) {
    World* world = GetWorld();
    if (!world) return;

    const Vec2  dir = SafeDir(direction);
    const Vec2  spawn = origin + dir * 0.55f;
    const float angle = std::atan2(dir.y, dir.x);

    GameObject* arrow = SpawnSprite(*world, "箭", "sprites/arrow.png", spawn, Vec2(0.9f, 0.9f), 2);
    if (auto* sprite = arrow->GetComponent<SpriteRendererComponent>()) {
        sprite->uv = Vec4(0.219f, 0.406f, 0.812f, 0.625f);
        sprite->rotation = angle;
        sprite->color = Vec4(1.0f, 0.95f, 0.6f, 1.0f);
    }

    auto* projectile = arrow->AddComponent<SurvivorsProjectileComponent>();
    projectile->velocity = dir * projectileSpeed;
    projectile->damage = ArrowDamage() * damageScale;
    projectile->life = ArrowProjectileLife();
}

void SurvivorsPlayerComponent::FireAtNearestEnemy(const Vec2& origin) {
    if (arrowSpread) {
        // 八向散射：不再锁定目标，向 8 个方向各射一箭
        for (int i = 0; i < 8; ++i) {
            const float angle = static_cast<float>(i) * kTwoPi / 8.0f;
            FireArrow(origin, Vec2(std::cos(angle), std::sin(angle)), 0.65f);
        }
    } else {
        GameObject* nearest = FindNearestEnemy(origin, ArrowRange());
        const Vec2  direction = nearest ? SafeDir(Vec2(nearest->Position().x, nearest->Position().y) - origin)
                                        : SafeDir(facing);
        FireArrow(origin, direction, 1.0f);
    }
    PlaySfx("shoot", 0.28f, 0.90f + Random01() * 0.20f);
}

void SurvivorsPlayerComponent::UpdateSkills(float dt) {
    World* world = GetWorld();
    if (!world) return;

    m_lightningTimer -= dt;
    if (m_lightningTimer <= 0.0f) {
        m_lightningTimer = CastLightning() ? LightningCooldown() : 0.35f;
    }

    m_frostTimer -= dt;
    if (m_frostTimer <= 0.0f) {
        CastFrost();
        m_frostTimer = FrostCooldown();
    }

    m_fireballTimer -= dt;
    if (m_fireballTimer <= 0.0f) {
        CastFireball();
        m_fireballTimer = FireballCooldown();
    }

    UpdateBlades(dt);
    UpdateAura(dt);
    UpdateFireballs(*world, dt);
}

bool SurvivorsPlayerComponent::CastLightning() {
    World* world = GetWorld();
    if (!world || !m_owner) return false;

    const Vec2 origin(m_owner->Position().x, m_owner->Position().y);
    std::vector<GameObject*> candidates;
    GatherEnemiesInRadius(origin, LightningRadius(), candidates);
    if (candidates.empty()) return false;

    // 随机洗牌，让每次落雷目标不同
    for (size_t i = candidates.size(); i > 1; --i) {
        const size_t j = static_cast<size_t>(Random01() * static_cast<float>(i)) % i;
        std::swap(candidates[i - 1], candidates[j]);
    }

    const int targetCount = (std::min)(LightningTargets(), static_cast<int>(candidates.size()));
    std::vector<GameObject*> hit;
    for (int i = 0; i < targetCount; ++i) {
        GameObject* target = candidates[static_cast<size_t>(i)];
        if (!target || !target->Active()) continue;
        const Vec2 targetPosition(target->Position().x, target->Position().y);
        DamageEnemy(target, LightningDamage());
        AddLightningBolt(targetPosition + Vec2(FxRandom01() * 0.8f - 0.4f, 5.5f), targetPosition, 3.0f);
        hit.push_back(target);

        // 连锁闪电：从当前目标跳到附近另一个没被劈过的敌人
        GameObject* current = target;
        float       chainDamage = LightningDamage();
        for (int chain = 0; chain < LightningChains(); ++chain) {
            chainDamage *= 0.72f;
            GameObject* next = FindNearestEnemy(Vec2(current->Position().x, current->Position().y), 3.6f, &hit);
            if (!next) break;
            const Vec2 nextPosition(next->Position().x, next->Position().y);
            const Vec2 currentPosition(current->Position().x, current->Position().y);
            DamageEnemy(next, chainDamage);
            AddLightningBolt(currentPosition, nextPosition, 2.0f);
            hit.push_back(next);
            current = next;
        }
    }
    PlaySfx("lightning", 0.50f, 0.95f + Random01() * 0.10f);
    return true;
}

void SurvivorsPlayerComponent::CastFrost() {
    World* world = GetWorld();
    if (!world || !m_owner) return;

    const Vec2 origin(m_owner->Position().x, m_owner->Position().y);
    std::vector<GameObject*> enemies;
    GatherEnemiesInRadius(origin, FrostRadius(), enemies);
    for (GameObject* enemy : enemies) {
        DamageEnemy(enemy, FrostDamage());
        auto* component = enemy->GetComponent<SurvivorsEnemyComponent>();
        if (component) {
            component->slowTimer = 2.4f;
            component->slowFactor = 1.0f - FrostSlow();
        }
    }

    FrostRingFx fx;
    fx.center = origin;
    fx.maxRadius = FrostRadius();
    g_frostFx.push_back(fx);
    PlaySfx("frost", 0.45f, 0.95f + Random01() * 0.10f);
}

void SurvivorsPlayerComponent::CastFireball() {
    World* world = GetWorld();
    if (!world || !m_owner) return;

    const Vec2 origin(m_owner->Position().x, m_owner->Position().y);
    std::vector<GameObject*> candidates;
    GatherEnemiesInRadius(origin, 13.0f, candidates);
    std::sort(candidates.begin(), candidates.end(), [&](GameObject* a, GameObject* b) {
        const float da = Length(Vec2(a->Position().x, a->Position().y) - origin);
        const float db = Length(Vec2(b->Position().x, b->Position().y) - origin);
        return da < db;
    });

    const int count = FireballCount();
    for (int i = 0; i < count; ++i) {
        FireballFx fireball;
        Vec2       direction = SafeDir(facing);
        if (i < static_cast<int>(candidates.size())) {
            direction = SafeDir(Vec2(candidates[static_cast<size_t>(i)]->Position().x,
                                     candidates[static_cast<size_t>(i)]->Position().y) -
                                origin);
            fireball.targetId = candidates[static_cast<size_t>(i)]->Id();
        }
        fireball.position = origin + direction * 0.5f;
        fireball.velocity = direction * 9.0f;
        fireball.damage = FireballDamage();
        fireball.explosionRadius = FireballExplosionRadius();
        fireball.life = 4.0f;
        g_fireballs.push_back(fireball);
    }
    PlaySfx("fireball", 0.50f, 0.95f + Random01() * 0.10f);
}

void SurvivorsPlayerComponent::UpdateBlades(float dt) {
    if (!m_owner) return;
    m_bladeAngle += BladeSpeed() * dt;
    m_bladeTick -= dt;
    if (m_bladeTick > 0.0f) return;

    const Vec2 center(m_owner->Position().x, m_owner->Position().y);
    const int  bladeCount = BladeCount();
    bool       hitAny = false;
    for (int i = 0; i < bladeCount; ++i) {
        const float angle = m_bladeAngle + static_cast<float>(i) * kTwoPi / static_cast<float>(bladeCount);
        const Vec2  blade = center + Vec2(std::cos(angle), std::sin(angle)) * BladeRadius();
        for (GameObject* enemy : g_enemies) {
            if (!enemy || !enemy->Active()) continue;
            const Vec2 position(enemy->Position().x, enemy->Position().y);
            if (Length(position - blade) < 0.65f) {
                DamageEnemy(enemy, BladeDamage());
                hitAny = true;
            }
        }
    }
    m_bladeTick = hitAny ? 0.30f : 0.08f;
    if (hitAny) PlaySfx("blade", 0.24f, 0.95f + Random01() * 0.12f);
}

void SurvivorsPlayerComponent::UpdateAura(float dt) {
    if (!m_owner) return;
    m_auraSfxTimer = (std::max)(0.0f, m_auraSfxTimer - dt);
    m_poisonTick -= dt;
    if (m_poisonTick > 0.0f) return;
    m_poisonTick = 0.4f;

    const Vec2 origin(m_owner->Position().x, m_owner->Position().y);
    std::vector<GameObject*> enemies;
    GatherEnemiesInRadius(origin, PoisonRadius(), enemies);
    for (GameObject* enemy : enemies) {
        DamageEnemy(enemy, PoisonDps() * 0.4f);
        auto* component = enemy->GetComponent<SurvivorsEnemyComponent>();
        if (component) component->poisonTimer = 0.55f;
    }
    if (!enemies.empty() && m_auraSfxTimer <= 0.0f) {
        PlaySfx("poison", 0.16f, 0.95f + Random01() * 0.10f);
        m_auraSfxTimer = 1.6f;
    }
}

// ================================================================ 敌人
void SurvivorsEnemyComponent::OnUpdate(float dt) {
    if (!m_owner) return;
    World* world = GetWorld();
    if (!world) return;

    hitFlash = (std::max)(0.0f, hitFlash - dt * 3.0f);
    slowTimer = (std::max)(0.0f, slowTimer - dt);
    poisonTimer = (std::max)(0.0f, poisonTimer - dt);
    bobPhase += dt;

    if (auto* sprite = m_owner->GetComponent<SpriteRendererComponent>()) {
        Vec4 color = baseColor;
        if (hitFlash > 0.0f) {
            color = Vec4(1.0f, 0.72f, 0.72f, baseColor.w);
        } else if (poisonTimer > 0.0f) {
            color = Vec4(0.55f, 1.0f, 0.55f, baseColor.w);
        } else if (slowTimer > 0.0f) {
            color = Vec4(0.62f, 0.80f, 1.0f, baseColor.w);
        }
        sprite->color = color;
        sprite->offset = Vec2(0.0f, std::sin(bobPhase * 3.0f + bobPhase * 0.7f) * 0.05f);
    }

    if (hp <= 0.0f) {
        const Vec2 position(m_owner->Position().x, m_owner->Position().y);
        SpawnGem(*world, position, xpValue);
        SpawnPickupForKill(*world, position, *this);
        ++g_kills;
        PlaySfx("death", 0.30f, 0.90f + Random01() * 0.20f);
        g_enemies.erase(std::remove(g_enemies.begin(), g_enemies.end(), m_owner), g_enemies.end());
        world->DestroyObject(m_owner);
        return;
    }

    GameObject* player = FindPlayer(*world);
    if (!player) return;
    auto* playerComponent = player->GetComponent<SurvivorsPlayerComponent>();
    if (!playerComponent || !playerComponent->alive) return;

    const Vec2 position(m_owner->Position().x, m_owner->Position().y);
    const Vec2 target(player->Position().x, player->Position().y);
    const Vec2 toTarget = target - position;
    const float distance = Length(toTarget);

    if (auto* sprite = m_owner->GetComponent<SpriteRendererComponent>()) {
        sprite->flipX = toTarget.x > 0.0f; // 兽人贴图默认朝左
    }

    const float stopDistance = 0.80f + contactRadius * 0.35f;
    if (distance > stopDistance) {
        const Vec2 direction = SafeDir(toTarget);
        const float slow = slowTimer > 0.0f ? slowFactor : 1.0f;
        Vec2       delta = direction * speed * slow * dt;

        // 简单分离，避免所有怪物叠成一个点
        Vec2 separation(0.0f, 0.0f);
        for (GameObject* other : g_enemies) {
            if (!other || other == m_owner || !other->Active()) continue;
            const Vec2 otherPosition(other->Position().x, other->Position().y);
            const Vec2 offset = position - otherPosition;
            const float otherDistance = Length(offset);
            if (otherDistance > 0.001f && otherDistance < 0.85f) {
                separation += offset / otherDistance * (0.85f - otherDistance);
            }
        }
        delta += separation * 1.8f * dt;

        Vec3 nextPosition = m_owner->Position();
        Vec2 next(position.x, position.y);
        if (type == static_cast<int>(SurvivorsEnemyType::Ghost)) {
            next = position + delta; // 幽灵穿障碍
        } else {
            next = MoveWithObstacles(position, delta, contactRadius);
            if (Length(next - position) < 0.001f) {
                const Vec2 perpendicular(-direction.y, direction.x);
                next = MoveWithObstacles(position, perpendicular * speed * slow * dt, contactRadius);
                if (Length(next - position) < 0.001f) {
                    next = MoveWithObstacles(position, -perpendicular * speed * slow * dt, contactRadius);
                }
            }
        }
        next.x = Clamp(next.x, -playerComponent->arenaHalf.x, playerComponent->arenaHalf.x);
        next.y = Clamp(next.y, -playerComponent->arenaHalf.y, playerComponent->arenaHalf.y);
        nextPosition.x = next.x;
        nextPosition.y = next.y;
        m_owner->SetPosition(nextPosition);
    } else {
        m_attackTimer -= dt;
        if (m_attackTimer <= 0.0f) {
            if (playerComponent->invulnerable <= 0.0f) {
                playerComponent->hp -= damage;
                playerComponent->invulnerable = 0.35f;
                playerComponent->hitFlash = 1.0f;
                PlaySfx("hit", 0.40f, 0.72f + Random01() * 0.10f);
            }
            m_attackTimer = attackInterval;
        }
    }
}

void SurvivorsEnemyComponent::OnDestroy() {
    // 对象真正释放时同步移除注册表项，避免 World::Clear/RemoveComponent 留下悬垂指针。
    // 只比较指针值，不解引用（此时 GameObject 正在析构）。
    if (!m_owner) return;
    g_enemies.erase(std::remove(g_enemies.begin(), g_enemies.end(), m_owner), g_enemies.end());
}

// ================================================================ 投射物
void SurvivorsProjectileComponent::OnUpdate(float dt) {
    if (!m_owner) return;
    World* world = GetWorld();
    if (!world) return;

    life -= dt;
    if (life <= 0.0f) {
        world->DestroyObject(m_owner);
        return;
    }

    Vec3 position = m_owner->Position();
    position.x += velocity.x * dt;
    position.y += velocity.y * dt;
    m_owner->SetPosition(position);

    for (GameObject* enemy : g_enemies) {
        if (!enemy || !enemy->Active()) continue;
        const Vec2 to(enemy->Position().x - position.x, enemy->Position().y - position.y);
        if (Length(to) < 0.55f) {
            if (auto* component = enemy->GetComponent<SurvivorsEnemyComponent>()) {
                component->hp -= damage;
                component->hitFlash = 1.0f;
                SpawnDamageNumber(Vec2(enemy->Position().x, enemy->Position().y + 0.8f), damage);
                PlayHitSfx();
            }
            world->DestroyObject(m_owner);
            return;
        }
    }
}

// ================================================================ 经验宝石
void SurvivorsGemComponent::OnUpdate(float dt) {
    if (!m_owner) return;
    World* world = GetWorld();
    if (!world) return;

    age += dt;
    GameObject* player = FindPlayer(*world);
    if (!player) return;

    const Vec2 position(m_owner->Position().x, m_owner->Position().y);
    const Vec2 target(player->Position().x, player->Position().y);
    const Vec2 toTarget = target - position;
    const float distance = Length(toTarget);

    // 近距离吸附；掉落 2.5 秒后开始全图回收，避免远程击杀的宝石永远捡不到
    if (distance < magnetRange || age > 2.5f) {
        const Vec2  direction = SafeDir(toTarget);
        Vec3        next = m_owner->Position();
        const float speed = 4.0f + age * 5.0f;
        next.x += direction.x * speed * dt;
        next.y += direction.y * speed * dt;
        m_owner->SetPosition(next);
    }

    if (distance < 0.45f) {
        if (auto* playerComponent = player->GetComponent<SurvivorsPlayerComponent>()) {
            playerComponent->AddXp(value);
        }
        PlayGemSfx();
        world->DestroyObject(m_owner);
    }
}

// ================================================================ 强化掉落物
void SurvivorsPickupComponent::OnUpdate(float dt) {
    if (!m_owner) return;
    World* world = GetWorld();
    if (!world) return;

    age += dt;
    const PickupInfo info = GetPickupInfo(type);
    if (auto* sprite = m_owner->GetComponent<SpriteRendererComponent>()) {
        Vec4 color = info.color;
        color.w = 0.82f + 0.18f * (0.5f + 0.5f * std::sin(age * 5.0f));
        sprite->color = color;
        sprite->offset = Vec2(0.0f, std::sin(age * 4.0f) * 0.12f);
        sprite->rotation = std::sin(age * 2.0f) * 0.15f;
    }

    if (age > 60.0f) {
        world->DestroyObject(m_owner);
        return;
    }

    GameObject* player = FindPlayer(*world);
    if (!player) return;
    auto* playerComponent = player->GetComponent<SurvivorsPlayerComponent>();
    if (!playerComponent) return;

    const Vec2  position(m_owner->Position().x, m_owner->Position().y);
    const Vec2  target(player->Position().x, player->Position().y);
    const Vec2  toTarget = target - position;
    const float distance = Length(toTarget);

    if (age > 3.0f || distance < magnetRange) {
        const Vec2  direction = SafeDir(toTarget);
        Vec3        next = m_owner->Position();
        const float speed = 4.5f + age * 0.5f;
        next.x += direction.x * speed * dt;
        next.y += direction.y * speed * dt;
        m_owner->SetPosition(next);
    }

    if (distance < 0.55f) {
        playerComponent->ApplyPickup(type);
        world->DestroyObject(m_owner);
    }
}

void SurvivorsPickupComponent::OnDestroy() {
    if (!m_owner) return;
    g_pickups.erase(std::remove(g_pickups.begin(), g_pickups.end(), m_owner), g_pickups.end());
}

// ================================================================ 导演：刷怪
void SurvivorsDirector::SpawnEnemy(World& world, const Vec2& position, int type, float hpScale, float speedScale,
                                   float damageScale) {
    const int index = Clamp(type, 0, static_cast<int>(SurvivorsEnemyType::Count) - 1);
    const EnemyArchetype& archetype = kEnemyArchetypes[index];

    Vec2 spawn = position;
    for (int attempt = 0; attempt < 8 && CircleHitsObstacle(spawn, archetype.contactRadius); ++attempt) {
        spawn += Vec2(RandomRange(-1.5f, 1.5f), RandomRange(-1.5f, 1.5f));
    }
    spawn.x = Clamp(spawn.x, -40.0f, 40.0f);
    spawn.y = Clamp(spawn.y, -22.0f, 22.0f);

    GameObject* enemy = SpawnSprite(world, archetype.name,
                                    archetype.animator ? "sprites/orc_walk.png" : "sprites/kenney_tilemap.png",
                                    spawn, Vec2(archetype.scale, archetype.scale), 1);
    if (auto* sprite = enemy->GetComponent<SpriteRendererComponent>()) {
        if (!archetype.animator) ApplyTileSprite(sprite, archetype.tileRow, archetype.tileCol);
        sprite->color = archetype.color;
    }
    if (archetype.animator) {
        auto* animator = enemy->AddComponent<SpriteAnimatorComponent>();
        animator->fps = 10.0f;
        animator->frameMin = Vec2(0.440f, 0.410f);
        animator->frameMax = Vec2(0.660f, 0.580f);
    }

    auto* component = enemy->AddComponent<SurvivorsEnemyComponent>();
    component->type = index;
    component->hp = archetype.hp * hpScale;
    component->maxHp = component->hp;
    component->speed = archetype.speed * speedScale;
    component->damage = archetype.damage * damageScale;
    component->attackInterval = archetype.attackInterval;
    component->xpValue = archetype.xp;
    component->contactRadius = archetype.contactRadius;
    component->baseColor = archetype.color;
    component->bobPhase = Random01() * kTwoPi;
    component->dropCount = index == static_cast<int>(SurvivorsEnemyType::Elite)
                               ? 3
                               : (index == static_cast<int>(SurvivorsEnemyType::Knight) ? 2 : 1);
    g_enemies.push_back(enemy);

    static bool loggedTypes[static_cast<int>(SurvivorsEnemyType::Count)] = {};
    if (!loggedTypes[index]) {
        loggedTypes[index] = true;
        LogInfo("[2D] 新怪物类型: %s（生命 %.0f / 速度 %.2f / 伤害 %.0f）", archetype.name, component->maxHp,
                component->speed, component->damage);
    }
}

void SurvivorsDirector::SpawnWave(World& world) {
    if (g_enemies.size() > 140) return; // 上限，避免刷爆
    GameObject* player = FindPlayer(world);
    const Vec2  center = player ? Vec2(player->Position().x, player->Position().y) : Vec2(0.0f, 0.0f);

    const int count = 2 + (std::min)(12, static_cast<int>(m_time / 10.0f));
    const float hpScale = 1.0f + m_time / 55.0f + std::pow(m_time / 150.0f, 2.0f);
    const float speedScale = 1.0f + (std::min)(0.8f, m_time / 420.0f);
    const float damageScale = 1.0f + m_time / 110.0f;

    for (int i = 0; i < count; ++i) {
        const float angle = Random01() * kTwoPi;
        const float radius = RandomRange(14.0f, 20.0f);
        const Vec2  position(center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius);
        SpawnEnemy(world, position, PickEnemyType(m_time), hpScale, speedScale, damageScale);
    }
}

void SurvivorsDirector::SpawnElite(World& world) {
    GameObject* player = FindPlayer(world);
    const Vec2  center = player ? Vec2(player->Position().x, player->Position().y) : Vec2(0.0f, 0.0f);
    const float angle = Random01() * kTwoPi;
    const Vec2  position(center.x + std::cos(angle) * 12.0f, center.y + std::sin(angle) * 12.0f);

    const float hpScale = 1.0f + m_time / 45.0f + std::pow(m_time / 150.0f, 2.0f);
    const float speedScale = 1.0f + (std::min)(0.8f, m_time / 420.0f);
    const float damageScale = 1.0f + m_time / 100.0f;
    SpawnEnemy(world, position, static_cast<int>(SurvivorsEnemyType::Elite), hpScale, speedScale, damageScale);
    ShowToast("兽人精英出现了！");
    PlaySfx("death", 0.60f, 0.55f);
}

// ================================================================ 导演：生命周期
void SurvivorsDirector::Reset(World& world) {
    // 清掉上一局生成的敌人/箭/宝石/强化道具
    std::vector<GameObject*> toDestroy;
    for (GameObject* root : world.RootObjects()) {
        root->ForEach([&](GameObject* object) {
            if (object->GetComponent<SurvivorsEnemyComponent>() ||
                object->GetComponent<SurvivorsProjectileComponent>() ||
                object->GetComponent<SurvivorsGemComponent>() ||
                object->GetComponent<SurvivorsPickupComponent>()) {
                toDestroy.push_back(object);
            }
        });
    }
    for (GameObject* object : toDestroy) world.DestroyObject(object);
    world.ProcessPendingDestruction();
    g_enemies.clear();
    g_pickups.clear();
    ClearSkillEffects();
    g_killsSinceDrop = 0;
    g_kills = 0;
    g_lastHitSfxTime = -10.0;
    g_lastGemSfxTime = -10.0;

    if (GameObject* player = FindPlayer(world)) {
        player->SetPosition(Vec3(0.0f, 0.0f, 0.0f));
        if (auto* component = player->GetComponent<SurvivorsPlayerComponent>()) component->ResetCombatStats();
    }

    g_rng.seed(0x12345u);
    g_fxRng.seed(0x271828u);
    m_state = State::Playing;
    m_kills = 0;
    m_time = 0.0f;
    m_difficulty = 0.0f;
    m_spawnTimer = 1.0f;
    m_eliteTimer = 45.0f;
    m_showSettings = false;
    m_toastTimer = 0.0f;
    m_toastText.clear();
    LogInfo("[2D] 开始游戏");
    ShowToast("猎杀开始：击杀怪物拾取强化；Esc 暂停");
}

void SurvivorsDirector::Update(World& world, float dt) {
    if (m_state != State::Playing) return;

    m_time += dt;
    m_difficulty = m_time / 60.0f;
    m_kills = g_kills;
    UpdateDamageNumbers(dt);
    UpdateSkillEffects(dt);
    m_toastTimer = (std::max)(0.0f, m_toastTimer - dt);

    if (GameObject* player = FindPlayer(world)) {
        if (auto* component = player->GetComponent<SurvivorsPlayerComponent>()) component->ApplyKillCount(m_kills);
    }

    m_spawnTimer -= dt;
    if (m_spawnTimer <= 0.0f) {
        SpawnWave(world);
        m_spawnTimer = (std::max)(0.55f, 2.4f - m_time * 0.012f);
    }

    m_eliteTimer -= dt;
    if (m_eliteTimer <= 0.0f) {
        SpawnElite(world);
        m_eliteTimer = (std::max)(32.0f, 55.0f - m_time * 0.03f);
    }

    GameObject* player = FindPlayer(world);
    if (!player) return;
    auto* component = player->GetComponent<SurvivorsPlayerComponent>();
    if (!component) return;
    if (component->hp <= 0.0f && component->alive) {
        component->hp = 0.0f;
        component->alive = false;
        m_state = State::GameOver;
        PlaySfx("death", 0.70f, 0.75f);
        LogInfo("[2D] 游戏结束：存活 %.1f 秒，击杀 %d", m_time, m_kills);
    }
}

void SurvivorsDirector::TogglePause() {
    if (m_state == State::Playing) {
        m_state = State::Paused;
        PlaySfx("click", 0.6f);
    } else if (m_state == State::Paused) {
        m_state = State::Playing;
        PlaySfx("click", 0.6f);
    }
}

// ================================================================ 导演：存档
bool SurvivorsDirector::HasSave() const { return FileSystem::Exists(SavePath()); }

void SurvivorsDirector::ShowToast(const std::string& text) {
    m_toastText = text;
    m_toastTimer = 2.4f;
}

void SurvivorsDirector::SaveGame(World& world) {
    GameObject* playerObject = FindPlayer(world);
    auto*       player = playerObject ? playerObject->GetComponent<SurvivorsPlayerComponent>() : nullptr;
    if (!player || !playerObject) {
        ShowToast("保存失败：找不到玩家");
        return;
    }

    JsonValue root = JsonValue::MakeObject();
    root.Set("version", 2);
    root.Set("kind", "survivors-2d");

    JsonValue playerJson = JsonValue::MakeObject();
    const Vec3 position = playerObject->Position();
    playerJson.Set("positionX", position.x);
    playerJson.Set("positionY", position.y);
    playerJson.Set("hp", player->hp);
    playerJson.Set("maxHp", player->maxHp);
    playerJson.Set("level", player->level);
    playerJson.Set("xp", player->xp);
    playerJson.Set("xpNext", player->xpNext);
    playerJson.Set("killCount", player->killCount);
    playerJson.Set("arrowSpread", player->arrowSpread);
    playerJson.Set("arrowSpeedLevel", player->arrowSpeedLevel);
    playerJson.Set("arrowDamageLevel", player->arrowDamageLevel);
    playerJson.Set("arrowRangeLevel", player->arrowRangeLevel);
    playerJson.Set("lightningLevel", player->lightningLevel);
    playerJson.Set("bladeLevel", player->bladeLevel);
    playerJson.Set("frostLevel", player->frostLevel);
    playerJson.Set("poisonLevel", player->poisonLevel);
    playerJson.Set("fireballLevel", player->fireballLevel);
    root.Set("player", playerJson);

    JsonValue directorJson = JsonValue::MakeObject();
    directorJson.Set("time", m_time);
    directorJson.Set("kills", m_kills);
    directorJson.Set("difficulty", m_difficulty);
    directorJson.Set("spawnTimer", m_spawnTimer);
    directorJson.Set("eliteTimer", m_eliteTimer);
    root.Set("director", directorJson);

    FileSystem::EnsureDirectory(FileSystem::Join(FileSystem::WritableAssetDir(), "saves"));
    const std::string path = SavePath();
    if (!FileSystem::WriteTextFile(path, root.Dump(2))) {
        ShowToast("保存失败：无法写入存档");
        LogWarn("[2D] 存档写入失败: %s", path.c_str());
        return;
    }
    ShowToast("游戏已保存");
    PlaySfx("levelup", 0.45f, 1.15f);
    LogInfo("[2D] 存档已保存: %s", path.c_str());
}

bool SurvivorsDirector::LoadGame(World& world) {
    const std::string path = SavePath();
    std::string       text;
    if (!FileSystem::ReadTextFile(path, text)) {
        ShowToast("没有可继续的存档");
        return false;
    }
    std::string error;
    JsonValue   root = JsonValue::Parse(text, &error);
    if (!error.empty() || !root.IsObject()) {
        ShowToast("存档损坏，无法读取");
        LogWarn("[2D] 存档解析失败: %s", error.c_str());
        return false;
    }
    const JsonValue& playerJson = root["player"];
    if (!playerJson.IsObject()) {
        ShowToast("存档损坏：缺少玩家数据");
        return false;
    }

    Reset(world);
    GameObject* playerObject = FindPlayer(world);
    auto*       player = playerObject ? playerObject->GetComponent<SurvivorsPlayerComponent>() : nullptr;
    if (!player || !playerObject) return false;

    player->maxHp = playerJson["maxHp"].AsFloat(player->maxHp);
    player->hp = playerJson["hp"].AsFloat(player->hp);
    player->level = playerJson["level"].AsInt(player->level);
    player->xp = playerJson["xp"].AsFloat(player->xp);
    player->xpNext = playerJson["xpNext"].AsFloat(player->xpNext);
    player->killCount = playerJson["killCount"].AsInt(player->killCount);
    player->arrowSpread = playerJson["arrowSpread"].AsBool(player->arrowSpread);
    player->arrowSpeedLevel = playerJson["arrowSpeedLevel"].AsInt(player->arrowSpeedLevel);
    player->arrowDamageLevel = playerJson["arrowDamageLevel"].AsInt(player->arrowDamageLevel);
    player->arrowRangeLevel = playerJson["arrowRangeLevel"].AsInt(player->arrowRangeLevel);
    player->lightningLevel = playerJson["lightningLevel"].AsInt(player->lightningLevel);
    player->bladeLevel = playerJson["bladeLevel"].AsInt(player->bladeLevel);
    player->frostLevel = playerJson["frostLevel"].AsInt(player->frostLevel);
    player->poisonLevel = playerJson["poisonLevel"].AsInt(player->poisonLevel);
    player->fireballLevel = playerJson["fireballLevel"].AsInt(player->fireballLevel);
    player->RefreshDerivedStats();
    player->hp = Clamp(player->hp, 1.0f, player->maxHp);
    player->alive = true;

    Vec2 position(playerJson["positionX"].AsFloat(0.0f), playerJson["positionY"].AsFloat(0.0f));
    position.x = Clamp(position.x, -player->arenaHalf.x, player->arenaHalf.x);
    position.y = Clamp(position.y, -player->arenaHalf.y, player->arenaHalf.y);
    if (CircleHitsObstacle(position, 0.6f)) position = Vec2(0.0f, 0.0f);
    playerObject->SetPosition(Vec3(position.x, position.y, 0.0f));

    const JsonValue& directorJson = root["director"];
    m_time = directorJson["time"].AsFloat(0.0f);
    m_kills = directorJson["kills"].AsInt(0);
    m_difficulty = directorJson["difficulty"].AsFloat(m_time / 60.0f);
    m_spawnTimer = (std::max)(0.35f, directorJson["spawnTimer"].AsFloat(0.8f));
    m_eliteTimer = (std::max)(5.0f, directorJson["eliteTimer"].AsFloat(35.0f));
    g_kills = m_kills;
    player->ApplyKillCount(m_kills);
    m_state = State::Playing;
    ShowToast("已载入存档");
    PlaySfx("levelup", 0.50f, 0.95f);
    LogInfo("[2D] 存档已读取: %s（%.1f 秒 / %d 击杀）", path.c_str(), m_time, m_kills);
    return true;
}

// ================================================================ 导演：设置/音乐
void SurvivorsDirector::ApplyAudioSettings() { ApplySurvivorsAudioSettings(); }

void SurvivorsDirector::EnsureMusic() {
    AudioSystem& audio = AudioSystem::Get();
    if (!g_settings.musicEnabled) {
        audio.StopMusic();
        return;
    }
    if (!audio.Ready() || audio.IsMusicPlaying()) return;
    audio.PlayMusic("bgm_survivors", g_settings.musicVolume);
}

void SurvivorsDirector::DrawSettingsPanel() {
    if (!m_showSettings) return;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 330), ImGuiCond_Always);
    ImGui::Begin("游戏设置", &m_showSettings, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
    ImGui::TextWrapped("设置会立即生效，并保存到 assets/survivors_settings.json。");
    ImGui::Separator();

    bool musicEnabled = AudioSystem::Get().MusicEnabled();
    if (ImGui::Checkbox("背景音乐", &musicEnabled)) {
        g_settings.musicEnabled = musicEnabled;
        ApplySurvivorsAudioSettings();
        SaveSurvivorsSettings();
        if (!musicEnabled) {
            AudioSystem::Get().StopMusic();
        } else {
            EnsureMusic();
        }
    }
    bool sfxEnabled = AudioSystem::Get().SfxEnabled();
    if (ImGui::Checkbox("游戏音效", &sfxEnabled)) {
        g_settings.sfxEnabled = sfxEnabled;
        ApplySurvivorsAudioSettings();
        SaveSurvivorsSettings();
        if (sfxEnabled) PlaySfx("click", 0.6f);
    }

    ImGui::Separator();
    float musicVolume = g_settings.musicVolume;
    ImGui::Text("音乐音量");
    ImGui::SameLine(110.0f);
    ImGui::SetNextItemWidth(250.0f);
    if (ImGui::SliderFloat("##musicVolume", &musicVolume, 0.0f, 1.0f, "%.2f")) {
        g_settings.musicVolume = musicVolume;
        ApplySurvivorsAudioSettings();
        SaveSurvivorsSettings();
    }
    float sfxVolume = g_settings.sfxVolume;
    ImGui::Text("音效音量");
    ImGui::SameLine(110.0f);
    ImGui::SetNextItemWidth(250.0f);
    if (ImGui::SliderFloat("##sfxVolume", &sfxVolume, 0.0f, 1.0f, "%.2f")) {
        g_settings.sfxVolume = sfxVolume;
        ApplySurvivorsAudioSettings();
        SaveSurvivorsSettings();
    }
    float masterVolume = g_settings.masterVolume;
    ImGui::Text("主音量");
    ImGui::SameLine(110.0f);
    ImGui::SetNextItemWidth(250.0f);
    if (ImGui::SliderFloat("##masterVolume", &masterVolume, 0.0f, 1.0f, "%.2f")) {
        g_settings.masterVolume = masterVolume;
        ApplySurvivorsAudioSettings();
        SaveSurvivorsSettings();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Esc 暂停 / 继续");
    ImGui::End();
}

// ================================================================ 导演：UI
void SurvivorsDirector::DrawUI(World& world, float viewportX, float viewportY, float viewportWidth,
                               float viewportHeight) {
    EnsureMusic();

    const ImVec2 center(viewportX + viewportWidth * 0.5f, viewportY + viewportHeight * 0.5f);
    auto centeredWindow = [&](const char* name, const ImVec2& size) {
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        return ImGui::Begin(name, nullptr,
                            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);
    };

    if (m_state == State::Menu) {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.04f, 0.06f, 0.93f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);
        if (centeredWindow("##SurvivorsMenu", ImVec2(850, 520))) {
            ImGui::Dummy(ImVec2(0, 8));
            ImGui::SetWindowFontScale(1.9f);
            ImGui::SetCursorPosX(52);
            ImGui::TextColored(ImVec4(0.96f, 0.80f, 0.36f, 1.0f), "二毛幸存者 · 2D");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::SetCursorPosX(54);
            ImGui::TextDisabled("自动战斗 · 击杀掉落 · 技能强化 · 生存挑战");
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::TextWrapped("WASD 移动。箭矢会自动攻击最近的敌人；没有目标时会朝面朝方向射击。");
            ImGui::TextWrapped("击杀怪物随机掉落强化道具：八向散射、箭矢攻速、箭矢攻击力、血瓶、箭矢范围，");
            ImGui::TextWrapped("以及雷霆、回旋刃、霜环、毒雾、火球的强化。走上去即可拾取。");
            ImGui::TextWrapped("怪物会越来越强；石柱、木箱、书架和围栏会挡住你和怪物。");
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.46f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.27f, 0.56f, 0.34f, 1.0f));
            if (ImGui::Button("开始游戏", ImVec2(230, 48))) {
                PlaySfx("click");
                Reset(world);
            }
            ImGui::PopStyleColor(2);
            ImGui::SameLine(0, 16);
            ImGui::BeginDisabled(!HasSave());
            if (ImGui::Button("继续游戏", ImVec2(210, 48))) {
                PlaySfx("click");
                LoadGame(world);
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0, 16);
            if (ImGui::Button("设置", ImVec2(130, 48))) {
                PlaySfx("click");
                m_showSettings = !m_showSettings;
            }
            ImGui::Dummy(ImVec2(0, 8));
            if (ImGui::Button("退出程序", ImVec2(180, 40))) {
                PlaySfx("click");
                m_wantsQuit = true;
            }
            ImGui::SameLine(0, 16);
            if (HasSave()) {
                ImGui::TextDisabled("检测到存档：可以继续上次的冒险。");
            } else {
                ImGui::TextDisabled("暂无存档；暂停或失败时可以保存本局。");
            }
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.96f, 0.80f, 0.36f, 1.0f), "局内目标");
            ImGui::TextWrapped("在越来越强的怪潮里活下来。击杀会掉落强化，捡得越多，箭矢和五个技能就越强。");
            ImGui::TextDisabled("技能：箭矢（掉落强化） / 雷霆 / 回旋刃 / 霜环 / 毒雾 / 火球");
            ImGui::TextDisabled("Esc 暂停与继续；暂停菜单里可以保存，主菜单可以继续上次存档。");
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    } else if (m_state == State::Paused) {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.04f, 0.06f, 0.93f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        if (centeredWindow("##SurvivorsPause", ImVec2(460, 380))) {
            ImGui::SetWindowFontScale(1.4f);
            ImGui::TextColored(ImVec4(0.96f, 0.80f, 0.36f, 1.0f), "已暂停");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::Separator();
            if (ImGui::Button("继续游戏", ImVec2(-1, 38))) m_state = State::Playing;
            if (ImGui::Button("保存游戏", ImVec2(-1, 38))) SaveGame(world);
            if (ImGui::Button("设置", ImVec2(-1, 38))) m_showSettings = !m_showSettings;
            if (ImGui::Button("重新开始", ImVec2(-1, 38))) Reset(world);
            if (ImGui::Button("保存并返回主菜单", ImVec2(-1, 38))) {
                SaveGame(world);
                m_state = State::Menu;
            }
            if (ImGui::Button("退出程序", ImVec2(-1, 38))) m_wantsQuit = true;
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    } else if (m_state == State::GameOver) {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.02f, 0.03f, 0.94f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        if (centeredWindow("##SurvivorsOver", ImVec2(540, 430))) {
            ImGui::SetWindowFontScale(1.5f);
            ImGui::TextColored(ImVec4(0.96f, 0.38f, 0.34f, 1.0f), "你被怪物淹没了");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::Separator();
            ImGui::Text("存活时间：%.1f 秒", m_time);
            ImGui::Text("击杀：%d", m_kills);
            if (GameObject* player = FindPlayer(world)) {
                if (auto* component = player->GetComponent<SurvivorsPlayerComponent>()) {
                    ImGui::Text("等级：%d    箭矢伤害：%.0f", component->level, component->ArrowDamage());
                    ImGui::TextDisabled("雷霆 %d · 回旋刃 %d · 霜环 %d · 毒雾 %d · 火球 %d", component->lightningLevel,
                                        component->bladeLevel, component->frostLevel, component->poisonLevel,
                                        component->fireballLevel);
                }
            }
            ImGui::Dummy(ImVec2(0, 8));
            if (ImGui::Button("保存本局", ImVec2(160, 38))) SaveGame(world);
            ImGui::SameLine(0, 12);
            if (ImGui::Button("重新开始", ImVec2(160, 38))) Reset(world);
            ImGui::SameLine(0, 12);
            if (ImGui::Button("返回主菜单", ImVec2(160, 38))) m_state = State::Menu;
            if (ImGui::Button("退出程序", ImVec2(-1, 36))) m_wantsQuit = true;
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    } else {
        // ------------------------------------------------ HUD
        EnsureMusic();
        GameObject* player = FindPlayer(world);
        auto*       component = player ? player->GetComponent<SurvivorsPlayerComponent>() : nullptr;
        const float hpRatio = component && component->maxHp > 0.0f ? Clamp01(component->hp / component->maxHp) : 0.0f;
        const float xpRatio = component && component->xpNext > 0.0f ? Clamp01(component->xp / component->xpNext) : 0.0f;

        ImGui::SetNextWindowPos(ImVec2(viewportX + 14.0f, viewportY + 34.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(390, 210), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.04f, 0.06f, 0.90f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
        ImGui::Begin("##SurvivorsHud", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoScrollbar);
        ImGui::Text("生命 %.0f / %.0f", component ? component->hp : 0.0f, component ? component->maxHp : 0.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.85f, 0.25f, 0.28f, 1.0f));
        ImGui::ProgressBar(hpRatio, ImVec2(310, 12));
        ImGui::PopStyleColor();
        ImGui::Text("经验 %.0f / %.0f", component ? component->xp : 0.0f, component ? component->xpNext : 0.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.35f, 0.75f, 0.95f, 1.0f));
        ImGui::ProgressBar(xpRatio, ImVec2(310, 10));
        ImGui::PopStyleColor();
        ImGui::Text("等级 %d   击杀 %d   时间 %.0fs", component ? component->level : 1, m_kills, m_time);
        if (component) {
            ImGui::Text("箭矢 %s   伤害 %.0f   攻速 +%.0f%%   范围 %.1f", component->arrowSpread ? "八向" : "单发",
                        component->ArrowDamage(), component->ArrowSpeedBonus() * 100.0f, component->ArrowRange());
            ImGui::TextDisabled("掉落：散射 %s · 攻速 %d · 攻击 %d · 范围 %d", component->arrowSpread ? "已解锁" : "未解锁",
                                component->arrowSpeedLevel, component->arrowDamageLevel, component->arrowRangeLevel);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        // 右上角技能面板
        const float panelWidth = 410.0f;
        const float panelHeight = 312.0f;
        ImGui::SetNextWindowPos(ImVec2(viewportX + viewportWidth - panelWidth - 14.0f, viewportY + 34.0f),
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.04f, 0.06f, 0.90f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
        ImGui::Begin("##SurvivorsSkills", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoScrollbar);
        ImGui::TextColored(ImVec4(0.96f, 0.80f, 0.36f, 1.0f), "技能");
        ImGui::Separator();
        if (component) {
            auto skillLine = [&](const ImVec4& color, const char* name, int level, const char* detail) {
                ImGui::TextColored(color, "%s", name);
                ImGui::SameLine(96);
                ImGui::Text("Lv.%d", level);
                ImGui::SetWindowFontScale(0.90f);
                ImGui::SetCursorPosX(18.0f);
                ImGui::TextDisabled("%s", detail);
                ImGui::SetWindowFontScale(1.0f);
            };
            char detail[192];
            std::snprintf(detail, sizeof(detail), "%s · 伤害 %.0f · 攻速 +%.0f%% · 范围 %.1f",
                          component->arrowSpread ? "八向 0.65x" : "单发锁定", component->ArrowDamage(),
                          component->ArrowSpeedBonus() * 100.0f, component->ArrowRange());
            skillLine(ImVec4(0.96f, 0.82f, 0.40f, 1.0f), "箭矢", 1 + component->arrowSpeedLevel +
                                                                     component->arrowDamageLevel +
                                                                     component->arrowRangeLevel + (component->arrowSpread ? 1 : 0),
                      detail);
            std::snprintf(detail, sizeof(detail), "%d 目标 · 连锁 %d · 伤害 %.0f · CD %.1fs",
                          component->LightningTargets(), component->LightningChains(), component->LightningDamage(),
                          component->LightningCooldown());
            skillLine(ImVec4(1.00f, 0.95f, 0.40f, 1.0f), "雷霆", component->lightningLevel, detail);
            std::snprintf(detail, sizeof(detail), "%d 刃 · 伤害 %.0f · 半径 %.1f", component->BladeCount(),
                          component->BladeDamage(), component->BladeRadius());
            skillLine(ImVec4(0.82f, 0.88f, 1.00f, 1.0f), "回旋刃", component->bladeLevel, detail);
            std::snprintf(detail, sizeof(detail), "半径 %.1f · 伤害 %.0f · 减速 %.0f%% · CD %.1fs",
                          component->FrostRadius(), component->FrostDamage(), component->FrostSlow() * 100.0f,
                          component->FrostCooldown());
            skillLine(ImVec4(0.55f, 0.85f, 1.00f, 1.0f), "霜环", component->frostLevel, detail);
            std::snprintf(detail, sizeof(detail), "半径 %.1f · DPS %.0f", component->PoisonRadius(),
                          component->PoisonDps());
            skillLine(ImVec4(0.55f, 1.00f, 0.50f, 1.0f), "毒雾", component->poisonLevel, detail);
            std::snprintf(detail, sizeof(detail), "%d 发 · 伤害 %.0f · 爆炸 %.1f · CD %.1fs", component->FireballCount(),
                          component->FireballDamage(), component->FireballExplosionRadius(),
                          component->FireballCooldown());
            skillLine(ImVec4(1.00f, 0.60f, 0.25f, 1.0f), "火球", component->fireballLevel, detail);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        ImGui::GetForegroundDrawList()->AddText(ImVec2(viewportX + 16, viewportY + viewportHeight - 30),
                                                IM_COL32(220, 225, 235, 180),
                                                "WASD 移动 · 自动战斗 · Esc 暂停");
    }

    if (m_showSettings) DrawSettingsPanel();

    m_toastTimer = (std::max)(0.0f, m_toastTimer - ImGui::GetIO().DeltaTime);
    std::string toast = m_toastTimer > 0.0f ? m_toastText : std::string();
    if (toast.empty()) {
        if (GameObject* player = FindPlayer(world)) {
            if (auto* component = player->GetComponent<SurvivorsPlayerComponent>()) {
                if (component->pickupToastTimer > 0.0f) toast = component->pickupToast;
            }
        }
    }
    if (!toast.empty()) {
        const float timer = m_toastTimer > 0.0f ? m_toastTimer : 2.0f;
        DrawToast(toast, timer, viewportX, viewportY, viewportWidth, viewportHeight);
    }
}

// ================================================================ 导演：战斗反馈
void SurvivorsDirector::DrawCombatFeedback(World& world, const Camera2D& camera, float viewportX, float viewportY,
                                           float viewportWidth, float viewportHeight) {
    if (m_state != State::Playing) return;
    if (viewportWidth < 8.0f || viewportHeight < 8.0f) return;

    const float zoom = camera.zoom > 1e-4f ? camera.zoom : 1e-4f;
    const float halfH = camera.halfHeight / zoom;
    const float halfW = halfH * (viewportWidth / viewportHeight);
    const float pixelsPerUnit = viewportWidth / (2.0f * halfW);

    auto project = [&](const Vec2& position) {
        return ImVec2(viewportX + (position.x - (camera.position.x - halfW)) / (2.0f * halfW) * viewportWidth,
                      viewportY + ((camera.position.y + halfH) - position.y) / (2.0f * halfH) * viewportHeight);
    };

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect(ImVec2(viewportX, viewportY),
                       ImVec2(viewportX + viewportWidth, viewportY + viewportHeight), true);

    GameObject* player = FindPlayer(world);
    auto*       playerComponent = player ? player->GetComponent<SurvivorsPlayerComponent>() : nullptr;
    if (player && playerComponent) {
        const Vec2 center(player->Position().x, player->Position().y);
        const ImVec2 centerScreen = project(center);

        // 毒雾光环
        const float auraRadius = playerComponent->PoisonRadius() * pixelsPerUnit;
        draw->AddCircleFilled(centerScreen, auraRadius, IM_COL32(55, 190, 80, 24), 56);
        draw->AddCircle(centerScreen, auraRadius, IM_COL32(95, 235, 115, 78), 56, 1.5f);

        // 回旋刃
        const int bladeCount = playerComponent->BladeCount();
        for (int i = 0; i < bladeCount; ++i) {
            const float angle = playerComponent->BladeAngle() + static_cast<float>(i) * kTwoPi /
                                                                      static_cast<float>(bladeCount);
            const Vec2 tip = center + Vec2(std::cos(angle), std::sin(angle)) * playerComponent->BladeRadius();
            const Vec2 tangent(-std::sin(angle), std::cos(angle));
            const ImVec2 p0 = project(tip - tangent * 0.32f);
            const ImVec2 p1 = project(tip + tangent * 0.32f);
            draw->AddLine(p0, p1, IM_COL32(225, 235, 255, 235), 3.0f);
            draw->AddCircleFilled(project(tip), 3.0f, IM_COL32(255, 255, 255, 200));
        }
    }

    // 火把暖光
    for (const Vec2& torch : g_torches) {
        const ImVec2 screen = project(torch);
        const float flicker = 0.82f + 0.18f * std::sin(m_time * 7.0f + torch.x * 1.7f);
        draw->AddCircleFilled(screen, 34.0f * flicker, IM_COL32(255, 150, 60, 20), 24);
        draw->AddCircleFilled(screen, 15.0f * flicker, IM_COL32(255, 190, 90, 40), 20);
        draw->AddCircleFilled(screen, 5.0f, IM_COL32(255, 235, 170, 220), 16);
    }

    // 掉落物标签
    for (GameObject* pickup : g_pickups) {
        if (!pickup || !pickup->Active()) continue;
        auto* component = pickup->GetComponent<SurvivorsPickupComponent>();
        if (!component) continue;
        const PickupInfo info = GetPickupInfo(component->type);
        const Vec2       position(pickup->Position().x, pickup->Position().y + 0.95f);
        const ImVec2     screen = project(position);
        const ImVec2     textSize = ImGui::GetFont()->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, info.label);
        const ImVec2     min(screen.x - textSize.x * 0.5f - 5.0f, screen.y - textSize.y - 3.0f);
        const ImVec2     max(screen.x + textSize.x * 0.5f + 5.0f, screen.y + 1.0f);
        draw->AddRectFilled(min, max, IM_COL32(8, 10, 15, 205), 4.0f);
        draw->AddRect(min, max, ToImColor(info.color, 0.85f), 4.0f);
        draw->AddText(ImGui::GetFont(), 13.0f, ImVec2(screen.x - textSize.x * 0.5f, screen.y - textSize.y),
                      ToImColor(info.color, 1.0f), info.label);
        draw->AddCircleFilled(project(Vec2(pickup->Position().x, pickup->Position().y)), 7.0f,
                              ToImColor(info.color, 0.28f));
    }

    // 闪电
    for (const LightningFx& fx : g_lightningFx) {
        if (fx.points.size() < 2) continue;
        const float t = Clamp01(fx.age / fx.life);
        std::vector<ImVec2> points;
        points.reserve(fx.points.size());
        for (const Vec2& point : fx.points) points.push_back(project(point));
        draw->AddPolyline(points.data(), static_cast<int>(points.size()), IM_COL32(255, 240, 140, static_cast<int>(70 * (1 - t))),
                          ImDrawFlags_None, fx.width + 4.0f);
        draw->AddPolyline(points.data(), static_cast<int>(points.size()),
                          IM_COL32(255, 250, 210, static_cast<int>(255 * (1 - t))), ImDrawFlags_None, fx.width);
    }

    // 霜环
    for (const FrostRingFx& fx : g_frostFx) {
        const float t = Clamp01(fx.age / fx.life);
        const ImVec2 centerScreen = project(fx.center);
        const float  radius = fx.radius * pixelsPerUnit;
        draw->AddCircleFilled(centerScreen, radius, IM_COL32(150, 220, 255, static_cast<int>(26 * (1 - t))), 56);
        draw->AddCircle(centerScreen, radius, IM_COL32(180, 235, 255, static_cast<int>(210 * (1 - t))), 56, 2.0f);
        draw->AddCircle(centerScreen, radius * 0.82f, IM_COL32(220, 245, 255, static_cast<int>(90 * (1 - t))), 56,
                        1.0f);
    }

    // 爆炸
    for (const ExplosionFx& fx : g_explosionFx) {
        const float t = Clamp01(fx.age / fx.life);
        const ImVec2 centerScreen = project(fx.center);
        const float  radius = fx.radius * pixelsPerUnit;
        draw->AddCircleFilled(centerScreen, radius, ToImColor(fx.color, 0.28f * (1 - t)), 40);
        draw->AddCircle(centerScreen, radius, ToImColor(fx.color, 0.95f * (1 - t)), 40, 2.0f);
    }

    // 火球
    for (const FireballFx& fireball : g_fireballs) {
        const ImVec2 screen = project(fireball.position);
        const ImVec2 trail = project(fireball.position - fireball.velocity * 0.08f);
        draw->AddLine(trail, screen, IM_COL32(255, 160, 60, 150), 7.0f);
        draw->AddCircleFilled(screen, 11.0f, IM_COL32(255, 120, 40, 65), 24);
        draw->AddCircleFilled(screen, 5.0f, IM_COL32(255, 225, 150, 245), 24);
    }

    // 头顶血条：锚在世界坐标、跟随相机投影，看起来像"立"在角色头上的 3D 血条
    for (GameObject* enemy : g_enemies) {
        if (!enemy || !enemy->Active()) continue;
        auto* component = enemy->GetComponent<SurvivorsEnemyComponent>();
        if (!component || component->hp <= 0.0f) continue;

        const ImVec2 screen = project(Vec2(enemy->Position().x, enemy->Position().y + 1.05f));
        constexpr float kBarWidth = 58.0f;
        constexpr float kBarHeight = 7.0f;
        const float ratio = Clamp01(component->hp / (component->maxHp > 0.0f ? component->maxHp : 1.0f));
        const float left = screen.x - kBarWidth * 0.5f;
        const float top = screen.y - kBarHeight;

        draw->AddRectFilled(ImVec2(left - 1.0f, top - 1.0f), ImVec2(left + kBarWidth + 1.0f, top + kBarHeight + 1.0f),
                            IM_COL32(6, 7, 10, 210), 3.0f);
        draw->AddRectFilled(ImVec2(left, top), ImVec2(left + kBarWidth, top + kBarHeight), IM_COL32(28, 30, 36, 235),
                            2.0f);
        if (ratio > 0.0f) {
            const ImU32 fill = component->hitFlash > 0.0f ? IM_COL32(255, 255, 255, 255)
                                                           : (component->poisonTimer > 0.0f
                                                                  ? IM_COL32(120, 230, 105, 245)
                                                                  : IM_COL32(226, 64, 74, 245));
            draw->AddRectFilled(ImVec2(left, top), ImVec2(left + kBarWidth * ratio, top + kBarHeight), fill, 2.0f);
            draw->AddRectFilled(ImVec2(left + 1.0f, top + 1.0f),
                                ImVec2(left + kBarWidth * ratio - 1.0f, top + 3.0f), IM_COL32(255, 255, 255, 60),
                                1.0f);
        }
    }

    // 2D 伤害数字：世界坐标起跳，向上飘并淡出
    for (const DamageNumber2D& number : g_damageNumbers) {
        const float t = Clamp01(number.age / number.life);
        const ImVec2 screen = project(number.position);
        char text[32];
        std::snprintf(text, sizeof(text), "-%.0f", number.value);
        const float fontSize = Lerp(23.0f, 15.0f, t);
        const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
        const ImVec2 pos(screen.x - textSize.x * 0.5f, screen.y - textSize.y * 0.5f);
        const int alpha = static_cast<int>(255.0f * (1.0f - t));
        const ImU32 outline = IM_COL32(20, 12, 6, alpha);
        draw->AddText(ImGui::GetFont(), fontSize, ImVec2(pos.x - 1.5f, pos.y), outline, text);
        draw->AddText(ImGui::GetFont(), fontSize, ImVec2(pos.x + 1.5f, pos.y), outline, text);
        draw->AddText(ImGui::GetFont(), fontSize, ImVec2(pos.x, pos.y - 1.5f), outline, text);
        draw->AddText(ImGui::GetFont(), fontSize, ImVec2(pos.x, pos.y + 1.5f), outline, text);
        draw->AddText(ImGui::GetFont(), fontSize, pos, IM_COL32(255, 214, 96, alpha), text);
    }

    draw->PopClipRect();
}

// ================================================================ 关卡
void BuildSurvivorsLevel(World& world, AssetManager& assets) {
    world.Clear();
    g_enemies.clear();
    g_pickups.clear();
    g_obstacles.clear();
    g_torches.clear();
    ClearSkillEffects();
    g_rng.seed(0x12345u);
    g_fxRng.seed(0x271828u);
    g_kills = 0;
    g_killsSinceDrop = 0;
    LoadSurvivorsSettings();

    // 程序化宝石贴图（12x12 菱形）
    {
        const int size = 12;
        std::vector<u8> pixels(size * size * 4, 0);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const int cx = x - size / 2;
                const int cy = y - size / 2;
                const int d = std::abs(cx) + std::abs(cy);
                u8* p = &pixels[(y * size + x) * 4];
                if (d <= 4) {
                    const bool highlight = (cx + cy < -1);
                    p[0] = highlight ? 200 : 90;
                    p[1] = highlight ? 245 : 210;
                    p[2] = highlight ? 255 : 255;
                    p[3] = 255;
                }
            }
        }
        assets.CreateTextureRGBA("gem_proc", size, size, pixels.data());
    }

    // 程序化强化道具贴图（16x16 白色菱形，精灵颜色按类型染色）
    {
        const int size = 16;
        std::vector<u8> pixels(size * size * 4, 0);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const int cx = x - size / 2;
                const int cy = y - size / 2;
                const int d = std::abs(cx) + std::abs(cy);
                u8* p = &pixels[(y * size + x) * 4];
                if (d <= 6) {
                    p[0] = 255;
                    p[1] = 255;
                    p[2] = 255;
                    p[3] = d <= 3 ? 255 : 165;
                }
            }
        }
        assets.CreateTextureRGBA("pickup_proc", size, size, pixels.data());
    }

    // 程序化地面装饰：小石子和草
    {
        const int size = 12;
        std::vector<u8> pebbles(size * size * 4, 0);
        std::vector<u8> grass(size * size * 4, 0);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const int cx = x - size / 2;
                const int cy = y - size / 2;
                u8* p = &pebbles[(y * size + x) * 4];
                if (cx * cx + cy * cy * 2 <= 18) {
                    p[0] = 130;
                    p[1] = 132;
                    p[2] = 138;
                    p[3] = 220;
                }
                u8* g = &grass[(y * size + x) * 4];
                if ((x == 3 || x == 6 || x == 9) && y >= 3 && y <= 10) {
                    g[0] = 80;
                    g[1] = 165;
                    g[2] = 80;
                    g[3] = 235;
                } else if (y >= 7 && y <= 10 && (x == 4 || x == 7 || x == 8)) {
                    g[0] = 55;
                    g[1] = 125;
                    g[2] = 55;
                    g[3] = 210;
                }
            }
        }
        assets.CreateTextureRGBA("pebble_proc", size, size, pebbles.data());
        assets.CreateTextureRGBA("grass_proc", size, size, grass.data());
    }

    // 地面：16x16 程序化石砖，64x64 重复成大贴图，1 个精灵铺满 90x54
    {
        const int size = 16;
        std::vector<u8> pixels(size * size * 4, 0);
        unsigned seed = 1337u;
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                seed = seed * 1664525u + 1013904223u;
                const int noise = static_cast<int>((seed >> 16) & 0x7) - 3;
                int shade = 113 + noise;
                const bool seam = (x == 0) || (y == 0) || (x == 8 && y == 8);
                if (seam) shade = 88;
                if (x == 8 && y < 8) shade -= 4;
                if (y == 8 && x > 8) shade -= 3;
                u8* p = &pixels[(y * size + x) * 4];
                if ((seed >> 21) % 29 == 0) {
                    // 少量青苔
                    p[0] = 64;
                    p[1] = 92;
                    p[2] = 62;
                } else if ((seed >> 18) % 31 == 0) {
                    // 裂纹
                    p[0] = 70;
                    p[1] = 68;
                    p[2] = 74;
                } else {
                    p[0] = static_cast<u8>(Clamp(shade + 5, 0, 255));
                    p[1] = static_cast<u8>(Clamp(shade + 3, 0, 255));
                    p[2] = static_cast<u8>(Clamp(shade + 6, 0, 255));
                }
                p[3] = 255;
            }
        }
        assets.CreateTextureRGBA("floor_proc", size, size, pixels.data());

        const int repeat = 64;
        const int bigSize = size * repeat;
        std::vector<u8> big(static_cast<size_t>(bigSize) * bigSize * 4, 0);
        for (int y = 0; y < bigSize; ++y) {
            for (int x = 0; x < bigSize; ++x) {
                const int sx = x % size;
                const int sy = y % size;
                std::memcpy(&big[(static_cast<size_t>(y) * bigSize + x) * 4],
                            &pixels[(static_cast<size_t>(sy) * size + sx) * 4], 4);
            }
        }
        assets.CreateTextureRGBA("floor_big", bigSize, bigSize, big.data());
        SpawnSprite(world, "地面", "floor_big", Vec2(0.0f, 0.0f), Vec2(90.0f, 54.0f), -10);
    }

    // 地图装饰：草和石子（无碰撞）
    for (int i = 0; i < 80; ++i) {
        const Vec2 position(RandomRange(-42.0f, 42.0f), RandomRange(-24.0f, 24.0f));
        if (Length(position) < 2.5f) continue;
        if (CircleHitsObstacle(position, 0.8f)) continue;
        SpawnSprite(world, "草丛", "grass_proc", position, Vec2(0.85f, 0.85f), -9);
    }
    for (int i = 0; i < 60; ++i) {
        const Vec2 position(RandomRange(-42.0f, 42.0f), RandomRange(-24.0f, 24.0f));
        if (Length(position) < 2.5f) continue;
        if (CircleHitsObstacle(position, 0.6f)) continue;
        SpawnSprite(world, "石子", "pebble_proc", position, Vec2(0.55f, 0.55f), -9);
    }

    // 中心区域再补一些装饰，避免开局画面光秃秃
    const Vec2 centralDecor[] = {{-6.0f, 2.0f}, {5.0f, -3.0f}, {-4.0f, -5.0f}, {7.0f, 4.0f},
                                 {-9.0f, 1.0f},  {9.0f, -1.0f}, {3.0f, 7.0f},   {-3.0f, -7.0f},
                                 {-7.5f, 5.0f},  {7.5f, -5.0f}};
    for (size_t i = 0; i < sizeof(centralDecor) / sizeof(centralDecor[0]); ++i) {
        const Vec2 position = centralDecor[i];
        if (i % 2 == 0) {
            SpawnSprite(world, "草丛", "grass_proc", position, Vec2(1.0f, 1.0f), -9);
        } else {
            SpawnSprite(world, "石子", "pebble_proc", position, Vec2(0.65f, 0.65f), -9);
        }
    }

    // 火把：纯装饰，但给暗色地图一点暖色光源
    const Vec2 torchPositions[] = {{-10.0f, 6.0f},  {10.0f, 6.0f},  {-10.0f, -6.0f}, {10.0f, -6.0f},
                                   {-20.0f, 12.0f}, {20.0f, 12.0f}, {-20.0f, -12.0f}, {20.0f, -12.0f},
                                   {-30.0f, 0.0f},  {30.0f, 0.0f}};
    for (const Vec2& position : torchPositions) {
        GameObject* torch = SpawnSprite(world, "火把", "sprites/kenney_tilemap.png", position, Vec2(1.1f, 1.1f), -8);
        if (auto* sprite = torch->GetComponent<SpriteRendererComponent>()) ApplyTileSprite(sprite, 10, 5);
        g_torches.push_back(position);
    }

    // 地图障碍：石柱、木箱、木桶、书架、围栏、桌子、大锅
    struct ObstacleSpec {
        float x, y, hx, hy;
        int   row, col;
    };
    const ObstacleSpec obstacles[] = {
        {-18.0f, 8.0f, 1.0f, 0.85f, 6, 2},   {-16.6f, 8.6f, 0.8f, 0.7f, 5, 6},
        {-6.0f, 18.0f, 1.1f, 0.9f, 5, 3},    {20.0f, 16.0f, 1.0f, 0.9f, 6, 6},
        {26.0f, -6.0f, 1.1f, 0.9f, 5, 3},    {10.0f, -18.0f, 1.0f, 0.85f, 6, 3},
        {-24.0f, -12.0f, 1.1f, 0.95f, 5, 3}, {-30.0f, 4.0f, 0.85f, 0.8f, 5, 6},
        {32.0f, 10.0f, 1.0f, 0.9f, 5, 3},    {-12.0f, -18.0f, 0.95f, 0.9f, 6, 6},
        {6.0f, 10.0f, 0.85f, 0.75f, 5, 3},   {-8.0f, 4.0f, 0.75f, 0.7f, 5, 6},
        {14.0f, 4.0f, 0.75f, 0.7f, 5, 6},    {0.0f, 22.0f, 1.0f, 0.9f, 6, 2},
        {-36.0f, 16.0f, 1.2f, 1.0f, 6, 2},   {36.0f, -18.0f, 1.2f, 1.0f, 5, 3},
        {-38.0f, -8.0f, 1.0f, 1.0f, 5, 3},   {38.0f, 4.0f, 1.0f, 1.0f, 5, 6},
        {24.0f, 22.0f, 1.0f, 1.0f, 6, 2},    {-28.0f, 22.0f, 1.0f, 1.0f, 6, 2},
        {18.0f, -10.0f, 0.85f, 0.85f, 6, 6}, {-18.0f, -4.0f, 0.85f, 0.85f, 5, 3},
        {-34.0f, -20.0f, 1.0f, 1.0f, 6, 2},  {34.0f, 20.0f, 1.0f, 1.0f, 6, 2},
        {-6.0f, 3.0f, 0.65f, 0.55f, 5, 6},    {6.5f, -3.0f, 0.65f, 0.55f, 5, 6},
        {-4.0f, -5.0f, 0.7f, 0.6f, 6, 3},     {7.0f, 5.0f, 0.7f, 0.6f, 6, 6},
        {-10.0f, 1.0f, 0.6f, 0.55f, 5, 6},    {10.0f, -1.0f, 0.6f, 0.55f, 5, 6},
    };
    for (const ObstacleSpec& spec : obstacles) {
        GameObject* object = SpawnSprite(world, "障碍", "sprites/kenney_tilemap.png", Vec2(spec.x, spec.y),
                                         Vec2(spec.hx * 2.0f + 0.35f, spec.hy * 2.0f + 0.35f), 0);
        if (auto* sprite = object->GetComponent<SpriteRendererComponent>()) {
            ApplyTileSprite(sprite, spec.row, spec.col);
            sprite->color = Vec4(0.95f, 0.95f, 0.95f, 1.0f);
        }
        g_obstacles.push_back({Vec2(spec.x, spec.y), Vec2(spec.hx, spec.hy)});
    }

    // 玩家
    GameObject* player = SpawnSprite(world, "玩家", "sprites/soldier_idle.png", Vec2(0.0f, 0.0f), Vec2(1.8f, 1.8f), 5);
    auto* animator = player->AddComponent<SpriteAnimatorComponent>();
    animator->fps = 6.0f;
    animator->frameMin = Vec2(0.410f, 0.380f); // 裁掉 100x100 帧里的透明留白
    animator->frameMax = Vec2(0.580f, 0.600f);
    if (auto* sprite = player->GetComponent<SpriteRendererComponent>()) {
        assets.GetTextureAsset(sprite->texture); // 先加载，OnUpdate 才能按宽高算列数
    }
    animator->OnUpdate(0.0f);
    auto* playerComponent = player->AddComponent<SurvivorsPlayerComponent>();
    playerComponent->arenaHalf = Vec2(43.0f, 25.0f);
    playerComponent->RefreshDerivedStats();

    // 装饰随机数固定，避免关卡每次加载长得不一样；玩法随机数再复位
    g_rng.seed(0x12345u);
    g_fxRng.seed(0x271828u);

    LogInfo("2D 幸存者关卡：%zu 个对象，%zu 个障碍", world.ObjectCount(), g_obstacles.size());
}

// ================================================================ 反射
void RegisterSurvivorsComponents() {
    Reflection::Register<SurvivorsPlayerComponent>("Survivors Player");
    Reflection::Register<SurvivorsEnemyComponent>("Survivors Enemy");
    Reflection::Register<SurvivorsProjectileComponent>("Survivors Projectile");
    Reflection::Register<SurvivorsGemComponent>("Survivors Gem");
}

void SurvivorsPlayerComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<SurvivorsPlayerComponent>(); };
    t.Field("speed", FieldType::Float, &SurvivorsPlayerComponent::speed, "移动速度", 0.5f, 15.0f);
    t.Field("maxHp", FieldType::Float, &SurvivorsPlayerComponent::maxHp, "最大生命", 1.0f, 999.0f);
    t.Field("attackDamage", FieldType::Float, &SurvivorsPlayerComponent::attackDamage, "攻击力", 1.0f, 200.0f);
    t.Field("attackInterval", FieldType::Float, &SurvivorsPlayerComponent::attackInterval, "攻击间隔", 0.05f, 3.0f);
    t.Field("attackRange", FieldType::Float, &SurvivorsPlayerComponent::attackRange, "攻击范围", 1.0f, 30.0f);
    t.Field("projectileSpeed", FieldType::Float, &SurvivorsPlayerComponent::projectileSpeed, "箭速", 1.0f, 60.0f);
    t.Field("arrowSpread", FieldType::Bool, &SurvivorsPlayerComponent::arrowSpread, "八向散射");
    t.Field("arrowSpeedLevel", FieldType::Int, &SurvivorsPlayerComponent::arrowSpeedLevel, "攻速强化", 0.0f, 99.0f);
    t.Field("arrowDamageLevel", FieldType::Int, &SurvivorsPlayerComponent::arrowDamageLevel, "攻击强化", 0.0f, 99.0f);
    t.Field("arrowRangeLevel", FieldType::Int, &SurvivorsPlayerComponent::arrowRangeLevel, "范围强化", 0.0f, 99.0f);
    t.Field("lightningLevel", FieldType::Int, &SurvivorsPlayerComponent::lightningLevel, "雷霆等级", 1.0f, 99.0f);
    t.Field("bladeLevel", FieldType::Int, &SurvivorsPlayerComponent::bladeLevel, "回旋刃等级", 1.0f, 99.0f);
    t.Field("frostLevel", FieldType::Int, &SurvivorsPlayerComponent::frostLevel, "霜环等级", 1.0f, 99.0f);
    t.Field("poisonLevel", FieldType::Int, &SurvivorsPlayerComponent::poisonLevel, "毒雾等级", 1.0f, 99.0f);
    t.Field("fireballLevel", FieldType::Int, &SurvivorsPlayerComponent::fireballLevel, "火球等级", 1.0f, 99.0f);
}

void SurvivorsEnemyComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<SurvivorsEnemyComponent>(); };
    t.Field("speed", FieldType::Float, &SurvivorsEnemyComponent::speed, "移动速度", 0.2f, 12.0f);
    t.Field("hp", FieldType::Float, &SurvivorsEnemyComponent::hp, "生命", 1.0f, 500.0f);
    t.Field("damage", FieldType::Float, &SurvivorsEnemyComponent::damage, "伤害", 1.0f, 100.0f);
    t.Field("xpValue", FieldType::Float, &SurvivorsEnemyComponent::xpValue, "经验值", 1.0f, 50.0f);
    t.Field("type", FieldType::Int, &SurvivorsEnemyComponent::type, "类型", 0.0f, 5.0f);
    t.Field("slowFactor", FieldType::Float, &SurvivorsEnemyComponent::slowFactor, "减速倍率", 0.1f, 1.0f);
}

void SurvivorsProjectileComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<SurvivorsProjectileComponent>(); };
    t.Field("damage", FieldType::Float, &SurvivorsProjectileComponent::damage, "伤害", 1.0f, 200.0f);
    t.Field("life", FieldType::Float, &SurvivorsProjectileComponent::life, "寿命", 0.05f, 5.0f);
}

void SurvivorsGemComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<SurvivorsGemComponent>(); };
    t.Field("value", FieldType::Float, &SurvivorsGemComponent::value, "经验", 1.0f, 100.0f);
    t.Field("magnetRange", FieldType::Float, &SurvivorsGemComponent::magnetRange, "吸附加范围", 0.0f, 20.0f);
}

void SurvivorsPickupComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<SurvivorsPickupComponent>(); };
    t.Field("type", FieldType::Int, &SurvivorsPickupComponent::type, "强化类型", 0.0f, 9.0f);
    t.Field("magnetRange", FieldType::Float, &SurvivorsPickupComponent::magnetRange, "吸附范围", 0.0f, 20.0f);
}

} // namespace erm
