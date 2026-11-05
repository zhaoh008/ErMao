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
#include "rhi/Texture.h"
#include "scene/Components.h"
#include "scene/GameObject.h"
#include "scene/World.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
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
std::vector<GameObject*> g_buffChests; // 地图抽奖宝箱（小地图/上限用）

// 增益宝箱拾取事件：组件先结算增益并登记效果，导演在本帧稍后冻结世界并接管抽奖动画。
struct PendingBuffGacha {
    bool        active = false;
    int         rarity = 0;
    std::string effect;
};
PendingBuffGacha g_pendingBuffGacha;

bool  g_inputEnabled = true;
bool  g_autopilot = false; // 仅平衡测试：自动走向宝箱/技能掉落，不读键盘
int   g_kills = 0;
int   g_killsSinceDrop = 0;
int   g_killsSinceSkillDrop = 0;
int   g_killsSincePotionDrop = 0;
float g_gameTime = 0.0f;
float g_lastSkillDropTime = 0.0f; // 上一次新技能掉落（生成）时的游戏时间，用于时间保底
float g_playerDamageMultiplier = 1.0f; // 地图抽奖“攻击力”buff，所有技能伤害共用

// 新技能时间保底：任何 60 秒窗口内至少掉落一个新技能，不要求击杀。
constexpr float kSkillDropPitySeconds = 60.0f;

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

int NormalizeLoadoutMask(int mask, int fallback = kSurvivorsDefaultLoadout) {
    mask &= 0x3FF;
    int count = 0;
    for (int skill = 0; skill < kSurvivorsSkillCount; ++skill) {
        if ((mask >> skill) & 1) ++count;
    }
    return (count >= 1 && count <= kSurvivorsMaxLoadout) ? mask : fallback;
}

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

bool IsSkillUnlockType(int type) {
    return type >= static_cast<int>(SurvivorsPickupType::SkillUnlockBase) &&
           type < static_cast<int>(SurvivorsPickupType::SkillUnlockEnd);
}

int SkillFromUnlockType(int type) {
    return IsSkillUnlockType(type) ? type - static_cast<int>(SurvivorsPickupType::SkillUnlockBase) : -1;
}

PickupInfo GetPickupInfo(int type) {
    if (IsSkillUnlockType(type)) {
        static const char* kLabels[kSurvivorsSkillCount] = {
            "新技能：箭矢", "新技能：雷霆", "新技能：回旋刃", "新技能：霜环", "新技能：毒雾", "新技能：火球",
            "新技能：陨石", "新技能：黑洞", "新技能：棱镜光束", "新技能：毒爆"};
        static const Vec4 kColors[kSurvivorsSkillCount] = {
            Vec4(1.00f, 0.82f, 0.25f, 1.0f), Vec4(1.00f, 0.95f, 0.35f, 1.0f), Vec4(0.80f, 0.86f, 0.98f, 1.0f),
            Vec4(0.55f, 0.85f, 1.00f, 1.0f), Vec4(0.50f, 1.00f, 0.45f, 1.0f), Vec4(1.00f, 0.55f, 0.20f, 1.0f),
            Vec4(1.00f, 0.40f, 0.15f, 1.0f), Vec4(0.75f, 0.40f, 1.00f, 1.0f), Vec4(0.45f, 0.95f, 1.00f, 1.0f),
            Vec4(0.55f, 1.00f, 0.30f, 1.0f)};
        const int skill = SkillFromUnlockType(type);
        if (skill >= 0 && skill < kSurvivorsSkillCount) return {kLabels[skill], kColors[skill]};
    }
    switch (static_cast<SurvivorsPickupType>(type)) {
        case SurvivorsPickupType::ArrowSpread: return {"箭矢散射", Vec4(1.00f, 0.82f, 0.25f, 1.0f)};
        case SurvivorsPickupType::ArrowAttackSpeed: return {"箭矢攻速", Vec4(0.35f, 0.90f, 1.00f, 1.0f)};
        case SurvivorsPickupType::ArrowAttackDamage: return {"箭矢攻击", Vec4(1.00f, 0.40f, 0.35f, 1.0f)};
        case SurvivorsPickupType::ArrowHealthPotion: return {"血瓶", Vec4(1.00f, 0.35f, 0.55f, 1.0f)};
        case SurvivorsPickupType::ArrowRange: return {"箭矢范围", Vec4(0.45f, 1.00f, 0.65f, 1.0f)};
        case SurvivorsPickupType::LightningUpgrade: return {"雷霆强化", Vec4(1.00f, 0.95f, 0.35f, 1.0f)};
        case SurvivorsPickupType::BladeUpgrade: return {"回旋刃强化", Vec4(0.80f, 0.86f, 0.98f, 1.0f)};
        case SurvivorsPickupType::FrostUpgrade: return {"霜环强化", Vec4(0.55f, 0.85f, 1.00f, 1.0f)};
        case SurvivorsPickupType::PoisonUpgrade: return {"毒雾强化", Vec4(0.50f, 1.00f, 0.45f, 1.0f)};
        case SurvivorsPickupType::FireballUpgrade: return {"火球强化", Vec4(1.00f, 0.55f, 0.20f, 1.0f)};
        case SurvivorsPickupType::MeteorUpgrade: return {"陨石强化", Vec4(1.00f, 0.40f, 0.15f, 1.0f)};
        case SurvivorsPickupType::SingularityUpgrade: return {"黑洞强化", Vec4(0.75f, 0.40f, 1.00f, 1.0f)};
        case SurvivorsPickupType::PrismUpgrade: return {"棱镜强化", Vec4(0.45f, 0.95f, 1.00f, 1.0f)};
        case SurvivorsPickupType::PlagueUpgrade: return {"毒爆强化", Vec4(0.55f, 1.00f, 0.30f, 1.0f)};
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
    float             life = 0.55f; // 稍微拉长，落雷与命中爆点更容易被看到
    float             width = 2.5f;
};

struct FrostRingFx {
    Vec2  center;
    float radius = 0.0f;
    float maxRadius = 4.0f;
    float age = 0.0f;
    float life = 0.65f;
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
    float age = 0.0f;
};

struct PoisonPuffFx {
    Vec2  position;
    Vec2  velocity;
    float age = 0.0f;
    float life = 1.0f;
    float size = 0.5f;
};

struct MeteorFx {
    Vec2  target;
    float age = 0.0f;
    float delay = 0.35f;
    float damage = 0.0f;
    float radius = 2.0f;
    bool  impacted = false;
};

struct SingularityFx {
    Vec2  center;
    float age = 0.0f;
    float life = 2.0f;
    float radius = 4.0f;
    float dps = 0.0f;
    float tick = 0.0f;
};

std::vector<LightningFx>  g_lightningFx;
std::vector<FrostRingFx>  g_frostFx;
std::vector<ExplosionFx>  g_explosionFx;
std::vector<FireballFx>   g_fireballs;
std::vector<PoisonPuffFx> g_poisonFx;
std::vector<MeteorFx>     g_meteors;
std::vector<SingularityFx> g_singularities;

void UpdateSkillEffects(float dt) {
    for (LightningFx& fx : g_lightningFx) fx.age += dt;
    for (FireballFx& fireball : g_fireballs) fireball.age += dt;
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

    for (PoisonPuffFx& puff : g_poisonFx) {
        puff.age += dt;
        puff.position += puff.velocity * dt;
        puff.velocity *= (1.0f - 1.6f * dt);
    }
    g_poisonFx.erase(std::remove_if(g_poisonFx.begin(), g_poisonFx.end(),
                                    [](const PoisonPuffFx& puff) { return puff.age >= puff.life; }),
                     g_poisonFx.end());
}

void ClearSkillEffects() {
    g_lightningFx.clear();
    g_frostFx.clear();
    g_explosionFx.clear();
    g_fireballs.clear();
    g_poisonFx.clear();
    g_meteors.clear();
    g_singularities.clear();
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
    const bool  skillUnlock = IsSkillUnlockType(type);
    const float size = skillUnlock ? 1.15f : 0.85f;
    GameObject* pickup = SpawnSprite(world, skillUnlock ? "技能掉落" : "强化道具", "pickup_proc", position,
                                     Vec2(size, size), 4);
    auto*       sprite = pickup->GetComponent<SpriteRendererComponent>();
    if (sprite) sprite->color = GetPickupInfo(type).color;
    auto* component = pickup->AddComponent<SurvivorsPickupComponent>();
    component->type = type;
    // 时间保底以“新技能真正掉到地上”为准；所有掉落路径（击杀/新手兜底/时间保底）共用。
    if (skillUnlock) g_lastSkillDropTime = g_gameTime;
    g_pickups.push_back(pickup);
}

int PickSkillUnlockType(const SurvivorsPlayerComponent& player) {
    std::vector<int> candidates;
    for (int skill = 0; skill < kSurvivorsSkillCount; ++skill) {
        if (!player.HasSkill(skill)) candidates.push_back(skill);
    }
    if (candidates.empty()) return -1;
    return candidates[static_cast<size_t>(Random01() * static_cast<float>(candidates.size())) %
                      candidates.size()];
}

// 在玩家附近生成一个未拥有的新技能（仍需手动拾取）。返回是否成功生成。
bool SpawnSkillUnlockNearPlayer(World& world, GameObject& playerObject, const SurvivorsPlayerComponent& player,
                                float radius) {
    if (player.SelectedSkillCount() >= kSurvivorsMaxLoadout) return false;
    const int skill = PickSkillUnlockType(player);
    if (skill < 0) return false;
    const Vec2  playerPosition(playerObject.Position().x, playerObject.Position().y);
    const float angle = Random01() * kTwoPi;
    SpawnPickup(world, playerPosition + Vec2(std::cos(angle), std::sin(angle)) * radius,
                static_cast<int>(SurvivorsPickupType::SkillUnlockBase) + skill);
    return true;
}

int PickUpgradePickupType(const SurvivorsPlayerComponent& player) {
    struct WeightedPickup {
        int   type;
        float weight;
    };
    std::vector<WeightedPickup> pool;
    if (player.HasSkill(0) && player.SkillLevel(0) < kSurvivorsMaxSkillLevel) {
        if (player.arrowSpreadLevel < kSurvivorsMaxArrowSpreadLevel)
            pool.push_back({static_cast<int>(SurvivorsPickupType::ArrowSpread), 8.0f});
        if (player.arrowSpeedLevel < kSurvivorsMaxArrowSpeedLevel)
            pool.push_back({static_cast<int>(SurvivorsPickupType::ArrowAttackSpeed), 12.0f});
        if (player.arrowDamageLevel < kSurvivorsMaxArrowDamageLevel)
            pool.push_back({static_cast<int>(SurvivorsPickupType::ArrowAttackDamage), 12.0f});
        if (player.arrowRangeLevel < kSurvivorsMaxArrowRangeLevel)
            pool.push_back({static_cast<int>(SurvivorsPickupType::ArrowRange), 8.0f});
    }
    for (int skill = 1; skill < kSurvivorsSkillCount; ++skill) {
        if (!player.HasSkill(skill) || player.SkillMaxed(skill)) continue;
        switch (skill) {
            case 1: pool.push_back({static_cast<int>(SurvivorsPickupType::LightningUpgrade), 10.0f}); break;
            case 2: pool.push_back({static_cast<int>(SurvivorsPickupType::BladeUpgrade), 10.0f}); break;
            case 3: pool.push_back({static_cast<int>(SurvivorsPickupType::FrostUpgrade), 10.0f}); break;
            case 4: pool.push_back({static_cast<int>(SurvivorsPickupType::PoisonUpgrade), 10.0f}); break;
            case 5: pool.push_back({static_cast<int>(SurvivorsPickupType::FireballUpgrade), 10.0f}); break;
            case 6: pool.push_back({static_cast<int>(SurvivorsPickupType::MeteorUpgrade), 10.0f}); break;
            case 7: pool.push_back({static_cast<int>(SurvivorsPickupType::SingularityUpgrade), 10.0f}); break;
            case 8: pool.push_back({static_cast<int>(SurvivorsPickupType::PrismUpgrade), 10.0f}); break;
            case 9: pool.push_back({static_cast<int>(SurvivorsPickupType::PlagueUpgrade), 10.0f}); break;
            default: break;
        }
    }
    if (pool.empty()) return -1;

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
    (void)enemy;

    ++g_killsSinceDrop;
    ++g_killsSinceSkillDrop;
    ++g_killsSincePotionDrop;

    // 技能掉落优先：一局最多 4 个额外技能，掉在地上必须走过去捡，不会被吸走。
    // 新技能爆率在上一版基础上减半（9%），保底间隔同步翻倍。
    if (player->SelectedSkillCount() < kSurvivorsMaxLoadout) {
        const int owned = player->SelectedSkillCount();
        const int pityKills = owned <= 1 ? 45 : (owned == 2 ? 55 : 65);
        const bool  pity = g_killsSinceSkillDrop >= pityKills;
        float       chance = pity ? 1.0f : 0.02f;
        // 新手期兜底：2 分钟后开始提高爆率，2:30 起每次击杀必掉，确保 3 分钟内凑齐 5 个技能。
        if (g_gameTime > 120.0f) {
            chance = (std::max)(chance, (std::min)(1.0f, 0.25f + (g_gameTime - 120.0f) * 0.05f));
        }
        if (g_gameTime >= 150.0f) chance = 1.0f;
        if (Random01() < chance) {
            const int skill = PickSkillUnlockType(*player);
            if (skill >= 0) {
                // 远程击杀时把技能掉落拉回玩家 6 单位内，保证手动拾取目标始终在镜头内。
                const Vec2 playerPosition(playerObject->Position().x, playerObject->Position().y);
                Vec2       dropPosition = position;
                const float distance = Length(dropPosition - playerPosition);
                if (distance > 6.0f) {
                    dropPosition = playerPosition + SafeDir(dropPosition - playerPosition) * 6.0f;
                }
                SpawnPickup(world, dropPosition + Vec2(RandomRange(-0.25f, 0.25f), RandomRange(-0.2f, 0.2f)),
                            static_cast<int>(SurvivorsPickupType::SkillUnlockBase) + skill);
                g_killsSinceSkillDrop = 0;
                return;
            }
        }
    }

    // 强化只从已拥有且未满级的技能里掉落；成长速度由掉落率分段控制：
    // 前期少掉（5%/14 杀保底），中期加速（9%/9），6–9 分钟补满（16%/6），9 分钟后维持（20%/5）。
    float upgradeChance = 0.005f;
    int   upgradePity = 150;
    if (g_gameTime < 180.0f) {
        upgradeChance = 0.04f;
        upgradePity = 24;
    } else if (g_gameTime < 360.0f) {
        upgradeChance = 0.018f;
        upgradePity = 55;
    } else if (g_gameTime < 540.0f) {
        upgradeChance = 0.010f;
        upgradePity = 75;
    }
    if (Random01() < upgradeChance || g_killsSinceDrop >= upgradePity) {
        const int upgradeType = PickUpgradePickupType(*player);
        if (upgradeType >= 0) {
            SpawnPickup(world, position + Vec2(RandomRange(-0.35f, 0.35f), RandomRange(-0.28f, 0.28f)),
                        upgradeType);
            g_killsSinceDrop = 0;
        }
    }

    // 血瓶独立掉率：约为上一版有效掉率的 1/4~1/5；9 分钟后进一步减半。
    const bool  lowHp = player->hp < player->maxHp * 0.35f;
    float       potionChance = lowHp ? 0.004f : 0.0015f;
    const int   potionPity = g_gameTime > 540.0f ? 450 : 300;
    if (g_gameTime > 540.0f) potionChance *= 0.5f;
    if (Random01() < potionChance || g_killsSincePotionDrop >= potionPity) {
        SpawnPickup(world, position + Vec2(RandomRange(-0.35f, 0.35f), RandomRange(-0.28f, 0.28f)),
                    static_cast<int>(SurvivorsPickupType::ArrowHealthPotion));
        g_killsSincePotionDrop = 0;
    }
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
    // 已经卡进障碍（例如被同伴挤进去）时允许先走出去，否则会被碰撞检测永久钉住。
    if (CircleHitsObstacle(position, radius)) return position + delta;
    const Vec2 target = position + delta;
    if (!CircleHitsObstacle(target, radius)) return target;
    const Vec2 xOnly(position.x + delta.x, position.y);
    if (!CircleHitsObstacle(xOnly, radius)) return xOnly;
    const Vec2 yOnly(position.x, position.y + delta.y);
    if (!CircleHitsObstacle(yOnly, radius)) return yOnly;
    return position;
}

// 敌人绕障：朝玩家走不通时依次尝试左右偏转角，选“最接近目标方向且确实前进”的那个。
// 固定一侧优先（strafeSign）避免两个等分方向来回抖动。
Vec2 MoveEnemyWithSteering(const Vec2& position, const Vec2& desiredDirection, float distance, float radius,
                           float strafeSign) {
    const Vec2 base = SafeDir(desiredDirection);
    if (distance <= 1e-6f || Length(base) < 1e-5f) return position;

    static const float kProbeDegrees[] = {0.0f, 25.0f, -25.0f, 50.0f, -50.0f, 75.0f, -75.0f,
                                          100.0f, -100.0f, 130.0f, -130.0f, 160.0f, -160.0f};
    Vec2  best = position;
    float bestScore = -2.0f;
    for (float degrees : kProbeDegrees) {
        const float side = (strafeSign < 0.0f) ? -1.0f : 1.0f;
        const float radians = degrees * side * kPi / 180.0f;
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        const Vec2  dir(base.x * c - base.y * s, base.x * s + base.y * c);
        const Vec2  next = MoveWithObstacles(position, dir * distance, radius);
        const Vec2  moved = next - position;
        const float movedLength = Length(moved);
        if (movedLength <= 1e-4f) continue;
        float score = Dot(SafeDir(moved), base) + 0.2f * (movedLength / distance);
        if (degrees != 0.0f) score += 0.02f; // 同等进度时优先主动绕行，避免贴着墙原地磨
        if (score > bestScore) {
            bestScore = score;
            best = next;
        }
    }
    return best;
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
    // 地图抽奖的攻击力 buff 对所有伤害生效。
    damage *= g_playerDamageMultiplier;
    // 毒雾联动：中毒目标受到的伤害提高 15%，让毒雾成为其他技能的增伤铺垫。
    if (component->poisonTimer > 0.0f) damage *= 1.15f;
    component->hp -= damage;
    component->hitFlash = 0.55f;
    SpawnDamageNumber(Vec2(enemy->Position().x, enemy->Position().y + 0.8f), damage);
    PlayHitSfx();
}

void SpawnExplosion(const Vec2& center, float radius, const Vec4& color);

void UpdateMeteors(World& world, float dt) {
    for (MeteorFx& meteor : g_meteors) {
        meteor.age += dt;
        if (!meteor.impacted && meteor.age >= meteor.delay) {
            meteor.impacted = true;
            std::vector<GameObject*> enemies;
            GatherEnemiesInRadius(meteor.target, meteor.radius, enemies);
            for (GameObject* enemy : enemies) DamageEnemy(enemy, meteor.damage);
            SpawnExplosion(meteor.target, meteor.radius, Vec4(1.0f, 0.50f, 0.15f, 1.0f));
            PlaySfx("fireball", 0.42f, 0.72f);
        }
    }
    g_meteors.erase(std::remove_if(g_meteors.begin(), g_meteors.end(),
                                   [](const MeteorFx& meteor) { return meteor.age > meteor.delay + 0.5f; }),
                    g_meteors.end());
}

void UpdateSingularities(World& world, float dt) {
    for (SingularityFx& hole : g_singularities) {
        hole.age += dt;
        hole.tick -= dt;
        if (hole.tick > 0.0f) continue;
        hole.tick = 0.35f;
        std::vector<GameObject*> enemies;
        GatherEnemiesInRadius(hole.center, hole.radius, enemies);
        for (GameObject* enemy : enemies) {
            Vec2 position(enemy->Position().x, enemy->Position().y);
            const Vec2 direction = SafeDir(hole.center - position);
            const Vec2 pulled = MoveWithObstacles(position, direction * 2.6f * 0.35f, 0.3f);
            Vec3 next = enemy->Position();
            next.x = pulled.x;
            next.y = pulled.y;
            enemy->SetPosition(next);
            DamageEnemy(enemy, hole.dps * 0.35f);
            if (auto* component = enemy->GetComponent<SurvivorsEnemyComponent>()) component->poisonTimer = 0.55f;
        }
    }
    g_singularities.erase(std::remove_if(g_singularities.begin(), g_singularities.end(),
                                         [](const SingularityFx& hole) { return hole.age >= hole.life; }),
                          g_singularities.end());
}

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

}

// ================================================================ 玩家：设置/数值
void SurvivorsPlayerComponent::SetInputEnabled(bool enabled) { g_inputEnabled = enabled; }
bool SurvivorsPlayerComponent::InputEnabled() { return g_inputEnabled; }

void SurvivorsPlayerComponent::RecomputeAttackInterval() {
    // 击杀攻速最多 +50%，掉落攻速最多 5 级，等级攻速最低到 0.62 倍间隔：
    // 三条通道都封顶，避免后期攻击间隔塌缩成每帧一箭。
    const float killBonus = (std::min)(0.5f, 0.005f * static_cast<float>(killCount));
    m_killIntervalScale = 1.0f / (1.0f + killBonus);
    m_dropIntervalScale = std::pow(0.96f, static_cast<float>(arrowSpeedLevel));
    attackInterval = Clamp(baseAttackInterval * m_levelIntervalScale * m_killIntervalScale * m_dropIntervalScale,
                           0.16f, baseAttackInterval);
}

void SurvivorsPlayerComponent::RecomputeAttackDamage() { attackDamage = ArrowDamage(); }

void SurvivorsPlayerComponent::RefreshDerivedStats() {
    // 等级带来的攻速缩放可以直接由 level 还原，读档时不会丢掉升级攻速
    m_levelIntervalScale = (std::max)(0.62f, std::pow(0.95f, static_cast<float>(level - 1)));
    RecomputeAttackInterval();
    RecomputeAttackDamage();
}

int SurvivorsPlayerComponent::SelectedSkillCount() const {
    int count = 0;
    for (int skill = 0; skill < kSurvivorsSkillCount; ++skill) {
        if (HasSkill(skill)) ++count;
    }
    return count;
}

int SurvivorsPlayerComponent::SkillLevel(int skill) const {
    if (!HasSkill(skill)) return 0;
    switch (skill) {
        case 0: return 1 + arrowSpeedLevel + arrowDamageLevel + arrowRangeLevel + arrowSpreadLevel;
        case 1: return lightningLevel;
        case 2: return bladeLevel;
        case 3: return frostLevel;
        case 4: return poisonLevel;
        case 5: return fireballLevel;
        case 6: return meteorLevel;
        case 7: return singularityLevel;
        case 8: return prismLevel;
        case 9: return plagueLevel;
        default: return 0;
    }
}

bool SurvivorsPlayerComponent::SkillMaxed(int skill) const {
    if (!HasSkill(skill)) return true;
    if (skill == 0) {
        return arrowSpreadLevel >= kSurvivorsMaxArrowSpreadLevel &&
               arrowSpeedLevel >= kSurvivorsMaxArrowSpeedLevel &&
               arrowDamageLevel >= kSurvivorsMaxArrowDamageLevel &&
               arrowRangeLevel >= kSurvivorsMaxArrowRangeLevel;
    }
    if (skill < 1 || skill > 9) return true;
    return SkillLevel(skill) >= kSurvivorsMaxSkillLevel;
}

bool SurvivorsPlayerComponent::UnlockSkill(int skill) {
    if (skill < 0 || skill >= kSurvivorsSkillCount || HasSkill(skill)) return false;
    if (SelectedSkillCount() >= kSurvivorsMaxLoadout) return false;

    loadoutMask |= (1 << skill);
    switch (skill) {
        case 0: arrowSpreadLevel = 0; break;
        case 1: lightningLevel = 1; m_lightningTimer = 0.5f; break;
        case 2: bladeLevel = 1; break;
        case 3: frostLevel = 1; m_frostTimer = 1.0f; break;
        case 4: poisonLevel = 1; m_poisonTick = 0.0f; break;
        case 5: fireballLevel = 1; m_fireballTimer = 1.2f; break;
        case 6: meteorLevel = 1; m_meteorTimer = 1.0f; break;
        case 7: singularityLevel = 1; m_singularityTimer = 1.5f; break;
        case 8: prismLevel = 1; m_prismTick = 0.0f; break;
        case 9: plagueLevel = 1; m_plagueTimer = 1.5f; break;
        default: break;
    }
    RefreshDerivedStats();
    static const char* kNames[kSurvivorsSkillCount] = {"箭矢", "雷霆", "回旋刃", "霜环", "毒雾",
                                                        "火球", "陨石", "黑洞", "棱镜光束", "毒爆"};
    ShowPickupToast(std::string("获得新技能：") + kNames[skill]);
    PlaySfx("levelup", 0.62f, 1.18f);
    LogInfo("[2D] 获得新技能: %s（本局 %d/%d）", kNames[skill], SelectedSkillCount(), kSurvivorsMaxLoadout);
    return true;
}

float SurvivorsPlayerComponent::KillAttackSpeedBonus() const {
    return m_killIntervalScale > 1e-5f ? 1.0f / m_killIntervalScale - 1.0f : 0.0f;
}

float SurvivorsPlayerComponent::ArrowSpeedBonus() const {
    const float scale = m_killIntervalScale * m_dropIntervalScale;
    return scale > 1e-5f ? 1.0f / scale - 1.0f : 0.0f;
}

int SurvivorsPlayerComponent::ArrowDirections() const {
    const int level = Clamp(arrowSpreadLevel, 0, kSurvivorsMaxArrowSpreadLevel);
    return kSurvivorsArrowDirectionCounts[level];
}

float SurvivorsPlayerComponent::ArrowDamage() const {
    if (!HasSkill(0)) return 0.0f;
    // 箭矢技能等级 1~20：按与怪物强度匹配的 1.22 曲线成长，20 级约 12×1.22^19。
    const int arrowLevel = Clamp(SkillLevel(0), 1, kSurvivorsMaxSkillLevel);
    return baseAttackDamage * std::pow(kSurvivorsSkillDamageGrowth, static_cast<float>(arrowLevel - 1));
}

float SurvivorsPlayerComponent::ArrowRange() const {
    return (baseAttackRange + 0.8f * static_cast<float>(arrowRangeLevel)) * buffRangeMultiplier;
}

float SurvivorsPlayerComponent::ArrowProjectileLife() const {
    return baseProjectileLife + 0.12f * static_cast<float>(arrowRangeLevel);
}

float SurvivorsPlayerComponent::BladeAngle() const { return m_bladeAngle; }

// 所有技能最多 20 级：伤害按 1.22 曲线成长，冷却只做小幅递减并设下限。
float SurvivorsPlayerComponent::LightningDamage() const {
    const int level = (std::min)(lightningLevel, kSurvivorsMaxSkillLevel);
    return level <= 0 ? 0.0f : 12.0f * std::pow(kSurvivorsSkillDamageGrowth, static_cast<float>(level - 1));
}
int SurvivorsPlayerComponent::LightningTargets() const {
    const int level = (std::min)(lightningLevel, kSurvivorsMaxSkillLevel);
    return level <= 0 ? 0 : (std::min)(10, 1 + (level - 1) / 2);
}
int SurvivorsPlayerComponent::LightningChains() const {
    const int level = (std::min)(lightningLevel, kSurvivorsMaxSkillLevel);
    return level <= 0 ? 0 : (std::min)(3, (level - 1) / 5);
}
float SurvivorsPlayerComponent::LightningRadius() const {
    const int level = (std::min)(lightningLevel, kSurvivorsMaxSkillLevel);
    return (8.5f + 0.55f * static_cast<float>((std::max)(0, level - 1))) * buffRangeMultiplier;
}
float SurvivorsPlayerComponent::LightningCooldown() const {
    const int level = (std::min)(lightningLevel, kSurvivorsMaxSkillLevel);
    return (std::max)(0.85f, 2.6f * std::pow(0.93f, static_cast<float>((std::max)(0, level - 1))));
}

float SurvivorsPlayerComponent::BladeDamage() const {
    const int level = (std::min)(bladeLevel, kSurvivorsMaxSkillLevel);
    return level <= 0 ? 0.0f : 10.0f * std::pow(kSurvivorsSkillDamageGrowth, static_cast<float>(level - 1));
}
int SurvivorsPlayerComponent::BladeCount() const {
    const int level = (std::min)(bladeLevel, kSurvivorsMaxSkillLevel);
    return level <= 0 ? 0 : (std::min)(8, 2 + (level - 1) / 3);
}
float SurvivorsPlayerComponent::BladeRadius() const {
    // 环绕半径一级就到 2.6，随等级成长；满级大致维持原设计的外圈范围。
    const int level = (std::min)(bladeLevel, kSurvivorsMaxSkillLevel);
    return level <= 0 ? 0.0f : (2.6f + 0.22f * static_cast<float>(level - 1)) * buffRangeMultiplier;
}
float SurvivorsPlayerComponent::BladeSize() const {
    const int level = (std::min)(bladeLevel, kSurvivorsMaxSkillLevel);
    return level <= 0 ? 0.0f : 0.50f + 0.035f * static_cast<float>(level - 1);
}
float SurvivorsPlayerComponent::BladeSpeed() const {
    const int level = (std::min)(bladeLevel, kSurvivorsMaxSkillLevel);
    return 4.2f + 0.45f * static_cast<float>((std::max)(1, level));
}

float SurvivorsPlayerComponent::FrostDamage() const {
    const int level = (std::min)(frostLevel, kSurvivorsMaxSkillLevel);
    return level <= 0 ? 0.0f : 12.0f * std::pow(kSurvivorsSkillDamageGrowth, static_cast<float>(level - 1));
}
float SurvivorsPlayerComponent::FrostRadius() const {
    const int level = (std::min)(frostLevel, kSurvivorsMaxSkillLevel);
    return (4.2f + 0.40f * static_cast<float>((std::max)(0, level - 1))) * buffRangeMultiplier;
}
float SurvivorsPlayerComponent::FrostFreezeDuration() const {
    const int level = (std::min)(frostLevel, kSurvivorsMaxSkillLevel);
    return level <= 0 ? 0.0f : (std::min)(1.65f, 0.8f + 0.06f * static_cast<float>(level - 1));
}
float SurvivorsPlayerComponent::FrostCooldown() const {
    const int level = (std::min)(frostLevel, kSurvivorsMaxSkillLevel);
    return (std::max)(1.8f, 4.5f * std::pow(0.94f, static_cast<float>((std::max)(0, level - 1))));
}

float SurvivorsPlayerComponent::PoisonDps() const {
    const int level = (std::min)(poisonLevel, kSurvivorsMaxSkillLevel);
    return level <= 0 ? 0.0f : 12.0f * std::pow(1.25f, static_cast<float>(level - 1));
}
float SurvivorsPlayerComponent::PoisonRadius() const {
    const int level = (std::min)(poisonLevel, kSurvivorsMaxSkillLevel);
    return (3.2f + 0.32f * static_cast<float>((std::max)(0, level - 1))) * buffRangeMultiplier;
}

float SurvivorsPlayerComponent::FireballDamage() const {
    const int level = (std::min)(fireballLevel, kSurvivorsMaxSkillLevel);
    return level <= 0 ? 0.0f : 18.0f * std::pow(kSurvivorsSkillDamageGrowth, static_cast<float>(level - 1));
}
int SurvivorsPlayerComponent::FireballCount() const {
    const int level = (std::min)(fireballLevel, kSurvivorsMaxSkillLevel);
    return level <= 0 ? 0 : (std::min)(5, 1 + (level - 1) / 4);
}
float SurvivorsPlayerComponent::FireballExplosionRadius() const {
    const int level = (std::min)(fireballLevel, kSurvivorsMaxSkillLevel);
    return (2.5f + 0.25f * static_cast<float>((std::max)(0, level - 1))) * buffRangeMultiplier;
}
float SurvivorsPlayerComponent::FireballCooldown() const {
    const int level = (std::min)(fireballLevel, kSurvivorsMaxSkillLevel);
    return (std::max)(0.9f, 3.2f * std::pow(0.93f, static_cast<float>((std::max)(0, level - 1))));
}

float SurvivorsPlayerComponent::MeteorDamage() const {
    const int level = (std::min)(meteorLevel, kSurvivorsMaxSkillLevel);
    return level <= 0 ? 0.0f : 30.0f * std::pow(kSurvivorsSkillDamageGrowth, static_cast<float>(level - 1));
}
int SurvivorsPlayerComponent::MeteorCount() const {
    const int level = (std::min)(meteorLevel, kSurvivorsMaxSkillLevel);
    return level <= 0 ? 0 : (std::min)(5, 1 + (level - 1) / 4);
}
float SurvivorsPlayerComponent::MeteorCooldown() const {
    const int level = (std::min)(meteorLevel, kSurvivorsMaxSkillLevel);
    return (std::max)(1.6f, 5.5f * std::pow(0.94f, static_cast<float>((std::max)(0, level - 1))));
}
float SurvivorsPlayerComponent::MeteorRadius() const {
    const int level = (std::min)(meteorLevel, kSurvivorsMaxSkillLevel);
    return (2.2f + 0.22f * static_cast<float>((std::max)(0, level - 1))) * buffRangeMultiplier;
}

float SurvivorsPlayerComponent::SingularityDps() const {
    const int level = (std::min)(singularityLevel, kSurvivorsMaxSkillLevel);
    return level <= 0 ? 0.0f : 12.0f * std::pow(kSurvivorsSkillDamageGrowth, static_cast<float>(level - 1));
}
float SurvivorsPlayerComponent::SingularityRadius() const {
    const int level = (std::min)(singularityLevel, kSurvivorsMaxSkillLevel);
    return (3.0f + 0.30f * static_cast<float>((std::max)(0, level - 1))) * buffRangeMultiplier;
}
float SurvivorsPlayerComponent::SingularityDuration() const {
    const int level = (std::min)(singularityLevel, kSurvivorsMaxSkillLevel);
    return level <= 0 ? 0.0f : 1.2f + 0.08f * static_cast<float>(level - 1);
}
float SurvivorsPlayerComponent::SingularityCooldown() const {
    const int level = (std::min)(singularityLevel, kSurvivorsMaxSkillLevel);
    return (std::max)(3.0f, 8.0f * std::pow(0.95f, static_cast<float>((std::max)(0, level - 1))));
}

float SurvivorsPlayerComponent::PrismDamage() const {
    const int level = (std::min)(prismLevel, kSurvivorsMaxSkillLevel);
    return level <= 0 ? 0.0f : 8.0f * std::pow(kSurvivorsSkillDamageGrowth, static_cast<float>(level - 1));
}
int SurvivorsPlayerComponent::PrismBeamCount() const {
    const int level = (std::min)(prismLevel, kSurvivorsMaxSkillLevel);
    return level <= 0 ? 0 : (std::min)(5, 2 + (level - 1) / 5);
}
float SurvivorsPlayerComponent::PrismBeamLength() const {
    const int level = (std::min)(prismLevel, kSurvivorsMaxSkillLevel);
    return (7.0f + 0.60f * static_cast<float>((std::max)(0, level - 1))) * buffRangeMultiplier;
}
float SurvivorsPlayerComponent::PrismBeamSpeed() const {
    const int level = (std::min)(prismLevel, kSurvivorsMaxSkillLevel);
    return 1.2f + 0.08f * static_cast<float>((std::max)(1, level));
}
float SurvivorsPlayerComponent::PrismBeamAngle() const { return m_prismAngle; }

float SurvivorsPlayerComponent::PlagueDamage() const {
    const int level = (std::min)(plagueLevel, kSurvivorsMaxSkillLevel);
    return level <= 0 ? 0.0f : 10.0f * std::pow(kSurvivorsSkillDamageGrowth, static_cast<float>(level - 1));
}
float SurvivorsPlayerComponent::PlagueRadius() const {
    const int level = (std::min)(plagueLevel, kSurvivorsMaxSkillLevel);
    return (2.0f + 0.20f * static_cast<float>((std::max)(0, level - 1))) * buffRangeMultiplier;
}
float SurvivorsPlayerComponent::PlagueCooldown() const {
    const int level = (std::min)(plagueLevel, kSurvivorsMaxSkillLevel);
    return (std::max)(1.5f, 3.5f * std::pow(0.95f, static_cast<float>((std::max)(0, level - 1))));
}

std::string SurvivorsPlayerComponent::ApplyBuff(int rarity) {
    rarity = Clamp(rarity, 0, 2);
    static const char* kRarity[3] = {"普通", "稀有", "史诗"};
    const float damageFactor[3] = {1.08f, 1.15f, 1.25f};
    const float speedFactor[3] = {1.08f, 1.15f, 1.25f};
    const float moveFactor[3] = {1.05f, 1.10f, 1.15f};
    const float hpAmount[3] = {15.0f, 30.0f, 50.0f};
    const float rangeFactor[3] = {1.06f, 1.10f, 1.15f};
    const float healPct[3] = {0.25f, 0.45f, 0.70f};

    std::string effect;
    const int effectCount = rarity >= 2 ? 8 : 7;
    int roll;
    if (buffPityOffense >= 2) {
        roll = Random01() < 0.5f ? 0 : 1;
    } else {
        roll = static_cast<int>(Random01() * static_cast<float>(effectCount)) % effectCount;
    }
    if (roll == 0 || roll == 1) {
        buffPityOffense = 0;
    } else {
        ++buffPityOffense;
    }
    switch (roll) {
        case 0:
            buffDamageMultiplier = (std::min)(2.0f, buffDamageMultiplier * damageFactor[rarity]);
            g_playerDamageMultiplier = buffDamageMultiplier;
            effect = "攻击力 +" + std::to_string(static_cast<int>((damageFactor[rarity] - 1.0f) * 100.0f)) + "%";
            break;
        case 1:
            buffAttackSpeedMultiplier = (std::min)(1.8f, buffAttackSpeedMultiplier * speedFactor[rarity]);
            effect = "攻击速度 +" + std::to_string(static_cast<int>((speedFactor[rarity] - 1.0f) * 100.0f)) + "%";
            break;
        case 2:
            buffMoveMultiplier = (std::min)(1.35f, buffMoveMultiplier * moveFactor[rarity]);
            effect = "移动速度 +" + std::to_string(static_cast<int>((moveFactor[rarity] - 1.0f) * 100.0f)) + "%";
            break;
        case 3:
            maxHp += hpAmount[rarity];
            hp = (std::min)(maxHp, hp + hpAmount[rarity]);
            effect = "最大生命 +" + std::to_string(static_cast<int>(hpAmount[rarity]));
            break;
        case 4:
            buffRangeMultiplier = (std::min)(1.30f, buffRangeMultiplier * rangeFactor[rarity]);
            effect = "技能范围 +" + std::to_string(static_cast<int>((rangeFactor[rarity] - 1.0f) * 100.0f)) + "%";
            break;
        case 5: {
            if (rarity < 2 || buffSkillUpgrades >= 2) {
                hp = (std::min)(maxHp, hp + maxHp * healPct[rarity]);
                effect = "生命恢复";
                break;
            }
            std::vector<int> owned;
            for (int skill = 0; skill < kSurvivorsSkillCount; ++skill) {
                if (HasSkill(skill)) owned.push_back(skill);
            }
            if (owned.empty()) {
                hp = (std::min)(maxHp, hp + maxHp * healPct[rarity]);
                effect = "生命恢复";
                break;
            }
            const int skill = owned[static_cast<size_t>(Random01() * static_cast<float>(owned.size())) % owned.size()];
            bool applied = false;
            if (skill == 0) {
                if (arrowSpreadLevel < kSurvivorsMaxArrowSpreadLevel) {
                    ++arrowSpreadLevel;
                    applied = true;
                } else if (arrowSpeedLevel < kSurvivorsMaxArrowSpeedLevel) {
                    ++arrowSpeedLevel;
                    applied = true;
                } else if (arrowDamageLevel < kSurvivorsMaxArrowDamageLevel) {
                    ++arrowDamageLevel;
                    applied = true;
                } else if (arrowRangeLevel < kSurvivorsMaxArrowRangeLevel) {
                    ++arrowRangeLevel;
                    applied = true;
                }
            } else {
                int* level = nullptr;
                switch (skill) {
                    case 1: level = &lightningLevel; break;
                    case 2: level = &bladeLevel; break;
                    case 3: level = &frostLevel; break;
                    case 4: level = &poisonLevel; break;
                    case 5: level = &fireballLevel; break;
                    case 6: level = &meteorLevel; break;
                    case 7: level = &singularityLevel; break;
                    case 8: level = &prismLevel; break;
                    case 9: level = &plagueLevel; break;
                    default: break;
                }
                if (level && *level < kSurvivorsMaxSkillLevel) {
                    *level += rarity >= 2 ? 2 : 1;
                    *level = (std::min)(*level, kSurvivorsMaxSkillLevel);
                    applied = true;
                }
            }
            if (applied) {
                ++buffSkillUpgrades;
                effect = "随机技能强化";
            } else {
                hp = (std::min)(maxHp, hp + maxHp * healPct[rarity]);
                effect = "技能已满，生命恢复";
            }
            break;
        }
        case 6:
            hp = (std::min)(maxHp, hp + maxHp * healPct[rarity]);
            effect = "生命恢复 " + std::to_string(static_cast<int>(healPct[rarity] * 100.0f)) + "%";
            break;
        default: {
            // 史诗专属：全屏雷击
            for (GameObject* enemy : g_enemies) {
                if (enemy && enemy->Active()) DamageEnemy(enemy, 180.0f + 12.0f * static_cast<float>(level));
            }
            effect = "全屏雷击";
            break;
        }
    }

    RefreshDerivedStats();
    ShowPickupToast(std::string("抽到 ") + kRarity[rarity] + "：" + effect);
    PlaySfx("levelup", 0.55f, 1.1f + static_cast<float>(rarity) * 0.15f);
    return effect;
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
    pendingLevelUps = 0;
    attackDamage = baseAttackDamage;
    maxHp = baseMaxHp;
    hp = maxHp;
    alive = true;
    invulnerable = 0.0f;
    hitFlash = 0.0f;
    killCount = 0;
    arrowSpreadLevel = 0;
    arrowSpeedLevel = 0;
    arrowDamageLevel = 0;
    arrowRangeLevel = 0;
    buffDamageMultiplier = 1.0f;
    buffAttackSpeedMultiplier = 1.0f;
    buffMoveMultiplier = 1.0f;
    buffRangeMultiplier = 1.0f;
    buffSkillUpgrades = 0;
    buffPityOffense = 0;
    g_playerDamageMultiplier = 1.0f;
    facing = Vec2(1.0f, 0.0f);
    const int classIndex = Clamp(playerClass, 0, kSurvivorsClassCount - 1);
    loadoutMask = 1 << kSurvivorsClasses[classIndex].baseSkill;
    lightningLevel = HasSkill(1) ? 1 : 0;
    bladeLevel = HasSkill(2) ? 1 : 0;
    frostLevel = HasSkill(3) ? 1 : 0;
    poisonLevel = HasSkill(4) ? 1 : 0;
    fireballLevel = HasSkill(5) ? 1 : 0;
    meteorLevel = HasSkill(6) ? 1 : 0;
    singularityLevel = HasSkill(7) ? 1 : 0;
    prismLevel = HasSkill(8) ? 1 : 0;
    plagueLevel = HasSkill(9) ? 1 : 0;
    pickupToast.clear();
    pickupToastTimer = 0.0f;
    m_levelIntervalScale = 1.0f;
    m_killIntervalScale = 1.0f;
    m_dropIntervalScale = 1.0f;
    m_attackTimer = 0.0f;
    m_lightningTimer = 0.8f;
    m_frostTimer = 1.8f;
    m_fireballTimer = 2.4f;
    m_meteorTimer = 1.6f;
    m_singularityTimer = 2.2f;
    m_plagueTimer = 2.0f;
    m_prismAngle = 0.0f;
    m_prismTick = 0.0f;
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
            if (!HasSkill(0)) break;
            if (arrowSpreadLevel < kSurvivorsMaxArrowSpreadLevel) {
                ++arrowSpreadLevel;
                ShowPickupToast("箭矢散射 Lv." + std::to_string(arrowSpreadLevel) + "：" +
                                std::to_string(ArrowDirections()) + " 方向");
                PlaySfx("levelup", 0.55f, 1.10f + 0.02f * static_cast<float>(arrowSpreadLevel));
            } else if (arrowDamageLevel < kSurvivorsMaxArrowDamageLevel) {
                ++arrowDamageLevel;
                RecomputeAttackDamage();
                ShowPickupToast("散射已满级：箭矢攻击力提升");
                PlaySfx("pickup", 0.50f, 0.90f);
            }
            break;
        case SurvivorsPickupType::ArrowAttackSpeed:
            if (!HasSkill(0) || arrowSpeedLevel >= kSurvivorsMaxArrowSpeedLevel) break;
            ++arrowSpeedLevel;
            RecomputeAttackInterval();
            ShowPickupToast("箭矢攻速提升 Lv." + std::to_string(arrowSpeedLevel));
            PlaySfx("pickup", 0.55f, 1.20f);
            break;
        case SurvivorsPickupType::ArrowAttackDamage:
            if (!HasSkill(0) || arrowDamageLevel >= kSurvivorsMaxArrowDamageLevel) break;
            ++arrowDamageLevel;
            RecomputeAttackDamage();
            ShowPickupToast("箭矢攻击力提升 Lv." + std::to_string(arrowDamageLevel));
            PlaySfx("pickup", 0.55f, 0.92f);
            break;
        case SurvivorsPickupType::ArrowHealthPotion:
            if (hp < maxHp) {
                hp = (std::min)(maxHp, hp + maxHp * 0.25f + 6.0f);
                ShowPickupToast("获得血瓶：恢复生命");
                PlaySfx("pickup", 0.70f, 0.85f);
            } else {
                AddXp(6.0f);
                ShowPickupToast("血瓶：生命已满，转化为经验");
                PlaySfx("pickup", 0.50f, 1.30f);
            }
            break;
        case SurvivorsPickupType::ArrowRange:
            if (!HasSkill(0) || arrowRangeLevel >= kSurvivorsMaxArrowRangeLevel) break;
            ++arrowRangeLevel;
            ShowPickupToast("箭矢攻击范围提升 Lv." + std::to_string(arrowRangeLevel));
            PlaySfx("pickup", 0.55f, 1.35f);
            break;
        case SurvivorsPickupType::LightningUpgrade:
            if (!HasSkill(1) || lightningLevel >= kSurvivorsMaxSkillLevel) break;
            ++lightningLevel;
            m_lightningTimer = 0.0f;
            ShowPickupToast("雷霆强化 Lv." + std::to_string(lightningLevel));
            PlaySfx("levelup", 0.50f, 1.22f);
            break;
        case SurvivorsPickupType::BladeUpgrade:
            if (!HasSkill(2) || bladeLevel >= kSurvivorsMaxSkillLevel) break;
            ++bladeLevel;
            ShowPickupToast("回旋刃强化 Lv." + std::to_string(bladeLevel));
            PlaySfx("levelup", 0.48f, 1.10f);
            break;
        case SurvivorsPickupType::FrostUpgrade:
            if (!HasSkill(3) || frostLevel >= kSurvivorsMaxSkillLevel) break;
            ++frostLevel;
            m_frostTimer = 0.0f;
            ShowPickupToast("霜环强化 Lv." + std::to_string(frostLevel));
            PlaySfx("levelup", 0.48f, 1.35f);
            break;
        case SurvivorsPickupType::PoisonUpgrade:
            if (!HasSkill(4) || poisonLevel >= kSurvivorsMaxSkillLevel) break;
            ++poisonLevel;
            m_poisonTick = 0.0f;
            ShowPickupToast("毒雾强化 Lv." + std::to_string(poisonLevel));
            PlaySfx("levelup", 0.48f, 0.92f);
            break;
        case SurvivorsPickupType::FireballUpgrade:
            if (!HasSkill(5) || fireballLevel >= kSurvivorsMaxSkillLevel) break;
            ++fireballLevel;
            m_fireballTimer = 0.0f;
            ShowPickupToast("火球强化 Lv." + std::to_string(fireballLevel));
            PlaySfx("levelup", 0.48f, 1.05f);
            break;
        case SurvivorsPickupType::MeteorUpgrade:
            if (!HasSkill(6) || meteorLevel >= kSurvivorsMaxSkillLevel) break;
            ++meteorLevel;
            m_meteorTimer = 0.0f;
            ShowPickupToast("陨石强化 Lv." + std::to_string(meteorLevel));
            PlaySfx("levelup", 0.48f, 0.78f);
            break;
        case SurvivorsPickupType::SingularityUpgrade:
            if (!HasSkill(7) || singularityLevel >= kSurvivorsMaxSkillLevel) break;
            ++singularityLevel;
            m_singularityTimer = 0.0f;
            ShowPickupToast("黑洞强化 Lv." + std::to_string(singularityLevel));
            PlaySfx("levelup", 0.48f, 0.70f);
            break;
        case SurvivorsPickupType::PrismUpgrade:
            if (!HasSkill(8) || prismLevel >= kSurvivorsMaxSkillLevel) break;
            ++prismLevel;
            ShowPickupToast("棱镜光束强化 Lv." + std::to_string(prismLevel));
            PlaySfx("levelup", 0.48f, 1.40f);
            break;
        case SurvivorsPickupType::PlagueUpgrade:
            if (!HasSkill(9) || plagueLevel >= kSurvivorsMaxSkillLevel) break;
            ++plagueLevel;
            m_plagueTimer = 0.0f;
            ShowPickupToast("毒爆强化 Lv." + std::to_string(plagueLevel));
            PlaySfx("levelup", 0.48f, 0.88f);
            break;
        default: break;
    }
    LogInfo("[2D] 拾取强化: %s", GetPickupInfo(type).label);
}

void SurvivorsPlayerComponent::AddXp(float amount) {
    if (amount <= 0.0f) return;
    xp += amount;
    // 升级不再冻结战斗做三选一：经验满立即结算等级与基础成长，
    // 真正的技能强化由击杀掉落、手动拾取完成。
    while (xp >= xpNext) {
        xp -= xpNext;
        xpNext *= 1.30f;
        CompleteLevelUp();
    }
}

void SurvivorsPlayerComponent::ApplyLevelUpUpgrade(int skill, int sub) {
    if (skill >= 0 && skill < kSurvivorsSkillCount && !HasSkill(skill)) return;
    switch (skill) {
        case 0:
            switch (sub) {
                case 0:
                    if (arrowSpreadLevel < kSurvivorsMaxArrowSpreadLevel) {
                        ++arrowSpreadLevel;
                        ShowPickupToast("箭矢散射 Lv." + std::to_string(arrowSpreadLevel) + "：" +
                                        std::to_string(ArrowDirections()) + " 方向");
                    }
                    break;
                case 1:
                    if (arrowSpeedLevel < kSurvivorsMaxArrowSpeedLevel) {
                        ++arrowSpeedLevel;
                        RecomputeAttackInterval();
                    }
                    break;
                case 2:
                    if (arrowDamageLevel < kSurvivorsMaxArrowDamageLevel) {
                        ++arrowDamageLevel;
                        RecomputeAttackDamage();
                    }
                    break;
                case 3:
                    if (arrowRangeLevel < kSurvivorsMaxArrowRangeLevel) ++arrowRangeLevel;
                    break;
                default: break;
            }
            break;
        case 1:
            if (lightningLevel < kSurvivorsMaxSkillLevel) {
                ++lightningLevel;
                m_lightningTimer = 0.0f;
            }
            break;
        case 2:
            if (bladeLevel < kSurvivorsMaxSkillLevel) ++bladeLevel;
            break;
        case 3:
            if (frostLevel < kSurvivorsMaxSkillLevel) {
                ++frostLevel;
                m_frostTimer = 0.0f;
            }
            break;
        case 4:
            if (poisonLevel < kSurvivorsMaxSkillLevel) {
                ++poisonLevel;
                m_poisonTick = 0.0f;
            }
            break;
        case 5:
            if (fireballLevel < kSurvivorsMaxSkillLevel) {
                ++fireballLevel;
                m_fireballTimer = 0.0f;
            }
            break;
        case 6:
            if (meteorLevel < kSurvivorsMaxSkillLevel) {
                ++meteorLevel;
                m_meteorTimer = 0.0f;
            }
            break;
        case 7:
            if (singularityLevel < kSurvivorsMaxSkillLevel) {
                ++singularityLevel;
                m_singularityTimer = 0.0f;
            }
            break;
        case 8:
            if (prismLevel < kSurvivorsMaxSkillLevel) ++prismLevel;
            break;
        case 9:
            if (plagueLevel < kSurvivorsMaxSkillLevel) {
                ++plagueLevel;
                m_plagueTimer = 0.0f;
            }
            break;
        case 100: // 满级后的额外补给（旧预览页用）
            if (sub == 0) {
                hp = (std::min)(maxHp, hp + maxHp * 0.30f + 10.0f);
            } else if (sub == 1) {
                maxHp += 8.0f;
                hp = (std::min)(maxHp, hp + 8.0f);
            } else {
                hp = maxHp;
            }
            break;
        default: break;
    }
}

void SurvivorsPlayerComponent::CompleteLevelUp() {
    if (pendingLevelUps > 0) --pendingLevelUps;
    ++level;
    maxHp += 6.0f;
    hp = (std::min)(maxHp, hp + 6.0f);
    RefreshDerivedStats();
    PlaySfx("levelup", 0.48f, 1.12f);
}

// ================================================================ 玩家：更新
void SurvivorsPlayerComponent::OnUpdate(float dt) {
    if (!alive || !m_owner) return;

    invulnerable = (std::max)(0.0f, invulnerable - dt);
    hitFlash = (std::max)(0.0f, hitFlash - dt * 3.0f);
    pickupToastTimer = (std::max)(0.0f, pickupToastTimer - dt);

    Vec2 move(0.0f, 0.0f);
    if (g_autopilot) {
        World* world = GetWorld();
        Vec2   target;
        bool   hasTarget = false;
        if (world) {
            const Vec2 origin(m_owner->Position().x, m_owner->Position().y);
            float      best = 1e9f;
            for (GameObject* chest : g_buffChests) {
                if (!chest || !chest->Active()) continue;
                const Vec2  p(chest->Position().x, chest->Position().y);
                const float d = Length(p - origin);
                if (d < best) { best = d; target = p; hasTarget = true; }
            }
            if (!hasTarget) {
                for (GameObject* pickup : g_pickups) {
                    if (!pickup || !pickup->Active()) continue;
                    auto* pickupComponent = pickup->GetComponent<SurvivorsPickupComponent>();
                    if (!pickupComponent || !IsSkillUnlockType(pickupComponent->type)) continue;
                    const Vec2  p(pickup->Position().x, pickup->Position().y);
                    const float d = Length(p - origin);
                    if (d < best) { best = d; target = p; hasTarget = true; }
                }
            }
            if (hasTarget) {
                move = target - origin;
            } else {
                // 没有目标时绕圈，避免完全站桩。
                const float angle = g_gameTime * 0.7f;
                move = Vec2(std::cos(angle), std::sin(angle));
            }
        }
    } else if (g_inputEnabled) {
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
        Vec2       next = MoveWithObstacles(oldPosition, direction * speed * buffMoveMultiplier * dt, 0.45f);
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
    if (HasSkill(0)) {
        m_attackTimer -= dt * buffAttackSpeedMultiplier;
        if (m_attackTimer <= 0.0f) {
            FireAtNearestEnemy(Vec2(m_owner->Position().x, m_owner->Position().y));
            m_attackTimer = attackInterval;
        }
    }

    UpdateSkills(dt * buffAttackSpeedMultiplier);

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
    const int directions = ArrowDirections();
    GameObject* nearest = FindNearestEnemy(origin, ArrowRange());
    const Vec2 aim = nearest ? SafeDir(Vec2(nearest->Position().x, nearest->Position().y) - origin)
                             : SafeDir(facing);
    if (directions > 1) {
        // 散射：方向数按 2/4/6/8/12 逐级成长；整圈均匀铺开，并让其中一箭对准最近目标。
        const float baseAngle = std::atan2(aim.y, aim.x);
        for (int i = 0; i < directions; ++i) {
            const float angle = baseAngle + static_cast<float>(i) * kTwoPi / static_cast<float>(directions);
            FireArrow(origin, Vec2(std::cos(angle), std::sin(angle)), 0.65f);
        }
    } else {
        FireArrow(origin, aim, 1.0f);
    }
    PlaySfx("shoot", 0.28f, 0.90f + Random01() * 0.20f);
}

void SurvivorsPlayerComponent::UpdateSkills(float dt) {
    World* world = GetWorld();
    if (!world) return;

    if (HasSkill(1)) {
        m_lightningTimer -= dt;
        if (m_lightningTimer <= 0.0f) {
            m_lightningTimer = CastLightning() ? LightningCooldown() : 0.35f;
        }
    }

    if (HasSkill(3)) {
        m_frostTimer -= dt;
        if (m_frostTimer <= 0.0f) {
            CastFrost();
            m_frostTimer = FrostCooldown();
        }
    }

    if (HasSkill(5)) {
        m_fireballTimer -= dt;
        if (m_fireballTimer <= 0.0f) {
            CastFireball();
            m_fireballTimer = FireballCooldown();
        }
    }

    if (HasSkill(6)) {
        m_meteorTimer -= dt;
        if (m_meteorTimer <= 0.0f) {
            CastMeteor();
            m_meteorTimer = MeteorCooldown();
        }
    }

    if (HasSkill(7)) {
        m_singularityTimer -= dt;
        if (m_singularityTimer <= 0.0f) {
            CastSingularity();
            m_singularityTimer = SingularityCooldown();
        }
    }

    if (HasSkill(8)) UpdatePrismBeams(dt);

    if (HasSkill(9)) {
        m_plagueTimer -= dt;
        if (m_plagueTimer <= 0.0f) {
            CastPlagueBloom();
            m_plagueTimer = PlagueCooldown();
        }
    }

    if (HasSkill(2)) UpdateBlades(dt);
    if (HasSkill(4)) UpdateAura(dt);
    UpdateFireballs(*world, dt);
    UpdateMeteors(*world, dt);
    UpdateSingularities(*world, dt);
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
            // 冰环不再只是减速：把范围内敌人冻结在原地，和回旋刃形成联动。
            component->slowTimer = FrostFreezeDuration();
            component->slowFactor = 0.0f;
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

void SurvivorsPlayerComponent::CastMeteor() {
    World* world = GetWorld();
    if (!world || !m_owner) return;
    const Vec2 origin(m_owner->Position().x, m_owner->Position().y);
    std::vector<GameObject*> candidates;
    GatherEnemiesInRadius(origin, 14.0f, candidates);
    if (candidates.empty()) return;
    for (size_t i = candidates.size(); i > 1; --i) {
        const size_t j = static_cast<size_t>(Random01() * static_cast<float>(i)) % i;
        std::swap(candidates[i - 1], candidates[j]);
    }
    const int count = (std::min)(MeteorCount(), static_cast<int>(candidates.size()));
    for (int i = 0; i < count; ++i) {
        MeteorFx meteor;
        meteor.target = Vec2(candidates[static_cast<size_t>(i)]->Position().x,
                             candidates[static_cast<size_t>(i)]->Position().y);
        meteor.delay = 0.32f + 0.08f * static_cast<float>(i);
        meteor.damage = MeteorDamage();
        meteor.radius = MeteorRadius();
        g_meteors.push_back(meteor);
    }
    PlaySfx("fireball", 0.45f, 0.72f + Random01() * 0.1f);
}

void SurvivorsPlayerComponent::CastSingularity() {
    World* world = GetWorld();
    if (!world || !m_owner) return;
    const Vec2 origin(m_owner->Position().x, m_owner->Position().y);
    Vec2 center = origin + SafeDir(facing) * 4.0f;
    if (GameObject* nearest = FindNearestEnemy(origin, 12.0f)) {
        center = Vec2(nearest->Position().x, nearest->Position().y);
    }
    SingularityFx hole;
    hole.center = center;
    hole.life = SingularityDuration();
    hole.radius = SingularityRadius();
    hole.dps = SingularityDps();
    hole.tick = 0.0f;
    g_singularities.push_back(hole);
    PlaySfx("magic", 0.5f, 0.75f + Random01() * 0.1f);
}

void SurvivorsPlayerComponent::UpdatePrismBeams(float dt) {
    m_prismAngle += PrismBeamSpeed() * dt;
    m_prismTick -= dt;
    if (m_prismTick > 0.0f) return;
    m_prismTick = 0.22f;
    if (!m_owner) return;
    const Vec2  origin(m_owner->Position().x, m_owner->Position().y);
    const int   count = PrismBeamCount();
    const float length = PrismBeamLength();
    bool        hitAny = false;
    for (int i = 0; i < count; ++i) {
        const float angle = m_prismAngle + static_cast<float>(i) * kTwoPi / static_cast<float>((std::max)(1, count));
        const Vec2  direction(std::cos(angle), std::sin(angle));
        for (GameObject* enemy : g_enemies) {
            if (!enemy || !enemy->Active()) continue;
            const Vec2  relative(enemy->Position().x - origin.x, enemy->Position().y - origin.y);
            const float along = Dot(relative, direction);
            if (along <= 0.0f || along > length) continue;
            const float cross = std::fabs(relative.x * direction.y - relative.y * direction.x);
            if (cross > 0.45f) continue;
            auto* component = enemy->GetComponent<SurvivorsEnemyComponent>();
            const bool frozen = component && component->slowTimer > 0.0f && component->slowFactor < 0.05f;
            DamageEnemy(enemy, PrismDamage() * (frozen ? 1.25f : 1.0f));
            hitAny = true;
        }
    }
    if (hitAny) PlaySfx("magic", 0.16f, 1.35f + Random01() * 0.1f);
}

void SurvivorsPlayerComponent::CastPlagueBloom() {
    World* world = GetWorld();
    if (!world || !m_owner) return;
    const Vec2 origin(m_owner->Position().x, m_owner->Position().y);
    std::vector<GameObject*> candidates;
    GatherEnemiesInRadius(origin, 9.0f, candidates);
    if (candidates.empty()) return;
    std::sort(candidates.begin(), candidates.end(), [](GameObject* a, GameObject* b) {
        const auto* ca = a->GetComponent<SurvivorsEnemyComponent>();
        const auto* cb = b->GetComponent<SurvivorsEnemyComponent>();
        return ca && cb && ca->poisonTimer > cb->poisonTimer;
    });
    const int count = (std::min)(4, static_cast<int>(candidates.size()));
    for (int i = 0; i < count; ++i) {
        const Vec2 position(candidates[static_cast<size_t>(i)]->Position().x,
                            candidates[static_cast<size_t>(i)]->Position().y);
        SpawnExplosion(position, PlagueRadius(), Vec4(0.45f, 1.0f, 0.35f, 1.0f));
        std::vector<GameObject*> nearby;
        GatherEnemiesInRadius(position, PlagueRadius(), nearby);
        for (GameObject* enemy : nearby) {
            DamageEnemy(enemy, PlagueDamage());
            if (auto* component = enemy->GetComponent<SurvivorsEnemyComponent>()) {
                component->poisonTimer = (std::max)(component->poisonTimer, 1.2f);
            }
        }
    }
    PlaySfx("poison", 0.35f, 0.85f + Random01() * 0.1f);
}

void SurvivorsPlayerComponent::UpdateBlades(float dt) {
    if (!m_owner) return;
    m_bladeAngle += BladeSpeed() * dt;
    m_bladeTick -= dt;
    if (m_bladeTick > 0.0f) return;

    const Vec2 center(m_owner->Position().x, m_owner->Position().y);
    const int  bladeCount = BladeCount();
    const float hitRadius = BladeSize() + 0.35f; // 命中范围跟飞剑视觉一起放大
    bool       hitAny = false;
    for (int i = 0; i < bladeCount; ++i) {
        const float angle = m_bladeAngle + static_cast<float>(i) * kTwoPi / static_cast<float>(bladeCount);
        const Vec2  blade = center + Vec2(std::cos(angle), std::sin(angle)) * BladeRadius();
        for (GameObject* enemy : g_enemies) {
            if (!enemy || !enemy->Active()) continue;
            const Vec2 position(enemy->Position().x, enemy->Position().y);
            if (Length(position - blade) < hitRadius) {
                // 冰环联动：被冻结的目标受到回旋刃 1.75 倍伤害。
                auto* enemyComponent = enemy->GetComponent<SurvivorsEnemyComponent>();
                const bool frozen = enemyComponent && enemyComponent->slowTimer > 0.0f &&
                                    enemyComponent->slowFactor < 0.05f;
                DamageEnemy(enemy, BladeDamage() * (frozen ? 1.75f : 1.0f));
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
    // 毒雾粒子：沿光环范围随机冒出的绿色烟雾，让持续伤害有可见来源。
    for (int i = 0; i < 2 && g_poisonFx.size() < 96; ++i) {
        const float angle = FxRandom01() * kTwoPi;
        const float radius = PoisonRadius() * (0.25f + FxRandom01() * 0.7f);
        PoisonPuffFx puff;
        puff.position = origin + Vec2(std::cos(angle), std::sin(angle)) * radius;
        puff.velocity = Vec2((FxRandom01() - 0.5f) * 0.5f, 0.45f + FxRandom01() * 0.45f);
        puff.life = 0.8f + FxRandom01() * 0.6f;
        puff.size = 0.55f + FxRandom01() * 0.55f;
        g_poisonFx.push_back(puff);
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

    auto*     sprite = m_owner->GetComponent<SpriteRendererComponent>();
    const float stopDistance = 0.62f + contactRadius * 0.25f;
    const bool  frozen = slowTimer > 0.0f && slowFactor < 0.05f;
    Vec2        moved(0.0f, 0.0f);
    if (!frozen && distance > stopDistance) {
        const Vec2  direction = SafeDir(toTarget);
        const float slow = slowTimer > 0.0f ? slowFactor : 1.0f;

        // 挤开同伴时只保留“切向”分量：怪群会横向铺开，而不是互相把对方顶离玩家。
        Vec2 separation(0.0f, 0.0f);
        for (GameObject* other : g_enemies) {
            if (!other || other == m_owner || !other->Active()) continue;
            const Vec2  otherPosition(other->Position().x, other->Position().y);
            const Vec2  offset = position - otherPosition;
            const float otherDistance = Length(offset);
            if (otherDistance > 0.001f && otherDistance < 0.95f) {
                separation += offset / otherDistance * (0.95f - otherDistance);
            }
        }
        Vec2 moveDir = direction;
        if (Length(separation) > 1e-4f) {
            const Vec2 lateral = separation - direction * Dot(separation, direction);
            moveDir = direction + lateral * 1.15f;
        }
        moveDir = SafeDir(moveDir);

        const Vec2 step = moveDir * speed * slow * dt;
        Vec2       next(position.x, position.y);
        if (type == static_cast<int>(SurvivorsEnemyType::Ghost)) {
            next = position + step; // 幽灵穿障碍
        } else {
            if (m_strafeSign == 0.0f) m_strafeSign = Random01() < 0.5f ? -1.0f : 1.0f;
            next = MoveEnemyWithSteering(position, moveDir, Length(step), contactRadius, m_strafeSign);
        }
        next.x = Clamp(next.x, -playerComponent->arenaHalf.x, playerComponent->arenaHalf.x);
        next.y = Clamp(next.y, -playerComponent->arenaHalf.y, playerComponent->arenaHalf.y);
        moved = next - position;
        Vec3 nextPosition = m_owner->Position();
        nextPosition.x = next.x;
        nextPosition.y = next.y;
        m_owner->SetPosition(nextPosition);
    } else if (!frozen) {
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

    // 朝向按实际移动方向翻转，而不是只看向量左右，绕障碍时也能面向玩家。
    // 兽人贴图是面向镜头的正视图（脸略朝画面右侧）：向右走保持原图，向左走才翻转。
    // 之前条件写反了，向右追击玩家时会翻成背对玩家，看起来像“屁股对人倒着走”。
    if (sprite && Length(moved) > 0.001f && std::abs(moved.x) > 1e-4f) {
        sprite->flipX = moved.x < 0.0f;
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
    const bool       skillUnlock = IsSkillUnlockType(type);
    const PickupInfo info = GetPickupInfo(type);
    if (auto* sprite = m_owner->GetComponent<SpriteRendererComponent>()) {
        Vec4 color = info.color;
        color.w = 0.78f + 0.22f * (0.5f + 0.5f * std::sin(age * (skillUnlock ? 7.0f : 5.0f)));
        sprite->color = color;
        sprite->offset = Vec2(0.0f, std::sin(age * 4.0f) * (skillUnlock ? 0.18f : 0.12f));
        sprite->rotation = std::sin(age * 2.0f) * (skillUnlock ? 0.25f : 0.15f);
    }

    // 新技能是“临时选择”：地上没被捡走的技能掉落 8 秒后消失，避免满地残留。
    // 强化/血瓶仍然保留 60 秒。
    if (age > (skillUnlock ? 8.0f : 60.0f)) {
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

    // 新技能必须手动走过去捡：既不进入磁吸范围，也不会随时间自动飞向玩家。
    if (!skillUnlock && (age > 3.0f || distance < magnetRange)) {
        const Vec2  direction = SafeDir(toTarget);
        Vec3        next = m_owner->Position();
        const float speed = 4.5f + age * 0.5f;
        next.x += direction.x * speed * dt;
        next.y += direction.y * speed * dt;
        m_owner->SetPosition(next);
    }

    if (distance < (skillUnlock ? 0.75f : 0.55f)) {
        if (skillUnlock) {
            playerComponent->UnlockSkill(SkillFromUnlockType(type));
        } else {
            playerComponent->ApplyPickup(type);
        }
        world->DestroyObject(m_owner);
    }
}

void SurvivorsPickupComponent::OnDestroy() {
    if (!m_owner) return;
    g_pickups.erase(std::remove(g_pickups.begin(), g_pickups.end(), m_owner), g_pickups.end());
}

// ================================================================ 地图抽奖宝箱
void SurvivorsBuffComponent::OnUpdate(float dt) {
    if (!m_owner) return;
    age += dt;
    World* world = GetWorld();
    if (!world) return;

    if (auto* sprite = m_owner->GetComponent<SpriteRendererComponent>()) {
        // 图标本身保持原色，稀有度由世界空间的彩色光环表达，避免把宝箱整体染成色块。
        sprite->color = Vec4(1.0f, 1.0f, 1.0f, 0.86f + 0.14f * (0.5f + 0.5f * std::sin(age * 5.0f)));
        sprite->offset = Vec2(0.0f, std::sin(age * 3.4f) * 0.12f);
    }

    if (age >= life) {
        world->DestroyObject(m_owner);
        return;
    }

    GameObject* player = FindPlayer(*world);
    if (!player) return;
    const Vec2  position(m_owner->Position().x, m_owner->Position().y);
    const Vec2  target(player->Position().x, player->Position().y);
    if (Length(target - position) < 1.3f) {
        if (auto* component = player->GetComponent<SurvivorsPlayerComponent>()) {
            // 先结算增益并拿到效果文本，由导演在本帧稍后冻结世界并播放抽奖动画。
            g_pendingBuffGacha.active = true;
            g_pendingBuffGacha.rarity = Clamp(rarity, 0, 2);
            g_pendingBuffGacha.effect = component->ApplyBuff(rarity);
            world->DestroyObject(m_owner);
        }
    }
}

void SurvivorsBuffComponent::OnDestroy() {
    if (!m_owner) return;
    g_buffChests.erase(std::remove(g_buffChests.begin(), g_buffChests.end(), m_owner), g_buffChests.end());
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
    spawn.x = Clamp(spawn.x, -kSurvivorsSpawnHalfX, kSurvivorsSpawnHalfX);
    spawn.y = Clamp(spawn.y, -kSurvivorsSpawnHalfY, kSurvivorsSpawnHalfY);

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

namespace {

// 怪物强度曲线：0–9 分钟按原二次曲线，9 分钟后生命/速度/伤害额外加速，制造终局压力。
float EnemyHpScale(float time) {
    float scale = 1.0f + time / 55.0f + std::pow(time / 150.0f, 2.0f);
    if (time > 540.0f) scale += 6.0f * std::pow((time - 540.0f) / 60.0f, 1.6f);
    return scale;
}

float EnemySpeedScale(float time) {
    float scale = 1.0f + (std::min)(0.8f, time / 420.0f);
    if (time > 540.0f) scale += (std::min)(0.8f, (time - 540.0f) / 300.0f);
    return scale;
}

float EnemyDamageScale(float time) {
    float scale = 1.0f + time / 110.0f;
    if (time > 540.0f) scale += (std::min)(1.0f, (time - 540.0f) / 240.0f);
    return scale;
}

// 出怪节奏：前期少量、中期加密、6–9 分钟满屏、9 分钟后暴增。
int WaveCount(float time) {
    if (time < 180.0f) return 3 + static_cast<int>(time / 90.0f);
    if (time < 360.0f) return 6 + static_cast<int>((time - 180.0f) / 60.0f);
    if (time < 540.0f) return 9 + static_cast<int>((time - 360.0f) / 45.0f);
    return 13 + (std::min)(17, static_cast<int>((time - 540.0f) / 25.0f));
}

float WaveInterval(float time) {
    if (time < 180.0f) return 2.0f - time / 180.0f * 0.5f;
    if (time < 360.0f) return 1.5f - (time - 180.0f) / 180.0f * 0.4f;
    if (time < 540.0f) return 1.1f - (time - 360.0f) / 180.0f * 0.3f;
    return (std::max)(0.25f, 0.9f - (time - 540.0f) / 300.0f * 0.6f);
}

} // namespace

// ================================================================ 增益宝箱展示数据
Vec4 SurvivorsBuffRarityColor(int rarity) {
    static const Vec4 kColors[3] = {Vec4(0.70f, 0.92f, 1.00f, 1.0f), Vec4(1.00f, 0.82f, 0.30f, 1.0f),
                                    Vec4(1.00f, 0.55f, 1.00f, 1.0f)};
    return kColors[Clamp(rarity, 0, 2)];
}

const char* SurvivorsBuffRarityName(int rarity) {
    static const char* kNames[3] = {"普通", "稀有", "史诗"};
    return kNames[Clamp(rarity, 0, 2)];
}

int SurvivorsBuffIconForEffect(const std::string& effect) {
    if (effect.find("攻击力") != std::string::npos) return 2; // 回旋刃
    if (effect.find("攻击速度") != std::string::npos) return 1; // 雷霆
    if (effect.find("移动速度") != std::string::npos) return 0; // 箭矢
    if (effect.find("最大生命") != std::string::npos || effect.find("生命恢复") != std::string::npos ||
        effect.find("技能已满") != std::string::npos) {
        return -1; // 生命图标
    }
    if (effect.find("技能范围") != std::string::npos) return 8;  // 棱镜
    if (effect.find("全屏雷击") != std::string::npos) return 1;  // 雷霆
    if (effect.find("随机技能") != std::string::npos) return 1;   // 雷霆
    return 1;
}

void SurvivorsDirector::SpawnWave(World& world) {
    const size_t enemyCap = m_time > 540.0f ? 320 : 220;
    if (g_enemies.size() > enemyCap) return; // 上限，避免刷爆
    GameObject* player = FindPlayer(world);
    const Vec2  center = player ? Vec2(player->Position().x, player->Position().y) : Vec2(0.0f, 0.0f);

    const int   count = WaveCount(m_time);
    const float hpScale = EnemyHpScale(m_time);
    const float speedScale = EnemySpeedScale(m_time);
    const float damageScale = EnemyDamageScale(m_time);

    for (int i = 0; i < count; ++i) {
        const float angle = Random01() * kTwoPi;
        const float radius = RandomRange(14.0f, 20.0f);
        const Vec2  position(center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius);
        SpawnEnemy(world, position, PickEnemyType(m_time), hpScale, speedScale, damageScale);
    }
}

void SurvivorsDirector::SpawnBuffChest(World& world) {
    if (g_buffChests.size() >= 2) return;
    GameObject* player = FindPlayer(world);
    if (!player) return;
    const Vec2 origin(player->Position().x, player->Position().y);
    Vec2       spawn = origin + Vec2(10.0f, 0.0f);
    for (int attempt = 0; attempt < 20; ++attempt) {
        const float angle = Random01() * kTwoPi;
        const float distance = RandomRange(12.0f, 20.0f);
        const Vec2  candidate = origin + Vec2(std::cos(angle), std::sin(angle)) * distance;
        if (std::fabs(candidate.x) <= kSurvivorsSpawnHalfX && std::fabs(candidate.y) <= kSurvivorsSpawnHalfY &&
            !CircleHitsObstacle(candidate, 0.7f)) {
            spawn = candidate;
            break;
        }
    }

    const float roll = Random01();
    const int   rarity = roll < 0.10f ? 2 : (roll < 0.40f ? 1 : 0);
    // 地图上用 Kenney Tiny Dungeon 的宝箱图标（第 7 行第 7 列），抽奖时再切到开启序列帧。
    GameObject* chest = SpawnSprite(world, "增益宝箱", "sprites/kenney_tilemap.png", spawn, Vec2(1.15f, 1.15f), 4);
    if (auto* sprite = chest->GetComponent<SpriteRendererComponent>()) {
        ApplyTileSprite(sprite, 7, 7);
        sprite->color = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    if (auto* component = chest->AddComponent<SurvivorsBuffComponent>()) component->rarity = rarity;
    g_buffChests.push_back(chest);
}

void SurvivorsDirector::SpawnElite(World& world) {
    GameObject* player = FindPlayer(world);
    const Vec2  center = player ? Vec2(player->Position().x, player->Position().y) : Vec2(0.0f, 0.0f);
    const float angle = Random01() * kTwoPi;
    const Vec2  position(center.x + std::cos(angle) * 12.0f, center.y + std::sin(angle) * 12.0f);

    const float hpScale = EnemyHpScale(m_time);
    const float speedScale = EnemySpeedScale(m_time);
    const float damageScale = EnemyDamageScale(m_time);
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
    const std::vector<GameObject*> buffsToDestroy = g_buffChests;
    g_buffChests.clear();
    for (GameObject* chest : buffsToDestroy) {
        if (chest) world.DestroyObject(chest);
    }
    g_pendingBuffGacha = PendingBuffGacha();
    ClearSkillEffects();
    g_killsSinceDrop = 0;
    g_killsSinceSkillDrop = 0;
    g_killsSincePotionDrop = 0;
    g_gameTime = 0.0f;
    g_lastSkillDropTime = 0.0f;
    g_autopilot = false;
    g_kills = 0;
    g_lastHitSfxTime = -10.0;
    g_lastGemSfxTime = -10.0;

    if (GameObject* player = FindPlayer(world)) {
        player->SetPosition(Vec3(0.0f, 0.0f, 0.0f));
        if (auto* component = player->GetComponent<SurvivorsPlayerComponent>()) {
            component->playerClass = Clamp(m_playerClass, 0, kSurvivorsClassCount - 1);
            component->ResetCombatStats();
        }
    }

    g_rng.seed(0x12345u);
    g_fxRng.seed(0x271828u);
    m_state = State::Playing;
    m_kills = 0;
    m_time = 0.0f;
    m_difficulty = 0.0f;
    m_spawnTimer = 1.0f;
    m_eliteTimer = 45.0f;
    m_skillCatchUpTimer = 0.0f;
    m_buffTimer = 18.0f;
    m_showSettings = false;
    m_settingsReturn = State::Menu;
    m_selectedSkill = -1;
    m_pauseNotice.clear();
    m_confirmExit = false;
    m_toastTimer = 0.0f;
    m_toastText.clear();
    ClearChestGacha();
    LogInfo("[2D] 开始游戏");
    ShowToast("猎杀开始：击杀怪物拾取强化；Esc 暂停");
}

// ================================================================ 导演：增益宝箱抽奖
bool SurvivorsDirector::BeginChestGacha(World& world, int rarity, const std::string& effect) {
    if (m_state != State::Playing) return false;
    GameObject* playerObject = FindPlayer(world);
    auto*       player = playerObject ? playerObject->GetComponent<SurvivorsPlayerComponent>() : nullptr;
    if (!player || player->hp <= 0.0f) return false;

    m_gachaRarity = Clamp(rarity, 0, 2);
    m_gachaEffect = effect;
    m_gachaTime = 0.0f;
    m_gachaPreview = false;
    m_gachaSkipRequested = false;
    m_gachaBurstPlayed = false;
    m_gachaNextTick = 0;
    m_state = State::Gacha;
    PlaySfx("levelup", 0.42f, 1.22f);
    LogInfo("[2D] 开启增益宝箱抽奖：稀有度 %d / 效果 %s", m_gachaRarity, m_gachaEffect.c_str());
    return true;
}

void SurvivorsDirector::UpdateChestGacha(World& world, float dt) {
    if (m_gachaPreview) return;
    if (m_gachaSkipRequested) {
        FinishChestGacha(world);
        return;
    }

    m_gachaTime += dt;

    static const float kTickTimes[3] = {0.30f, 0.56f, 0.82f};
    while (m_gachaNextTick < 3 && m_gachaTime >= kTickTimes[m_gachaNextTick]) {
        PlaySfx("pickup", 0.18f, 0.82f + 0.12f * static_cast<float>(m_gachaNextTick));
        ++m_gachaNextTick;
    }
    if (!m_gachaBurstPlayed && m_gachaTime >= kSurvivorsGachaBurst) {
        m_gachaBurstPlayed = true;
        PlaySfx("magic", 0.65f, 0.92f);
        PlaySfx("levelup", 0.50f, 1.35f);
    }
    if (m_gachaTime >= kSurvivorsGachaDuration) FinishChestGacha(world);
}

void SurvivorsDirector::FinishChestGacha(World& world) {
    if (m_state != State::Gacha) return;
    if (!m_gachaPreview) {
        if (GameObject* playerObject = FindPlayer(world)) {
            if (auto* player = playerObject->GetComponent<SurvivorsPlayerComponent>()) {
                // 增益已在拾取时结算，抽奖只负责展示；关掉宝箱时给 0.8 秒无敌。
                player->invulnerable = (std::max)(player->invulnerable, 0.8f);
            }
        }
    }
    m_state = State::Playing;
    m_gachaTime = kSurvivorsGachaDuration;
    m_gachaSkipRequested = false;
    m_gachaBurstPlayed = false;
    LogInfo("[2D] 抽奖完成：%s增益 / %s", SurvivorsBuffRarityName(m_gachaRarity), m_gachaEffect.c_str());
}

void SurvivorsDirector::ClearChestGacha() {
    g_pendingBuffGacha = PendingBuffGacha();
    m_gachaTime = 0.0f;
    m_gachaRarity = 0;
    m_gachaEffect.clear();
    m_gachaPreview = false;
    m_gachaSkipRequested = false;
    m_gachaBurstPlayed = false;
    m_gachaNextTick = 0;
    if (m_state == State::Gacha) m_state = State::Playing;
}

void SurvivorsDirector::Update(World& world, float dt) {
    if (m_state == State::Gacha) {
        UpdateChestGacha(world, dt);
        return;
    }
    if (m_state != State::Playing) return;

    // 增益宝箱在本帧世界更新里刚被拾取：立刻进入抽奖，世界从下一帧开始冻结。
    if (g_pendingBuffGacha.active) {
        const PendingBuffGacha pending = g_pendingBuffGacha;
        g_pendingBuffGacha = PendingBuffGacha();
        if (BeginChestGacha(world, pending.rarity, pending.effect)) return;
    }

    m_time += dt;
    g_gameTime = m_time;
    m_difficulty = m_time / 60.0f;
    m_kills = g_kills;
    if (m_time - m_lastBalanceLog >= 60.0f) {
        m_lastBalanceLog = m_time;
        GameObject* logPlayer = FindPlayer(world);
        auto*       logComponent = logPlayer ? logPlayer->GetComponent<SurvivorsPlayerComponent>() : nullptr;
        if (logComponent) {
            std::string levels;
            for (int skill = 0; skill < kSurvivorsSkillCount; ++skill) {
                if (!logComponent->HasSkill(skill)) continue;
                if (!levels.empty()) levels += " ";
                levels += std::to_string(skill) + ":" + std::to_string(logComponent->SkillLevel(skill));
            }
            LogInfo("[平衡] t=%.0f 击杀=%d 等级=%d 技能=%s 宝箱=%zu 生命=%.0f/%.0f", m_time, m_kills,
                    logComponent->level, levels.c_str(), g_buffChests.size(), logComponent->hp, logComponent->maxHp);
        }
    }
    UpdateDamageNumbers(dt);
    UpdateSkillEffects(dt);
    m_toastTimer = (std::max)(0.0f, m_toastTimer - dt);

    if (GameObject* player = FindPlayer(world)) {
        if (auto* component = player->GetComponent<SurvivorsPlayerComponent>()) component->ApplyKillCount(m_kills);
    }

    // 新手期兜底：2:30 后如果还没集齐技能，每 2 秒在玩家附近补掉一个缺失技能（仍需手动拾取）。
    if (m_time >= 150.0f) {
        GameObject* catchUpPlayer = FindPlayer(world);
        auto*       catchUpComponent =
            catchUpPlayer ? catchUpPlayer->GetComponent<SurvivorsPlayerComponent>() : nullptr;
        if (catchUpComponent && catchUpComponent->SelectedSkillCount() < kSurvivorsMaxLoadout) {
            m_skillCatchUpTimer -= dt;
            if (m_skillCatchUpTimer <= 0.0f &&
                SpawnSkillUnlockNearPlayer(world, *catchUpPlayer, *catchUpComponent, 3.0f)) {
                m_skillCatchUpTimer = 2.0f;
            }
        }
    }

    // 新技能时间保底：最多 60 秒没有新技能掉落，就在玩家附近补一个。
    // 不要求击杀，避免被怪追赶时长时间没有任何新技能掉落；仍然需要手动拾取。
    if (m_time - g_lastSkillDropTime >= kSkillDropPitySeconds) {
        GameObject* pityPlayer = FindPlayer(world);
        auto*       pityComponent = pityPlayer ? pityPlayer->GetComponent<SurvivorsPlayerComponent>() : nullptr;
        if (pityComponent && pityComponent->SelectedSkillCount() < kSurvivorsMaxLoadout) {
            if (SpawnSkillUnlockNearPlayer(world, *pityPlayer, *pityComponent, 3.0f)) {
                LogInfo("[2D] 技能掉落时间保底：%.1f 秒未掉新技能，补一个", m_time);
            }
        }
    }

    m_buffTimer -= dt;
    if (m_buffTimer <= 0.0f) {
        SpawnBuffChest(world);
        m_buffTimer = m_time > 540.0f ? (30.0f + Random01() * 8.0f) : (35.0f + Random01() * 10.0f);
    }

    m_spawnTimer -= dt;
    if (m_spawnTimer <= 0.0f) {
        SpawnWave(world);
        m_spawnTimer = WaveInterval(m_time);
    }

    m_eliteTimer -= dt;
    if (m_eliteTimer <= 0.0f) {
        SpawnElite(world);
        m_eliteTimer = (std::max)(20.0f, 55.0f - m_time * 0.03f - (m_time > 540.0f ? (m_time - 540.0f) * 0.05f : 0.0f));
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
    // 设置页优先响应 Esc：只关设置，回到打开它的页面；从暂停进入时保持暂停。
    if (m_showSettings) {
        m_showSettings = false;
        PlaySfx("click", 0.6f);
        return;
    }
    if (m_state == State::Prepare || m_state == State::Codex) {
        m_state = State::Menu;
        PlaySfx("click", 0.6f);
        return;
    }
    if (m_state == State::Gacha) {
        // 翻牌后允许 Esc 快速收起；抖动/光爆阶段不打断，避免误触跳过。
        if (m_gachaTime >= kSurvivorsGachaReveal) m_gachaSkipRequested = true;
        return;
    }
    if (m_state == State::LevelUp) return; // 必须先完成三选一
    if (m_state == State::Playing) {
        m_state = State::Paused;
        m_pauseNotice.clear();
        m_confirmExit = false;
        PlaySfx("click", 0.6f);
    } else if (m_state == State::Paused) {
        m_state = State::Playing;
        m_pauseNotice.clear();
        m_confirmExit = false;
        PlaySfx("click", 0.6f);
    }
}

// ================================================================ 导演：存档
bool SurvivorsDirector::HasSave() const { return FileSystem::Exists(SavePath()); }

void SurvivorsDirector::ShowToast(const std::string& text) {
    m_toastText = text;
    m_toastTimer = 2.4f;
}

bool SurvivorsDirector::SaveGame(World& world) {
    GameObject* playerObject = FindPlayer(world);
    auto*       player = playerObject ? playerObject->GetComponent<SurvivorsPlayerComponent>() : nullptr;
    if (!player || !playerObject) {
        ShowToast("保存失败：找不到玩家");
        m_pauseNotice = "保存失败：找不到玩家";
        return false;
    }

    JsonValue root = JsonValue::MakeObject();
    root.Set("version", 3);
    root.Set("kind", "survivors-2d");

    JsonValue playerJson = JsonValue::MakeObject();
    const Vec3 position = playerObject->Position();
    playerJson.Set("positionX", position.x);
    playerJson.Set("positionY", position.y);
    playerJson.Set("playerClass", player->playerClass);
    playerJson.Set("loadoutMask", player->loadoutMask);
    playerJson.Set("hp", player->hp);
    playerJson.Set("maxHp", player->maxHp);
    playerJson.Set("level", player->level);
    playerJson.Set("xp", player->xp);
    playerJson.Set("xpNext", player->xpNext);
    playerJson.Set("pendingLevelUps", player->pendingLevelUps);
    playerJson.Set("killCount", player->killCount);
    playerJson.Set("arrowSpreadLevel", player->arrowSpreadLevel);
    playerJson.Set("arrowSpeedLevel", player->arrowSpeedLevel);
    playerJson.Set("arrowDamageLevel", player->arrowDamageLevel);
    playerJson.Set("arrowRangeLevel", player->arrowRangeLevel);
    playerJson.Set("lightningLevel", player->lightningLevel);
    playerJson.Set("bladeLevel", player->bladeLevel);
    playerJson.Set("frostLevel", player->frostLevel);
    playerJson.Set("poisonLevel", player->poisonLevel);
    playerJson.Set("fireballLevel", player->fireballLevel);
    playerJson.Set("meteorLevel", player->meteorLevel);
    playerJson.Set("singularityLevel", player->singularityLevel);
    playerJson.Set("prismLevel", player->prismLevel);
    playerJson.Set("plagueLevel", player->plagueLevel);
    playerJson.Set("buffDamageMultiplier", player->buffDamageMultiplier);
    playerJson.Set("buffAttackSpeedMultiplier", player->buffAttackSpeedMultiplier);
    playerJson.Set("buffMoveMultiplier", player->buffMoveMultiplier);
    playerJson.Set("buffRangeMultiplier", player->buffRangeMultiplier);
    playerJson.Set("buffSkillUpgrades", player->buffSkillUpgrades);
    playerJson.Set("buffPityOffense", player->buffPityOffense);
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
        m_pauseNotice = "保存失败：无法写入存档";
        LogWarn("[2D] 存档写入失败: %s", path.c_str());
        return false;
    }
    ShowToast("游戏已保存");
    PlaySfx("levelup", 0.45f, 1.15f);
    LogInfo("[2D] 存档已保存: %s", path.c_str());
    return true;
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

    // 先恢复职业与已拥有技能，Reset 才能把未拥有技能的等级初始化为 0。
    m_playerClass = Clamp(playerJson["playerClass"].AsInt(kSurvivorsDefaultClass), 0,
                          kSurvivorsClassCount - 1);
    const int baseSkill = kSurvivorsClasses[m_playerClass].baseSkill;
    int       savedMask = playerJson["loadoutMask"].AsInt(1 << baseSkill) & 0x3F;
    savedMask |= (1 << baseSkill);
    const int baseMask = 1 << baseSkill;
    Reset(world);
    GameObject* playerObject = FindPlayer(world);
    auto*       player = playerObject ? playerObject->GetComponent<SurvivorsPlayerComponent>() : nullptr;
    if (!player || !playerObject) return false;

    player->maxHp = playerJson["maxHp"].AsFloat(player->maxHp);
    player->hp = playerJson["hp"].AsFloat(player->hp);
    player->level = playerJson["level"].AsInt(player->level);
    player->xp = playerJson["xp"].AsFloat(player->xp);
    player->xpNext = playerJson["xpNext"].AsFloat(player->xpNext);
    player->pendingLevelUps = 0;
    player->killCount = playerJson["killCount"].AsInt(player->killCount);
    player->playerClass = m_playerClass;
    player->loadoutMask = NormalizeLoadoutMask(savedMask, baseMask);
    // 旧存档只有布尔 arrowSpread（八向 = 新体系 4 级），读档时换算过来。
    const int legacySpreadLevel = playerJson["arrowSpread"].AsBool(false) ? 4 : 0;
    player->arrowSpreadLevel =
        player->HasSkill(0) ? Clamp(playerJson["arrowSpreadLevel"].AsInt(legacySpreadLevel), 0,
                                    kSurvivorsMaxArrowSpreadLevel)
                            : 0;
    player->arrowSpeedLevel = player->HasSkill(0) ? playerJson["arrowSpeedLevel"].AsInt(player->arrowSpeedLevel) : 0;
    player->arrowDamageLevel = player->HasSkill(0) ? playerJson["arrowDamageLevel"].AsInt(player->arrowDamageLevel) : 0;
    player->arrowRangeLevel = player->HasSkill(0) ? playerJson["arrowRangeLevel"].AsInt(player->arrowRangeLevel) : 0;
    player->lightningLevel = player->HasSkill(1) ? playerJson["lightningLevel"].AsInt(player->lightningLevel) : 0;
    player->bladeLevel = player->HasSkill(2) ? playerJson["bladeLevel"].AsInt(player->bladeLevel) : 0;
    player->frostLevel = player->HasSkill(3) ? playerJson["frostLevel"].AsInt(player->frostLevel) : 0;
    player->poisonLevel = player->HasSkill(4) ? playerJson["poisonLevel"].AsInt(player->poisonLevel) : 0;
    player->fireballLevel = player->HasSkill(5) ? playerJson["fireballLevel"].AsInt(player->fireballLevel) : 0;
    player->meteorLevel = player->HasSkill(6) ? playerJson["meteorLevel"].AsInt(player->meteorLevel) : 0;
    player->singularityLevel = player->HasSkill(7) ? playerJson["singularityLevel"].AsInt(player->singularityLevel) : 0;
    player->prismLevel = player->HasSkill(8) ? playerJson["prismLevel"].AsInt(player->prismLevel) : 0;
    player->plagueLevel = player->HasSkill(9) ? playerJson["plagueLevel"].AsInt(player->plagueLevel) : 0;
    player->buffDamageMultiplier = (std::max)(1.0f, playerJson["buffDamageMultiplier"].AsFloat(1.0f));
    player->buffAttackSpeedMultiplier = (std::max)(1.0f, playerJson["buffAttackSpeedMultiplier"].AsFloat(1.0f));
    player->buffMoveMultiplier = (std::max)(1.0f, playerJson["buffMoveMultiplier"].AsFloat(1.0f));
    player->buffRangeMultiplier = (std::max)(1.0f, playerJson["buffRangeMultiplier"].AsFloat(1.0f));
    player->buffSkillUpgrades = playerJson["buffSkillUpgrades"].AsInt(0);
    player->buffPityOffense = playerJson["buffPityOffense"].AsInt(0);
    g_playerDamageMultiplier = player->buffDamageMultiplier;
    player->arrowSpeedLevel = (std::min)(player->arrowSpeedLevel, kSurvivorsMaxArrowSpeedLevel);
    player->arrowDamageLevel = (std::min)(player->arrowDamageLevel, kSurvivorsMaxArrowDamageLevel);
    player->arrowRangeLevel = (std::min)(player->arrowRangeLevel, kSurvivorsMaxArrowRangeLevel);
    player->lightningLevel = (std::min)(player->lightningLevel, kSurvivorsMaxSkillLevel);
    player->bladeLevel = (std::min)(player->bladeLevel, kSurvivorsMaxSkillLevel);
    player->frostLevel = (std::min)(player->frostLevel, kSurvivorsMaxSkillLevel);
    player->poisonLevel = (std::min)(player->poisonLevel, kSurvivorsMaxSkillLevel);
    player->fireballLevel = (std::min)(player->fireballLevel, kSurvivorsMaxSkillLevel);
    player->meteorLevel = (std::min)(player->meteorLevel, kSurvivorsMaxSkillLevel);
    player->singularityLevel = (std::min)(player->singularityLevel, kSurvivorsMaxSkillLevel);
    player->prismLevel = (std::min)(player->prismLevel, kSurvivorsMaxSkillLevel);
    player->plagueLevel = (std::min)(player->plagueLevel, kSurvivorsMaxSkillLevel);
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
    g_lastSkillDropTime = m_time; // 读档后从当前时间重新计时，避免一进游戏就触发时间保底
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

// ================================================================ 导演：UI 数据与三选一
void SurvivorsDirector::InitializeUI(AssetManager& assets, ImFont* body, ImFont* large, ImFont* display) {
    m_uiReady = m_ui.Load(assets, body, large, display);
    if (!m_uiReady) LogWarn("[2D] UI 切图未就绪，将使用纯色回退绘制");
}

SurvivorsSettingsState SurvivorsDirector::GetSettings() const {
    SurvivorsSettingsState state;
    state.musicEnabled = g_settings.musicEnabled;
    state.sfxEnabled = g_settings.sfxEnabled;
    state.masterVolume = g_settings.masterVolume;
    state.musicVolume = g_settings.musicVolume;
    state.sfxVolume = g_settings.sfxVolume;
    return state;
}

void SurvivorsDirector::ApplySettings(const SurvivorsSettingsState& settings, bool persist) {
    g_settings.musicEnabled = settings.musicEnabled;
    g_settings.sfxEnabled = settings.sfxEnabled;
    g_settings.masterVolume = settings.masterVolume;
    g_settings.musicVolume = settings.musicVolume;
    g_settings.sfxVolume = settings.sfxVolume;
    ApplySurvivorsAudioSettings();
    if (persist) SaveSurvivorsSettings();
    if (g_settings.musicEnabled) {
        EnsureMusic();
    } else {
        AudioSystem::Get().StopMusic();
    }
}

void SurvivorsDirector::GenerateLevelUpChoices(const SurvivorsPlayerComponent& player) {
    // 只在本局携带、且未满级的项目里抽卡：一个箭矢强化 + 最多两个不同技能；
    // 全都满级后补“生命补给”卡，保证三选一永远有内容。
    std::vector<SurvivorsLevelUpChoice> arrowOptions;
    std::vector<SurvivorsLevelUpChoice> skillOptions;
    if (player.HasSkill(0) && player.SkillLevel(0) < kSurvivorsMaxSkillLevel) {
        if (player.arrowSpreadLevel < kSurvivorsMaxArrowSpreadLevel) arrowOptions.push_back({0, 0}); // 散射
        if (player.arrowSpeedLevel < kSurvivorsMaxArrowSpeedLevel) arrowOptions.push_back({0, 1});
        if (player.arrowDamageLevel < kSurvivorsMaxArrowDamageLevel) arrowOptions.push_back({0, 2});
        if (player.arrowRangeLevel < kSurvivorsMaxArrowRangeLevel) arrowOptions.push_back({0, 3});
    }
    for (int skill = 1; skill < kSurvivorsSkillCount; ++skill) {
        if (player.HasSkill(skill) && !player.SkillMaxed(skill)) skillOptions.push_back({skill, 0});
    }
    std::shuffle(arrowOptions.begin(), arrowOptions.end(), g_rng);
    std::shuffle(skillOptions.begin(), skillOptions.end(), g_rng);

    std::vector<SurvivorsLevelUpChoice> picked;
    if (!arrowOptions.empty()) picked.push_back(arrowOptions.back());
    for (const SurvivorsLevelUpChoice& choice : skillOptions) {
        if (picked.size() >= 3) break;
        picked.push_back(choice);
    }
    while (picked.size() < 3 && arrowOptions.size() > 1) {
        picked.push_back(arrowOptions[arrowOptions.size() - 2]);
        arrowOptions.pop_back();
    }

    static const SurvivorsLevelUpChoice kFallbacks[3] = {{100, 0}, {100, 1}, {100, 2}};
    for (int i = 0; picked.size() < 3; ++i) picked.push_back(kFallbacks[i % 3]);

    for (int i = 0; i < 3; ++i) m_levelChoices[i] = picked[static_cast<size_t>(i)];
    // 卡片顺序也随机，避免永远是箭矢排第一
    std::shuffle(m_levelChoices, m_levelChoices + 3, g_rng);
}

void SurvivorsDirector::OpenLevelUp(const SurvivorsPlayerComponent& player) {
    if (m_state == State::LevelUp) return;
    m_state = State::LevelUp;
    m_selectedSkill = -1;
    GenerateLevelUpChoices(player);
    PlaySfx("levelup", 0.55f, 1.05f);
}

void SurvivorsDirector::ApplyLevelUpChoice(World& world, int index) {
    if (m_state != State::LevelUp || index < 0 || index > 2) return;
    GameObject* playerObject = FindPlayer(world);
    auto*       player = playerObject ? playerObject->GetComponent<SurvivorsPlayerComponent>() : nullptr;
    if (!player) return;

    const SurvivorsLevelUpChoice choice = m_levelChoices[index];
    player->ApplyLevelUpUpgrade(choice.skill, choice.sub);
    player->CompleteLevelUp();
    PlaySfx("click", 0.55f, 1.05f);

    if (player->pendingLevelUps > 0) {
        GenerateLevelUpChoices(*player);
        return;
    }
    m_state = State::Playing;
    m_selectedSkill = -1;
}

void SurvivorsDirector::ApplyPreviewState(World& world, const std::string& screen) {
    // 仅用于 --ui-preview 截图/走查。正常游戏不会调用。
    if (screen.empty()) return;
    m_showSettings = false;
    m_settingsReturn = State::Menu;
    m_pauseNotice.clear();
    m_confirmExit = false;

    auto seed = [&](bool dead) {
        Reset(world);
        m_time = 156.0f;
        m_kills = 47;
        g_kills = 47;
        if (GameObject* playerObject = FindPlayer(world)) {
            if (auto* player = playerObject->GetComponent<SurvivorsPlayerComponent>()) {
                player->level = 3;
                player->xp = 8.0f;
                player->xpNext = 12.0f;
                player->hp = dead ? 0.0f : player->maxHp * 0.82f;
                player->alive = !dead;
                player->playerClass = kSurvivorsDefaultClass;
                player->loadoutMask = (1 << kSurvivorsClasses[kSurvivorsDefaultClass].baseSkill);
                player->arrowSpeedLevel = 1;
                player->arrowDamageLevel = 1;
                player->lightningLevel = 0;
                player->bladeLevel = 0;
                player->frostLevel = 0;
                player->poisonLevel = 0;
                player->fireballLevel = 0;
                player->RefreshDerivedStats();
            }
        }
    };

    if (screen == "menu") {
        ReturnToMenu();
    } else if (screen == "prepare") {
        Reset(world);
        m_state = State::Prepare;
    } else if (screen == "hud") {
        seed(false);
        // 走查截图：地上放一个“新技能”和一个强化，展示手动拾取与磁吸的区别。
        SpawnPickup(world, Vec2(2.6f, 0.9f),
                    static_cast<int>(SurvivorsPickupType::SkillUnlockBase) + 1);
        SpawnPickup(world, Vec2(-2.4f, 0.8f), static_cast<int>(SurvivorsPickupType::ArrowAttackSpeed));
    } else if (screen == "hud5") {
        seed(false);
        if (GameObject* playerObject = FindPlayer(world)) {
            if (auto* player = playerObject->GetComponent<SurvivorsPlayerComponent>()) {
                player->loadoutMask = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 5);
                player->lightningLevel = 2;
                player->bladeLevel = 1;
                player->frostLevel = 2;
                player->poisonLevel = 0;
                player->fireballLevel = 2;
                player->RefreshDerivedStats();
            }
        }
    } else if (screen == "endgame") {
        seed(false);
        m_time = 600.0f;
        g_gameTime = m_time;
        m_kills = 900;
        g_kills = 900;
        if (GameObject* playerObject = FindPlayer(world)) {
            if (auto* player = playerObject->GetComponent<SurvivorsPlayerComponent>()) {
                player->level = 40;
                player->maxHp = 340.0f;
                player->hp = 340.0f;
                player->loadoutMask = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 5);
                player->arrowSpreadLevel = kSurvivorsMaxArrowSpreadLevel;
                player->arrowSpeedLevel = kSurvivorsMaxArrowSpeedLevel;
                player->arrowDamageLevel = kSurvivorsMaxArrowDamageLevel;
                player->arrowRangeLevel = kSurvivorsMaxArrowRangeLevel;
                player->lightningLevel = kSurvivorsMaxSkillLevel;
                player->bladeLevel = kSurvivorsMaxSkillLevel;
                player->frostLevel = kSurvivorsMaxSkillLevel;
                player->poisonLevel = 0;
                player->fireballLevel = kSurvivorsMaxSkillLevel;
                player->invulnerable = 999.0f;
                player->RefreshDerivedStats();
                const Vec2 center(playerObject->Position().x, playerObject->Position().y);
                for (int i = 0; i < 48; ++i) {
                    const float angle = static_cast<float>(i) * kTwoPi / 48.0f + RandomRange(-0.06f, 0.06f);
                    const float radius = RandomRange(4.5f, 9.0f);
                    SpawnEnemy(world, center + Vec2(std::cos(angle), std::sin(angle)) * radius,
                               PickEnemyType(m_time), EnemyHpScale(m_time), EnemySpeedScale(m_time),
                               EnemyDamageScale(m_time));
                }
            }
        }
    } else if (screen == "newskills") {
        seed(false);
        m_time = 600.0f;
        g_gameTime = m_time;
        m_kills = 900;
        g_kills = 900;
        if (GameObject* playerObject = FindPlayer(world)) {
            if (auto* player = playerObject->GetComponent<SurvivorsPlayerComponent>()) {
                player->level = 40;
                player->maxHp = 340.0f;
                player->hp = 340.0f;
                player->loadoutMask = (1 << 0) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9);
                player->arrowSpreadLevel = kSurvivorsMaxArrowSpreadLevel;
                player->arrowSpeedLevel = kSurvivorsMaxArrowSpeedLevel;
                player->arrowDamageLevel = kSurvivorsMaxArrowDamageLevel;
                player->arrowRangeLevel = kSurvivorsMaxArrowRangeLevel;
                player->meteorLevel = kSurvivorsMaxSkillLevel;
                player->singularityLevel = kSurvivorsMaxSkillLevel;
                player->prismLevel = kSurvivorsMaxSkillLevel;
                player->plagueLevel = kSurvivorsMaxSkillLevel;
                player->invulnerable = 999.0f;
                player->RefreshDerivedStats();
                const Vec2 center(playerObject->Position().x, playerObject->Position().y);
                for (int i = 0; i < 48; ++i) {
                    const float angle = static_cast<float>(i) * kTwoPi / 48.0f + RandomRange(-0.06f, 0.06f);
                    const float radius = RandomRange(4.5f, 9.0f);
                    SpawnEnemy(world, center + Vec2(std::cos(angle), std::sin(angle)) * radius,
                               PickEnemyType(m_time), EnemyHpScale(m_time), EnemySpeedScale(m_time),
                               EnemyDamageScale(m_time));
                }
            }
        }
    } else if (screen == "bot") {
        seed(false);
        g_autopilot = true;
        m_state = State::Playing;
    } else if (screen == "botstrong") {
        seed(false);
        g_autopilot = true;
        m_state = State::Playing;
        if (GameObject* playerObject = FindPlayer(world)) {
            if (auto* player = playerObject->GetComponent<SurvivorsPlayerComponent>()) {
                player->loadoutMask = (1 << 0) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 7);
                player->bladeLevel = 10;
                player->frostLevel = 10;
                player->poisonLevel = 10;
                player->singularityLevel = 10;
                player->RefreshDerivedStats();
            }
        }
    } else if (screen == "buff") {
        seed(false);
        m_time = 30.0f;
        g_gameTime = m_time;
        m_kills = 80;
        g_kills = 80;
        if (GameObject* playerObject = FindPlayer(world)) {
            if (auto* player = playerObject->GetComponent<SurvivorsPlayerComponent>()) {
                player->level = 10;
                player->RefreshDerivedStats();
                const Vec2 center(playerObject->Position().x, playerObject->Position().y);
                for (int i = 0; i < 18; ++i) {
                    const float angle = static_cast<float>(i) * kTwoPi / 18.0f;
                    SpawnEnemy(world, center + Vec2(std::cos(angle), std::sin(angle)) * 8.0f,
                               PickEnemyType(m_time), EnemyHpScale(m_time), EnemySpeedScale(m_time),
                               EnemyDamageScale(m_time));
                }
                SpawnBuffChest(world);
                SpawnBuffChest(world);
                // 近身宝箱：验证拾取/抽奖 toast；远处宝箱留在地图上给小地图标记。
                GameObject* nearChest = SpawnSprite(world, "增益宝箱", "pickup_proc", center + Vec2(0.6f, 0.0f),
                                                    Vec2(1.35f, 1.35f), 4);
                if (auto* buff = nearChest->AddComponent<SurvivorsBuffComponent>()) buff->rarity = 1;
                g_buffChests.push_back(nearChest);
            }
        }
    } else if (screen == "chest") {
        seed(false);
        // 走查截图：固定放一个史诗宝箱，不进入拾取半径，方便和旧版图标做同场景对比。
        GameObject* chest = SpawnSprite(world, "增益宝箱", "sprites/kenney_tilemap.png", Vec2(2.4f, 0.6f),
                                        Vec2(1.15f, 1.15f), 4);
        if (auto* sprite = chest->GetComponent<SpriteRendererComponent>()) ApplyTileSprite(sprite, 7, 7);
        if (auto* buff = chest->AddComponent<SurvivorsBuffComponent>()) buff->rarity = 2;
        g_buffChests.push_back(chest);
    } else if (screen == "gacha" || screen == "gacha-burst") {
        seed(false);
        // 走查截图：直接进入抽奖并冻结在翻牌/光爆帧。
        if (BeginChestGacha(world, 2, "全屏雷击")) {
            m_gachaPreview = true;
            m_gachaTime = screen == "gacha-burst" ? kSurvivorsGachaBurst + 0.22f : kSurvivorsGachaReveal + 0.35f;
        }
    } else if (screen == "poison") {
        seed(false);
        if (GameObject* playerObject = FindPlayer(world)) {
            if (auto* player = playerObject->GetComponent<SurvivorsPlayerComponent>()) {
                player->loadoutMask = (1 << 0) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5);
                player->lightningLevel = 0;
                player->bladeLevel = kSurvivorsMaxSkillLevel;
                player->frostLevel = kSurvivorsMaxSkillLevel;
                player->poisonLevel = kSurvivorsMaxSkillLevel;
                player->fireballLevel = kSurvivorsMaxSkillLevel;
                player->RefreshDerivedStats();
                const Vec2 center(playerObject->Position().x, playerObject->Position().y);
                for (int i = 0; i < 32; ++i) {
                    const float angle = static_cast<float>(i) * kTwoPi / 32.0f;
                    const float radius = 4.0f + static_cast<float>(i % 4) * 1.1f;
                    SpawnEnemy(world, center + Vec2(std::cos(angle), std::sin(angle)) * radius,
                               PickEnemyType(m_time), EnemyHpScale(m_time), EnemySpeedScale(m_time),
                               EnemyDamageScale(m_time));
                }
            }
        }
    } else if (screen == "hud-orc") {
        seed(false);
        if (GameObject* playerObject = FindPlayer(world)) {
            if (auto* player = playerObject->GetComponent<SurvivorsPlayerComponent>()) {
                player->loadoutMask = 0;
                player->hp = player->maxHp = 500.0f;
                player->RefreshDerivedStats();
            }
        }
        SpawnEnemy(world, Vec2(-4.2f, 0.0f), 0, 1.0f, 1.0f, 1.0f);
        SpawnEnemy(world, Vec2(4.2f, 0.0f), 0, 1.0f, 1.0f, 1.0f);
        SpawnEnemy(world, Vec2(0.0f, 4.2f), 0, 1.0f, 1.0f, 1.0f);
        SpawnEnemy(world, Vec2(0.0f, -4.2f), 0, 1.0f, 1.0f, 1.0f);
    } else if (screen.rfind("hud-bow", 0) == 0) {
        seed(false);
        if (GameObject* playerObject = FindPlayer(world)) {
            if (auto* player = playerObject->GetComponent<SurvivorsPlayerComponent>()) {
                player->loadoutMask = (1 << 0);
                player->arrowSpreadLevel = Clamp(std::atoi(screen.c_str() + 7), 0, kSurvivorsMaxArrowSpreadLevel);
                player->arrowSpeedLevel = 1;
                player->arrowDamageLevel = 1;
                player->RefreshDerivedStats();
            }
        }
    } else if (screen.rfind("hud-blade", 0) == 0) {
        seed(false);
        if (GameObject* playerObject = FindPlayer(world)) {
            if (auto* player = playerObject->GetComponent<SurvivorsPlayerComponent>()) {
                player->loadoutMask = (1 << 2);
                player->bladeLevel = Clamp(std::atoi(screen.c_str() + 9), 0, kSurvivorsMaxSkillLevel);
                player->RefreshDerivedStats();
            }
        }
    } else if (screen == "pause") {
        seed(false);
        m_state = State::Paused;
    } else if (screen == "settings") {
        seed(false);
        m_state = State::Paused;
        m_settingsReturn = State::Paused;
        m_showSettings = true;
    } else if (screen == "levelup") {
        seed(false);
        GameObject* playerObject = FindPlayer(world);
        auto*       player = playerObject ? playerObject->GetComponent<SurvivorsPlayerComponent>() : nullptr;
        if (player) {
            player->pendingLevelUps = 1;
            OpenLevelUp(*player);
        }
    } else if (screen == "result") {
        seed(true);
        m_state = State::GameOver;
    } else if (screen == "codex") {
        Reset(world);
        m_state = State::Codex;
    } else {
        LogWarn("[2D] 未知的 --ui-preview 页面: %s", screen.c_str());
    }

    // 走查截图不需要“猎杀开始”提示遮挡画面
    m_toastTimer = 0.0f;
    m_toastText.clear();
    if (GameObject* playerObject = FindPlayer(world)) {
        if (auto* player = playerObject->GetComponent<SurvivorsPlayerComponent>()) {
            player->pickupToast.clear();
            player->pickupToastTimer = 0.0f;
        }
    }
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

    auto textureId = [](Texture* texture) -> ImTextureID {
        return static_cast<ImTextureID>(texture ? texture->ImGuiTexId() : 0);
    };
    // 画一张序列帧/精灵表：center 为世界坐标中心，halfW/halfH 为世界半宽高。
    auto drawSheet = [&](Texture* texture, int columns, int rows, int frame, const Vec2& center, float halfWidth,
                         float halfHeight, ImU32 tint, float rotation) {
        if (!texture || columns <= 0 || rows <= 0) return;
        frame = Clamp(frame, 0, columns * rows - 1);
        const float cellU = 1.0f / static_cast<float>(columns);
        const float cellV = 1.0f / static_cast<float>(rows);
        const float u0 = static_cast<float>(frame % columns) * cellU;
        const float v0 = static_cast<float>(frame / columns) * cellV;
        const ImVec2 uv0(u0, v0);
        const ImVec2 uv1(u0 + cellU, v0);
        const ImVec2 uv2(u0 + cellU, v0 + cellV);
        const ImVec2 uv3(u0, v0 + cellV);
        const float  cs = std::cos(rotation);
        const float  sn = std::sin(rotation);
        const Vec2   right(cs * halfWidth, sn * halfWidth);
        const Vec2   up(-sn * halfHeight, cs * halfHeight);
        const ImVec2 p0 = project(center - right + up);
        const ImVec2 p1 = project(center + right + up);
        const ImVec2 p2 = project(center + right - up);
        const ImVec2 p3 = project(center - right - up);
        draw->AddImageQuad(textureId(texture), p0, p1, p2, p3, uv0, uv1, uv2, uv3, tint);
    };

    draw->PushClipRect(ImVec2(viewportX, viewportY),
                       ImVec2(viewportX + viewportWidth, viewportY + viewportHeight), true);

    GameObject* player = FindPlayer(world);
    auto*       playerComponent = player ? player->GetComponent<SurvivorsPlayerComponent>() : nullptr;
    if (player && playerComponent) {
        const Vec2 center(player->Position().x, player->Position().y);
        const ImVec2 centerScreen = project(center);

        // 毒雾光环（未携带毒雾时不绘制）
        if (playerComponent->HasSkill(4)) {
            const float auraRadius = playerComponent->PoisonRadius() * pixelsPerUnit;
            // 柔和的多层雾圈 + 透明烟雾贴图，避免只有一个硬边绿圆。
            for (int i = 0; i < 3; ++i) {
                const float radius = auraRadius * (1.0f - static_cast<float>(i) * 0.16f);
                draw->AddCircleFilled(centerScreen, radius, IM_COL32(60, 190, 85, 10), 56);
            }
            draw->AddCircle(centerScreen, auraRadius, IM_COL32(95, 235, 115, 70), 56, 1.5f);
            draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear);
            drawSheet(m_ui.fxPoison, 1, 1, 0, center, auraRadius / pixelsPerUnit, auraRadius / pixelsPerUnit,
                      IM_COL32(90, 220, 110, 42), m_time * 0.15f);
            draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest);
        }

        // 回旋刃：先画一圈环绕轨道，让飞剑的作用范围一眼可见。
        const int bladeCount = playerComponent->BladeCount();
        if (bladeCount > 0) {
            const float orbitRadius = playerComponent->BladeRadius() * pixelsPerUnit;
            draw->AddCircle(centerScreen, orbitRadius, IM_COL32(196, 214, 255, 46), 72, 2.0f);
            draw->AddCircle(centerScreen, orbitRadius - 2.0f, IM_COL32(196, 214, 255, 22), 72, 2.0f);
            const float bladeLength = playerComponent->BladeSize();
            for (int i = 0; i < bladeCount; ++i) {
                const float angle = playerComponent->BladeAngle() + static_cast<float>(i) * kTwoPi /
                                                                          static_cast<float>(bladeCount);
                const Vec2 tip = center + Vec2(std::cos(angle), std::sin(angle)) * playerComponent->BladeRadius();
                const Vec2 tangent(-std::sin(angle), std::cos(angle));
                const ImVec2 p0 = project(tip - tangent * bladeLength);
                const ImVec2 p1 = project(tip + tangent * bladeLength);
                draw->AddLine(p0, p1, IM_COL32(225, 235, 255, 235), 4.0f);
                draw->AddCircleFilled(project(tip), 4.0f, IM_COL32(255, 255, 255, 210));
            }
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

    // 增益宝箱：图标保持原色，用稀有度光环 + 顶部标签告诉玩家箱子的品质。
    for (GameObject* chest : g_buffChests) {
        if (!chest || !chest->Active()) continue;
        auto* component = chest->GetComponent<SurvivorsBuffComponent>();
        if (!component) continue;
        const Vec2   center(chest->Position().x, chest->Position().y);
        const ImVec2 screen = project(center);
        const Vec4   rarityColor = SurvivorsBuffRarityColor(component->rarity);
        const float  pulse = 0.5f + 0.5f * std::sin(m_time * 4.5f + center.x * 1.3f);
        draw->AddCircleFilled(screen, 30.0f + 5.0f * pulse, ToImColor(rarityColor, 0.10f), 32);
        draw->AddCircle(screen, 31.0f + 4.0f * pulse, ToImColor(rarityColor, 0.30f + 0.25f * pulse), 32, 1.8f);
        draw->AddCircle(screen, 42.0f + 7.0f * pulse, ToImColor(rarityColor, 0.12f), 32, 1.2f);
        for (int i = 0; i < 4; ++i) {
            const float angle = m_time * 1.2f + static_cast<float>(i) * kPi * 0.5f;
            const Vec2  spark = center + Vec2(std::cos(angle), std::sin(angle)) * 0.95f;
            const ImVec2 p = project(spark);
            draw->AddCircleFilled(p, 2.2f + 1.4f * pulse, ToImColor(rarityColor, 0.85f), 8);
            draw->AddCircle(p, 4.5f, ToImColor(rarityColor, 0.28f), 8, 1.0f);
        }

        char label[48];
        std::snprintf(label, sizeof(label), "%s增益", SurvivorsBuffRarityName(component->rarity));
        const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, label);
        const ImVec2 textPos = project(Vec2(center.x, center.y + 1.15f));
        const ImVec2 min(textPos.x - textSize.x * 0.5f - 5.0f, textPos.y - textSize.y - 3.0f);
        const ImVec2 max(textPos.x + textSize.x * 0.5f + 5.0f, textPos.y + 1.0f);
        draw->AddRectFilled(min, max, IM_COL32(8, 10, 15, 210), 4.0f);
        draw->AddRect(min, max, ToImColor(rarityColor, 0.9f), 4.0f);
        draw->AddText(ImGui::GetFont(), 13.0f, ImVec2(textPos.x - textSize.x * 0.5f, textPos.y - textSize.y),
                      ToImColor(rarityColor, 1.0f), label);
    }

    // 技能特效贴图（线性采样，避免序列帧被拉成硬边）
    draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear);

    // 回旋刃：刃口带一点奥术拖影，让环绕轨迹更醒目
    if (player && playerComponent && playerComponent->HasSkill(2)) {
        const Vec2 center(player->Position().x, player->Position().y);
        const int  bladeCount = playerComponent->BladeCount();
        for (int i = 0; i < bladeCount; ++i) {
            const float angle = playerComponent->BladeAngle() + static_cast<float>(i) * kTwoPi /
                                                                      static_cast<float>((std::max)(1, bladeCount));
            const Vec2 tip = center + Vec2(std::cos(angle), std::sin(angle)) * playerComponent->BladeRadius();
            const float bladeSize = playerComponent->BladeSize() * 1.15f;
            drawSheet(m_ui.fxArcane, 7, 1, static_cast<int>(m_time * 18.0f + i) % 7, tip, bladeSize, bladeSize,
                      IM_COL32(220, 235, 255, 165), angle + kPi * 0.5f);
        }
    }

    // 棱镜光束：旋转的发光长束
    if (player && playerComponent && playerComponent->HasSkill(8)) {
        const Vec2  center(player->Position().x, player->Position().y);
        const int   beamCount = playerComponent->PrismBeamCount();
        const float length = playerComponent->PrismBeamLength();
        for (int i = 0; i < beamCount; ++i) {
            const float angle = playerComponent->PrismBeamAngle() +
                                static_cast<float>(i) * kTwoPi / static_cast<float>((std::max)(1, beamCount));
            const Vec2 direction(std::cos(angle), std::sin(angle));
            const ImVec2 from = project(center);
            const ImVec2 to = project(center + direction * length);
            draw->AddLine(from, to, IM_COL32(90, 220, 255, 55), 10.0f);
            draw->AddLine(from, to, IM_COL32(160, 245, 255, 180), 4.0f);
            draw->AddLine(from, to, IM_COL32(240, 255, 255, 230), 1.5f);
            for (float step = 1.5f; step < length; step += 2.5f) {
                drawSheet(m_ui.fxArcane, 7, 1, static_cast<int>(m_time * 20.0f + step) % 7,
                          center + direction * step, 0.35f, 0.35f, IM_COL32(150, 240, 255, 150), angle);
            }
        }
    }

    // 闪电：折线 + 命中点的奥术爆发序列帧
    for (const LightningFx& fx : g_lightningFx) {
        if (fx.points.size() < 2) continue;
        const float t = Clamp01(fx.age / fx.life);
        std::vector<ImVec2> points;
        points.reserve(fx.points.size());
        for (const Vec2& point : fx.points) points.push_back(project(point));
        draw->AddPolyline(points.data(), static_cast<int>(points.size()),
                          IM_COL32(255, 240, 140, static_cast<int>(70 * (1 - t))), ImDrawFlags_None, fx.width + 4.0f);
        draw->AddPolyline(points.data(), static_cast<int>(points.size()),
                          IM_COL32(255, 250, 210, static_cast<int>(255 * (1 - t))), ImDrawFlags_None, fx.width);
        const Vec2   impact = fx.points.back();
        const int    frame = static_cast<int>(t * 6.99f);
        drawSheet(m_ui.fxArcane, 7, 1, frame, impact, 1.45f, 1.45f,
                  IM_COL32(255, 244, 175, static_cast<int>(235 * (1 - t))), 0.0f);
        draw->AddCircleFilled(project(impact), 26.0f * (1.0f - t) + 4.0f,
                              IM_COL32(255, 250, 200, static_cast<int>(110 * (1 - t))), 20);
    }

    // 霜环：扩散圆环 + 冰爆序列帧
    for (const FrostRingFx& fx : g_frostFx) {
        const float  t = Clamp01(fx.age / fx.life);
        const ImVec2 centerScreen = project(fx.center);
        const float  radius = fx.radius * pixelsPerUnit;
        draw->AddCircleFilled(centerScreen, radius, IM_COL32(150, 220, 255, static_cast<int>(26 * (1 - t))), 56);
        draw->AddCircle(centerScreen, radius, IM_COL32(180, 235, 255, static_cast<int>(210 * (1 - t))), 56, 2.0f);
        draw->AddCircle(centerScreen, radius * 0.82f, IM_COL32(220, 245, 255, static_cast<int>(90 * (1 - t))), 56,
                        1.0f);
        const int   frame = static_cast<int>(t * 4.99f);
        const float edge = fx.radius * 0.88f;
        for (int i = 0; i < 4; ++i) {
            const float angle = static_cast<float>(i) * kPi * 0.5f + t * 0.7f;
            const Vec2  position = fx.center + Vec2(std::cos(angle), std::sin(angle)) * edge;
            const float size = 0.55f + 0.55f * t;
            drawSheet(m_ui.fxWater, 5, 1, frame, position, size, size,
                      IM_COL32(205, 240, 255, static_cast<int>(200 * (1 - t))), angle + kPi * 0.5f);
        }
    }

    // 爆炸：火焰冲击圆 + 64 帧爆炸序列
    for (const ExplosionFx& fx : g_explosionFx) {
        const float  t = Clamp01(fx.age / fx.life);
        const ImVec2 centerScreen = project(fx.center);
        const float  radius = fx.radius * pixelsPerUnit;
        draw->AddCircleFilled(centerScreen, radius, ToImColor(fx.color, 0.28f * (1 - t)), 40);
        draw->AddCircle(centerScreen, radius, ToImColor(fx.color, 0.95f * (1 - t)), 40, 2.0f);
        // 只取前 40 帧（后面的烟雾帧太淡），并用暖橙色染色，和火球配色一致。
        const int frame = static_cast<int>(t * 39.0f);
        drawSheet(m_ui.fxExplosion, 8, 8, frame, fx.center, fx.radius * 1.15f, fx.radius * 1.15f,
                  ToImColor(fx.color, 1.0f - t * 0.25f), 0.0f);
    }

    // 火球：飞行弹体 + 拖尾
    for (const FireballFx& fireball : g_fireballs) {
        const ImVec2 screen = project(fireball.position);
        const ImVec2 trail = project(fireball.position - fireball.velocity * 0.08f);
        draw->AddLine(trail, screen, IM_COL32(255, 160, 60, 150), 7.0f);
        draw->AddCircleFilled(screen, 11.0f, IM_COL32(255, 120, 40, 65), 24);
        draw->AddCircleFilled(screen, 5.0f, IM_COL32(255, 225, 150, 245), 24);
        const int   frame = static_cast<int>(fireball.age * 14.0f) % 4;
        const float rotation = std::atan2(fireball.velocity.y, fireball.velocity.x) + kPi;
        drawSheet(m_ui.fxFireball, 2, 2, frame, fireball.position, 0.75f, 0.6f, IM_COL32_WHITE, rotation);
    }

    // 毒雾：缓慢上飘的绿色烟团
    for (const PoisonPuffFx& puff : g_poisonFx) {
        const float t = Clamp01(puff.age / puff.life);
        const float size = puff.size * (0.9f + t * 1.2f);
        drawSheet(m_ui.fxPoison, 1, 1, 0, puff.position, size, size,
                  IM_COL32(115, 230, 115, static_cast<int>(125 * (1 - t))), puff.age * 1.5f);
    }

    // 陨石：下落火球 + 落点爆炸
    for (const MeteorFx& meteor : g_meteors) {
        if (meteor.impacted) continue;
        const float p = Clamp01(meteor.age / meteor.delay);
        const Vec2  position = meteor.target + Vec2(0.0f, (1.0f - p) * 7.0f);
        const ImVec2 a = project(position - Vec2(0.35f, 0.35f));
        const ImVec2 b = project(position + Vec2(0.35f, 0.35f));
        draw->AddLine(project(position + Vec2(0.0f, 5.0f)), a, IM_COL32(255, 120, 40, 80), 8.0f);
        draw->AddImageQuad(textureId(m_ui.fxFireball), a, ImVec2(b.x, a.y), b, ImVec2(a.x, b.y),
                           ImVec2(0.0f, 0.0f), ImVec2(0.5f, 0.0f), ImVec2(0.5f, 0.5f), ImVec2(0.0f, 0.5f),
                           IM_COL32(255, 220, 160, 255));
        draw->AddCircle(project(meteor.target), meteor.radius * pixelsPerUnit * p,
                        IM_COL32(255, 150, 60, 180), 32, 2.0f);
    }

    // 黑洞：暗色球体 + 旋转奥术吸积盘
    for (const SingularityFx& hole : g_singularities) {
        const float t = Clamp01(hole.age / hole.life);
        const ImVec2 centerScreen = project(hole.center);
        const float  radius = hole.radius * pixelsPerUnit;
        draw->AddCircleFilled(centerScreen, radius, IM_COL32(18, 6, 32, 120), 48);
        draw->AddCircle(centerScreen, radius, IM_COL32(170, 90, 255, 190), 48, 2.0f);
        draw->AddCircleFilled(centerScreen, radius * 0.18f, IM_COL32(5, 2, 10, 245), 24);
        for (int ring = 0; ring < 3; ++ring) {
            const float ringRadius = radius * (0.45f + static_cast<float>(ring) * 0.2f);
            const int   frame = static_cast<int>(m_time * 24.0f + ring * 2.0f) % 7;
            drawSheet(m_ui.fxArcane, 7, 1, frame, hole.center + Vec2(0.0f, 0.0f),
                      ringRadius / pixelsPerUnit, ringRadius / pixelsPerUnit,
                      IM_COL32(170, 90, 255, static_cast<int>(150 * (1.0f - t * 0.4f))),
                      m_time * (1.2f + 0.4f * ring));
        }
    }

    draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest);

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
        std::snprintf(text, sizeof(text), "%.0f", number.value);
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
    g_killsSinceSkillDrop = 0;
    g_killsSincePotionDrop = 0;
    g_gameTime = 0.0f;
    g_lastSkillDropTime = 0.0f;
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

    // 地面：16x16 程序化石砖，64x64 重复成大贴图，2×2 张铺满扩大后的地图
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
        // 2×2 张同样的石砖贴图铺满扩大后的地图，砖块世界尺寸与旧地图基本一致。
        const float floorW = kSurvivorsMapWidth * 0.5f;
        const float floorH = kSurvivorsMapHeight * 0.5f;
        for (int gy = 0; gy < 2; ++gy) {
            for (int gx = 0; gx < 2; ++gx) {
                const Vec2 center((gx == 0 ? -0.5f : 0.5f) * floorW, (gy == 0 ? -0.5f : 0.5f) * floorH);
                SpawnSprite(world, "地面", "floor_big", center, Vec2(floorW, floorH), -10);
            }
        }
    }

    const float decorHalfX = kSurvivorsArenaHalfX - 1.0f;
    const float decorHalfY = kSurvivorsArenaHalfY - 1.0f;

    // 地图装饰：草和石子（无碰撞）。面积扩大 5 倍，数量也按 5 倍补足，避免大块空地。
    for (int i = 0; i < 400; ++i) {
        const Vec2 position(RandomRange(-decorHalfX, decorHalfX), RandomRange(-decorHalfY, decorHalfY));
        if (Length(position) < 2.5f) continue;
        if (CircleHitsObstacle(position, 0.8f)) continue;
        SpawnSprite(world, "草丛", "grass_proc", position, Vec2(0.85f, 0.85f), -9);
    }
    for (int i = 0; i < 300; ++i) {
        const Vec2 position(RandomRange(-decorHalfX, decorHalfX), RandomRange(-decorHalfY, decorHalfY));
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

    // 火把：纯装饰，但给暗色地图一点暖色光源；位置随地图同比例放大并补足外圈。
    auto spawnTorch = [&](const Vec2& position) {
        GameObject* torch =
            SpawnSprite(world, "火把", "sprites/kenney_tilemap.png", position, Vec2(1.1f, 1.1f), -8);
        if (auto* sprite = torch->GetComponent<SpriteRendererComponent>()) ApplyTileSprite(sprite, 10, 5);
        g_torches.push_back(position);
    };
    const Vec2 torchPositions[] = {{-10.0f, 6.0f},  {10.0f, 6.0f},  {-10.0f, -6.0f}, {10.0f, -6.0f},
                                   {-20.0f, 12.0f}, {20.0f, 12.0f}, {-20.0f, -12.0f}, {20.0f, -12.0f},
                                   {-30.0f, 0.0f},  {30.0f, 0.0f}};
    for (const Vec2& base : torchPositions) {
        spawnTorch(Vec2(base.x * kSurvivorsMapScale, base.y * kSurvivorsMapScale));
    }
    for (int i = 0; i < 24; ++i) {
        const Vec2 position(RandomRange(-decorHalfX, decorHalfX), RandomRange(-decorHalfY, decorHalfY));
        if (CircleHitsObstacle(position, 0.9f)) continue;
        spawnTorch(position);
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
    auto spawnObstacle = [&](float x, float y, float hx, float hy, int row, int col) {
        GameObject* object = SpawnSprite(world, "障碍", "sprites/kenney_tilemap.png", Vec2(x, y),
                                         Vec2(hx * 2.0f + 0.35f, hy * 2.0f + 0.35f), 0);
        if (auto* sprite = object->GetComponent<SpriteRendererComponent>()) {
            ApplyTileSprite(sprite, row, col);
            sprite->color = Vec4(0.95f, 0.95f, 0.95f, 1.0f);
        }
        g_obstacles.push_back({Vec2(x, y), Vec2(hx, hy)});
    };
    for (const ObstacleSpec& spec : obstacles) {
        spawnObstacle(spec.x * kSurvivorsMapScale, spec.y * kSurvivorsMapScale, spec.hx, spec.hy, spec.row,
                      spec.col);
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
    playerComponent->arenaHalf = Vec2(kSurvivorsArenaHalfX, kSurvivorsArenaHalfY);
    playerComponent->RefreshDerivedStats();
    LogInfo("[2D] 地图边界：玩家 ±%.1f × ±%.1f，刷怪/宝箱 ±%.1f × ±%.1f", playerComponent->arenaHalf.x,
            playerComponent->arenaHalf.y, kSurvivorsSpawnHalfX, kSurvivorsSpawnHalfY);

    // 装饰随机数固定，避免关卡每次加载长得不一样；玩法随机数再复位
    g_rng.seed(0x12345u);
    g_fxRng.seed(0x271828u);

    LogInfo("2D 幸存者关卡：%zu 个对象，%zu 个障碍", world.ObjectCount(), g_obstacles.size());
}

// ================================================================ 小地图
SurvivorsMinimapSnapshot BuildSurvivorsMinimapSnapshot(World& world) {
    SurvivorsMinimapSnapshot snapshot;
    if (GameObject* player = FindPlayer(world)) {
        snapshot.player = Vec2(player->Position().x, player->Position().y);
        if (auto* component = player->GetComponent<SurvivorsPlayerComponent>()) {
            snapshot.arenaHalf = component->arenaHalf;
        }
    }
    for (const MapObstacle& obstacle : g_obstacles) {
        snapshot.obstacles.push_back({obstacle.center, obstacle.half});
    }
    for (GameObject* chest : g_buffChests) {
        if (!chest || !chest->Active()) continue;
        snapshot.markers.push_back({Vec2(chest->Position().x, chest->Position().y), 0});
    }
    for (GameObject* pickup : g_pickups) {
        if (!pickup || !pickup->Active()) continue;
        snapshot.markers.push_back({Vec2(pickup->Position().x, pickup->Position().y), 1});
    }
    for (GameObject* enemy : g_enemies) {
        if (!enemy || !enemy->Active()) continue;
        snapshot.markers.push_back({Vec2(enemy->Position().x, enemy->Position().y), 2});
    }
    return snapshot;
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
    t.Field("playerClass", FieldType::Int, &SurvivorsPlayerComponent::playerClass, "职业", 0.0f, 2.0f);
    t.Field("arrowSpreadLevel", FieldType::Int, &SurvivorsPlayerComponent::arrowSpreadLevel, "箭矢散射等级",
            0.0f, static_cast<float>(kSurvivorsMaxArrowSpreadLevel));
    t.Field("arrowSpeedLevel", FieldType::Int, &SurvivorsPlayerComponent::arrowSpeedLevel, "攻速强化", 0.0f, 99.0f);
    t.Field("arrowDamageLevel", FieldType::Int, &SurvivorsPlayerComponent::arrowDamageLevel, "攻击强化", 0.0f, 99.0f);
    t.Field("arrowRangeLevel", FieldType::Int, &SurvivorsPlayerComponent::arrowRangeLevel, "范围强化", 0.0f, 99.0f);
    t.Field("lightningLevel", FieldType::Int, &SurvivorsPlayerComponent::lightningLevel, "雷霆等级", 1.0f, 99.0f);
    t.Field("bladeLevel", FieldType::Int, &SurvivorsPlayerComponent::bladeLevel, "回旋刃等级", 1.0f, 99.0f);
    t.Field("frostLevel", FieldType::Int, &SurvivorsPlayerComponent::frostLevel, "霜环等级", 1.0f, 99.0f);
    t.Field("poisonLevel", FieldType::Int, &SurvivorsPlayerComponent::poisonLevel, "毒雾等级", 1.0f, 99.0f);
    t.Field("fireballLevel", FieldType::Int, &SurvivorsPlayerComponent::fireballLevel, "火球等级", 1.0f, 99.0f);
    t.Field("meteorLevel", FieldType::Int, &SurvivorsPlayerComponent::meteorLevel, "陨石等级", 0.0f, 99.0f);
    t.Field("singularityLevel", FieldType::Int, &SurvivorsPlayerComponent::singularityLevel, "黑洞等级", 0.0f, 99.0f);
    t.Field("prismLevel", FieldType::Int, &SurvivorsPlayerComponent::prismLevel, "棱镜等级", 0.0f, 99.0f);
    t.Field("plagueLevel", FieldType::Int, &SurvivorsPlayerComponent::plagueLevel, "毒爆等级", 0.0f, 99.0f);
}

void SurvivorsEnemyComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<SurvivorsEnemyComponent>(); };
    t.Field("speed", FieldType::Float, &SurvivorsEnemyComponent::speed, "移动速度", 0.2f, 12.0f);
    t.Field("hp", FieldType::Float, &SurvivorsEnemyComponent::hp, "生命", 1.0f, 500.0f);
    t.Field("maxHp", FieldType::Float, &SurvivorsEnemyComponent::maxHp, "最大生命", 1.0f, 999.0f);
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
    t.Field("type", FieldType::Int, &SurvivorsPickupComponent::type, "强化/技能类型", 0.0f, 29.0f);
    t.Field("magnetRange", FieldType::Float, &SurvivorsPickupComponent::magnetRange, "吸附范围", 0.0f, 20.0f);
}

void SurvivorsBuffComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<SurvivorsBuffComponent>(); };
    t.Field("rarity", FieldType::Int, &SurvivorsBuffComponent::rarity, "稀有度", 0.0f, 2.0f);
    t.Field("life", FieldType::Float, &SurvivorsBuffComponent::life, "存在时间", 1.0f, 120.0f);
}

} // namespace erm
