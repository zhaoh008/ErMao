// ErMao Engine - 3D 幸存者玩法实现
#include "game/Survivors3D.h"

#include "asset/AssetManager.h"
#include "core/Log.h"
#include "game/ParticleEmitter.h"
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

namespace erm {

namespace {

std::vector<GameObject*> g3dEnemies;
bool  g3dInputEnabled = true;
int   g3dKills = 0;
float g3dRng = 0.7f;

float Random01() {
    g3dRng = std::fmod(g3dRng * 1103515245.0f + 12345.0f, 2147483648.0f);
    return g3dRng / 2147483648.0f;
}

Vec2 SafeDir2(const Vec2& v) {
    const float len = Length(v);
    return len > 1e-5f ? v / len : Vec2(0.0f, 0.0f);
}

// 漂浮伤害数字（3D）：世界坐标 + 世界速度，投影到屏幕时按距离缩放
struct DamageNumber3D {
    Vec3  position;
    Vec3  velocity;
    float age = 0.0f;
    float life = 0.9f;
    float value = 0.0f;
};

std::vector<DamageNumber3D> g3dDamageNumbers;
float                       g3dFxRng = 0.314159f;

float FxRandom01() {
    g3dFxRng = std::fmod(g3dFxRng * 1103515245.0f + 12345.0f, 2147483648.0f);
    return g3dFxRng / 2147483648.0f;
}

void SpawnDamageNumber(const Vec3& position, float value) {
    if (g3dDamageNumbers.size() >= 96) g3dDamageNumbers.erase(g3dDamageNumbers.begin());
    DamageNumber3D number;
    number.position = position;
    number.velocity = Vec3((FxRandom01() - 0.5f) * 0.8f, 2.4f, (FxRandom01() - 0.5f) * 0.8f);
    number.life = 0.95f + FxRandom01() * 0.2f;
    number.value = value;
    g3dDamageNumbers.push_back(number);
}

void UpdateDamageNumbers(float dt) {
    for (DamageNumber3D& number : g3dDamageNumbers) {
        number.age += dt;
        number.position += number.velocity * dt;
        number.velocity.y -= 2.6f * dt;
    }
    g3dDamageNumbers.erase(
        std::remove_if(g3dDamageNumbers.begin(), g3dDamageNumbers.end(),
                       [](const DamageNumber3D& number) { return number.age >= number.life; }),
        g3dDamageNumbers.end());
}

GameObject* FindPlayer(World& world) { return world.FindByName("玩家"); }

GameObject* SpawnModel(World& world, const std::string& name, const std::string& mesh, const Vec3& position,
                       const Vec3& scale, const Vec4& albedo, bool useColormap) {
    GameObject* object = world.CreateGameObject(name);
    object->SetPosition(position);
    object->SetScale(scale);
    auto* renderer = object->AddComponent<MeshRenderer>();
    renderer->mesh = mesh;
    renderer->albedo = albedo;
    renderer->roughness = 0.75f;
    if (useColormap) renderer->texture = "textures/kenney/colormap.png";
    return object;
}

// 短命特效：让发射器存在 life 秒后被销毁
class Survivors3DEffectComponent : public Component {
public:
    ER_COMPONENT(Survivors3DEffectComponent, "Survivors3D Effect")
    float life = 0.45f;

    void OnUpdate(float dt) override {
        life -= dt;
        if (life <= 0.0f && m_owner) {
            if (World* world = GetWorld()) world->DestroyObject(m_owner);
        }
    }
};

void Survivors3DEffectComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<Survivors3DEffectComponent>(); };
    t.Field("life", FieldType::Float, &Survivors3DEffectComponent::life, "存活时间", 0.05f, 2.0f);
}

void SpawnBurst(World& world, const Vec3& position, const char* texture, const Vec4& color, float size) {
    GameObject* effect = world.CreateGameObject("特效");
    effect->SetPosition(position);
    auto* emitter = effect->AddComponent<ParticleEmitterComponent>();
    emitter->texture = texture;
    emitter->rate = 120.0f;
    emitter->lifetime = 0.30f;
    emitter->lifetimeJitter = 0.1f;
    emitter->initialVelocity = Vec3(0.0f, 0.0f, 0.0f);
    emitter->spread = 2.6f;
    emitter->sizeStart = size;
    emitter->sizeEnd = 0.02f;
    emitter->colorStart = color;
    emitter->colorEnd = Vec4(color.x * 0.2f, color.y * 0.2f, color.z * 0.2f, 0.0f);
    emitter->gravity = Vec3(0.0f, -3.0f, 0.0f);
    effect->AddComponent<Survivors3DEffectComponent>();
}

void SpawnGem(World& world, const Vec3& position, float value) {
    GameObject* gem = world.CreateGameObject("宝石");
    gem->SetPosition(position);
    gem->SetScale(Vec3(0.28f, 0.28f, 0.28f));
    auto* renderer = gem->AddComponent<MeshRenderer>();
    renderer->mesh = "sphere";
    renderer->albedo = Vec4(0.25f, 0.85f, 1.0f, 1.0f);
    renderer->emissive = 1.6f;
    renderer->castShadow = false;
    auto* component = gem->AddComponent<Survivors3DGemComponent>();
    component->value = value;
}

} // namespace

// ================================================================ 玩家
void Survivors3DPlayerComponent::SetInputEnabled(bool enabled) { g3dInputEnabled = enabled; }
bool Survivors3DPlayerComponent::InputEnabled() { return g3dInputEnabled; }

void Survivors3DPlayerComponent::RecomputeAttackInterval() {
    attackInterval = Clamp(baseAttackInterval * m_levelIntervalScale * m_killIntervalScale, 0.07f, baseAttackInterval);
}

void Survivors3DPlayerComponent::ApplyKillCount(int kills) {
    if (kills == killCount) return;
    killCount = kills;
    // 递减叠加：每击杀 +3% 攻速 → 实际间隔 = 基础间隔 / (1 + 0.03 * 击杀数)
    m_killIntervalScale = 1.0f / (1.0f + 0.03f * static_cast<float>(killCount));
    RecomputeAttackInterval();
}

float Survivors3DPlayerComponent::KillAttackSpeedBonus() const {
    return m_killIntervalScale > 1e-5f ? 1.0f / m_killIntervalScale - 1.0f : 0.0f;
}

void Survivors3DPlayerComponent::ResetCombatStats() {
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
    m_levelIntervalScale = 1.0f;
    m_killIntervalScale = 1.0f;
    m_attackTimer = 0.0f;
    RecomputeAttackInterval();
}

void Survivors3DPlayerComponent::OnUpdate(float dt) {
    if (!alive || !m_owner) return;

    invulnerable = (std::max)(0.0f, invulnerable - dt);
    hitFlash = (std::max)(0.0f, hitFlash - dt * 3.0f);

    Vec2 move(0.0f, 0.0f);
    if (g3dInputEnabled) {
        if (Input::IsKeyDown('A')) move.x -= 1.0f;
        if (Input::IsKeyDown('D')) move.x += 1.0f;
        if (Input::IsKeyDown('W')) move.y -= 1.0f; // W 朝 -Z
        if (Input::IsKeyDown('S')) move.y += 1.0f;
    }

    const bool moving = (move.x * move.x + move.y * move.y) > 0.01f;
    if (moving) {
        const Vec2 direction = SafeDir2(move);
        Vec3       position = m_owner->Position();
        position.x += direction.x * speed * dt;
        position.z += direction.y * speed * dt;
        position.x = Clamp(position.x, -arenaHalf.x, arenaHalf.x);
        position.z = Clamp(position.z, -arenaHalf.y, arenaHalf.y);
        m_owner->SetPosition(position);

        // 模型朝向移动方向（Kenney 角色默认朝 +Z）
        const float yaw = std::atan2(direction.x, direction.y);
        m_owner->SetRotation(Quat::FromAxisAngle(Vec3(0, 1, 0), yaw));
    }

    if (auto* renderer = m_owner->GetComponent<MeshRenderer>()) {
        renderer->albedo = hitFlash > 0.0f ? Vec4(1.0f, 0.35f, 0.35f, 1.0f) : Vec4(0.55f, 0.72f, 1.0f, 1.0f);
    }

    m_attackTimer -= dt;
    if (m_attackTimer <= 0.0f) {
        FireAtNearestEnemy();
        m_attackTimer = attackInterval;
    }
}

void Survivors3DPlayerComponent::FireAtNearestEnemy() {
    World* world = GetWorld();
    if (!world || !m_owner) return;

    const Vec3 origin = m_owner->Position() + Vec3(0.0f, 0.9f, 0.0f);
    GameObject* nearest = nullptr;
    float best = attackRange;
    for (GameObject* enemy : g3dEnemies) {
        if (!enemy || !enemy->Active()) continue;
        const Vec3  p = enemy->Position();
        const float dx = p.x - origin.x;
        const float dz = p.z - origin.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        if (distance < best) {
            best = distance;
            nearest = enemy;
        }
    }
    if (!nearest) return;

    const Vec3 target = nearest->Position() + Vec3(0.0f, 0.7f, 0.0f);
    Vec3       direction = target - origin;
    const float length = Length(direction);
    if (length < 1e-4f) return;
    direction = direction / length;

    GameObject* bolt = world->CreateGameObject("箭");
    bolt->SetPosition(origin);
    bolt->SetScale(Vec3(0.22f, 0.22f, 0.22f));
    auto* renderer = bolt->AddComponent<MeshRenderer>();
    renderer->mesh = "sphere";
    renderer->albedo = Vec4(1.0f, 0.9f, 0.45f, 1.0f);
    renderer->emissive = 1.2f;
    renderer->castShadow = false;

    auto* projectile = bolt->AddComponent<Survivors3DProjectileComponent>();
    projectile->velocity = direction * projectileSpeed;
    projectile->damage = attackDamage;
    PlaySfx("shoot", 0.35f, 0.9f + Random01() * 0.2f);
}

void Survivors3DPlayerComponent::AddXp(float amount) {
    xp += amount;
    while (xp >= xpNext) {
        xp -= xpNext;
        ++level;
        xpNext *= 1.32f;
        attackDamage += 5.0f;
        m_levelIntervalScale = (std::max)(0.25f, m_levelIntervalScale * 0.92f);
        RecomputeAttackInterval();
        maxHp += 10.0f;
        hp = (std::min)(maxHp, hp + 10.0f);
        PlaySfx("levelup", 0.6f);
    }
}

// ================================================================ 敌人
void Survivors3DEnemyComponent::OnUpdate(float dt) {
    if (!m_owner) return;
    World* world = GetWorld();
    if (!world) return;

    hitFlash = (std::max)(0.0f, hitFlash - dt * 3.0f);
    if (auto* renderer = m_owner->GetComponent<MeshRenderer>()) {
        renderer->albedo = hitFlash > 0.0f ? Vec4(1.0f, 0.3f, 0.3f, 1.0f) : Vec4(0.85f, 0.85f, 0.9f, 1.0f);
    }

    if (hp <= 0.0f) {
        SpawnGem(*world, m_owner->Position() + Vec3(0.0f, 0.35f, 0.0f), xpValue);
        SpawnBurst(*world, m_owner->Position() + Vec3(0.0f, 0.8f, 0.0f), "particles/spark_01.png",
                   Vec4(1.0f, 0.55f, 0.2f, 1.0f), 0.5f);
        ++g3dKills; // 3D 版此前漏了计数：击杀数一直是 0，攻速/结算都不准
        g3dEnemies.erase(std::remove(g3dEnemies.begin(), g3dEnemies.end(), m_owner), g3dEnemies.end());
        world->DestroyObject(m_owner);
        return;
    }

    GameObject* player = FindPlayer(*world);
    if (!player) return;

    const Vec3  position = m_owner->Position();
    const Vec3  target = player->Position();
    const float dx = target.x - position.x;
    const float dz = target.z - position.z;
    const float distance = std::sqrt(dx * dx + dz * dz);

    // 朝向玩家
    m_owner->SetRotation(Quat::FromAxisAngle(Vec3(0, 1, 0), std::atan2(dx, dz)));

    if (distance > 1.5f) {
        const float inv = distance > 1e-4f ? 1.0f / distance : 0.0f;
        Vec3        next = position;
        next.x += dx * inv * speed * dt;
        next.z += dz * inv * speed * dt;
        m_owner->SetPosition(next);
    } else {
        m_attackTimer -= dt;
        if (m_attackTimer <= 0.0f) {
            if (auto* playerComponent = player->GetComponent<Survivors3DPlayerComponent>()) {
                if (playerComponent->invulnerable <= 0.0f) {
                    playerComponent->hp -= damage;
                    playerComponent->invulnerable = 0.4f;
                    playerComponent->hitFlash = 1.0f;
                }
            }
            m_attackTimer = attackInterval;
        }
    }
}

// ================================================================ 投射物
void Survivors3DProjectileComponent::OnUpdate(float dt) {
    if (!m_owner) return;
    World* world = GetWorld();
    if (!world) return;

    life -= dt;
    if (life <= 0.0f) {
        world->DestroyObject(m_owner);
        return;
    }

    Vec3 position = m_owner->Position();
    position += velocity * dt;
    m_owner->SetPosition(position);

    for (GameObject* enemy : g3dEnemies) {
        if (!enemy || !enemy->Active()) continue;
        const Vec3  p = enemy->Position();
        const float dx = p.x - position.x;
        const float dz = p.z - position.z;
        const float dy = (p.y + 0.8f) - position.y;
        if (dx * dx + dz * dz < 0.75f * 0.75f && std::fabs(dy) < 1.6f) {
            if (auto* component = enemy->GetComponent<Survivors3DEnemyComponent>()) {
                component->hp -= damage;
                component->hitFlash = 1.0f;
                SpawnDamageNumber(Vec3(p.x, p.y + 1.15f, p.z), damage);
            }
            SpawnBurst(*world, position, "particles/flame_01.png", Vec4(1.0f, 0.8f, 0.35f, 1.0f), 0.35f);
            world->DestroyObject(m_owner);
            return;
        }
    }
}

// ================================================================ 宝石
void Survivors3DGemComponent::OnUpdate(float dt) {
    if (!m_owner) return;
    World* world = GetWorld();
    if (!world) return;

    age += dt;
    const Vec3 position = m_owner->Position();
    m_owner->SetPosition(Vec3(position.x, position.y + std::sin(age * 4.0f) * dt * 0.6f, position.z));

    GameObject* player = FindPlayer(*world);
    if (!player) return;

    const Vec3  p = m_owner->Position();
    const Vec3  t = player->Position();
    const float dx = t.x - p.x;
    const float dz = t.z - p.z;
    const float distance = std::sqrt(dx * dx + dz * dz);

    if (distance < magnetRange) {
        const float inv = distance > 1e-4f ? 1.0f / distance : 0.0f;
        const float speed = 3.5f + age * 4.0f;
        m_owner->SetPosition(Vec3(p.x + dx * inv * speed * dt, p.y + (t.y + 0.5f - p.y) * 2.0f * dt,
                                p.z + dz * inv * speed * dt));
    }

    if (distance < 1.0f) {
        if (auto* playerComponent = player->GetComponent<Survivors3DPlayerComponent>()) {
            playerComponent->AddXp(value);
        }
        SpawnBurst(*world, p, "particles/star_01.png", Vec4(0.4f, 0.9f, 1.0f, 1.0f), 0.3f);
        PlaySfx("pickup", 0.45f, 0.95f + Random01() * 0.2f);
        world->DestroyObject(m_owner);
    }
}

// ================================================================ 导演
void Survivors3DDirector::SpawnEnemy(World& world, const Vec2& position, float hpScale, float speedScale) {
    static const char* kEnemyModels[3] = {"models/kenney/character-skeleton.obj",
                                          "models/kenney/character-zombie.obj",
                                          "models/kenney/character-vampire.obj"};
    const int model = static_cast<int>(Random01() * 3.0f) % 3;

    GameObject* enemy = SpawnModel(world, "亡灵", kEnemyModels[model], Vec3(position.x, 0.0f, position.y),
                                   Vec3(2.0f, 2.0f, 2.0f), Vec4(0.85f, 0.85f, 0.9f, 1.0f), true);
    auto* component = enemy->AddComponent<Survivors3DEnemyComponent>();
    component->hp *= hpScale;
    component->maxHp = component->hp;
    component->speed *= speedScale;
    g3dEnemies.push_back(enemy);
}

void Survivors3DDirector::SpawnWave(World& world) {
    if (g3dEnemies.size() > 90) return;
    GameObject* player = FindPlayer(world);
    const Vec2  center = player ? Vec2(player->Position().x, player->Position().z) : Vec2(0, 0);

    const int count = 2 + static_cast<int>(m_time / 14.0f);
    for (int i = 0; i < count; ++i) {
        const float angle = Random01() * kTwoPi;
        const float radius = 15.0f + Random01() * 5.0f;
        SpawnEnemy(world, Vec2(center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius),
                   1.0f + m_time / 38.0f, 1.0f + m_time / 140.0f);
    }
}

void Survivors3DDirector::Reset(World& world) {
    std::vector<GameObject*> toDestroy;
    for (GameObject* root : world.RootObjects()) {
        root->ForEach([&](GameObject* object) {
            if (object->GetComponent<Survivors3DEnemyComponent>() ||
                object->GetComponent<Survivors3DProjectileComponent>() ||
                object->GetComponent<Survivors3DGemComponent>() ||
                object->GetComponent<Survivors3DEffectComponent>()) {
                toDestroy.push_back(object);
            }
        });
    }
    for (GameObject* object : toDestroy) world.DestroyObject(object);
    world.ProcessPendingDestruction();
    g3dEnemies.clear();

    g3dDamageNumbers.clear();
    if (GameObject* player = FindPlayer(world)) {
        player->SetPosition(Vec3(0.0f, 0.0f, 0.0f));
        if (auto* component = player->GetComponent<Survivors3DPlayerComponent>()) component->ResetCombatStats();
    }

    g3dRng = 0.7f;
    g3dFxRng = 0.314159f;
    m_state = State::Playing;
    m_kills = 0;
    g3dKills = 0;
    m_time = 0.0f;
    m_spawnTimer = 1.0f;
    LogInfo("[3D] 开始游戏");
}

void Survivors3DDirector::Update(World& world, float dt) {
    if (m_state != State::Playing) return;

    m_time += dt;
    m_kills = g3dKills;
    UpdateDamageNumbers(dt);
    if (GameObject* player = FindPlayer(world)) {
        if (auto* component = player->GetComponent<Survivors3DPlayerComponent>()) component->ApplyKillCount(m_kills);
    }
    m_spawnTimer -= dt;
    if (m_spawnTimer <= 0.0f) {
        SpawnWave(world);
        m_spawnTimer = (std::max)(1.0f, 2.6f - m_time * 0.012f);
    }

    GameObject* player = FindPlayer(world);
    if (!player) return;
    auto* component = player->GetComponent<Survivors3DPlayerComponent>();
    if (!component) return;
    if (component->hp <= 0.0f && component->alive) {
        component->hp = 0.0f;
        component->alive = false;
        m_state = State::GameOver;
        PlaySfx("death", 0.6f);
        LogInfo("[3D] 游戏结束：存活 %.1f 秒，击杀 %d", m_time, m_kills);
    }
}

void Survivors3DDirector::DrawUI(World& world, float viewportX, float viewportY, float viewportWidth,
                                 float viewportHeight) {
    const ImVec2 center(viewportX + viewportWidth * 0.5f, viewportY + viewportHeight * 0.5f);

    auto centeredWindow = [&](const char* name, const ImVec2& size) {
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        return ImGui::Begin(name, nullptr,
                            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_NoCollapse);
    };

    if (m_state == State::Menu) {
        if (centeredWindow("##Survivors3DMenu", ImVec2(560, 380))) {
            ImGui::Dummy(ImVec2(0, 8));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.78f, 0.35f, 1.0f));
            ImGui::SetCursorPosX(180);
            ImGui::Text("二毛幸存者 3D（外部模型 Demo）");
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::TextWrapped("WASD 移动，角色自动朝最近的亡灵发射光弹。");
            ImGui::TextWrapped("击杀掉落经验宝石，靠近自动拾取；升级会提高攻击力与生命。");
            ImGui::TextWrapped("击杀越多攻速越快；亡灵头顶有 3D 血条，命中会飘出伤害数字。");
            ImGui::TextWrapped("模型来自 Kenney（CC0）：Blocky Characters + Graveyard Kit；");
            ImGui::TextWrapped("特效来自 Kenney Particle Pack（火焰/火花/星光）。");
            ImGui::Dummy(ImVec2(0, 12));
            if (ImGui::Button("开始游戏", ImVec2(240, 40))) { PlaySfx("click"); Reset(world); }
            ImGui::SameLine();
            if (ImGui::Button("退出", ImVec2(120, 40))) m_wantsQuit = true;
            ImGui::Dummy(ImVec2(0, 8));
            ImGui::TextDisabled("Esc 暂停 / 继续");
        }
        ImGui::End();
        return;
    }

    if (m_state == State::Paused) {
        if (centeredWindow("##Survivors3DPause", ImVec2(360, 240))) {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f), "已暂停");
            ImGui::Separator();
            {
                float volume = AudioSystem::Get().MasterVolume();
                if (ImGui::SliderFloat("音量", &volume, 0.0f, 1.0f)) AudioSystem::Get().SetMasterVolume(volume);
                bool muted = AudioSystem::Get().Muted();
                if (ImGui::Checkbox("静音", &muted)) AudioSystem::Get().SetMuted(muted);
            }            if (ImGui::Button("继续游戏", ImVec2(-1, 32))) m_state = State::Playing;
            if (ImGui::Button("重新开始", ImVec2(-1, 32))) Reset(world);
            if (ImGui::Button("返回主菜单", ImVec2(-1, 32))) m_state = State::Menu;
            if (ImGui::Button("退出程序", ImVec2(-1, 32))) m_wantsQuit = true;
        }
        ImGui::End();
        return;
    }

    if (m_state == State::GameOver) {
        if (centeredWindow("##Survivors3DOver", ImVec2(420, 260))) {
            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.32f, 1.0f), "你被亡灵淹没了");
            ImGui::Separator();
            ImGui::Text("存活时间：%.1f 秒", m_time);
            ImGui::Text("击杀：%d", m_kills);
            ImGui::Dummy(ImVec2(0, 8));
            if (ImGui::Button("重新开始", ImVec2(-1, 34))) Reset(world);
            if (ImGui::Button("返回主菜单", ImVec2(-1, 34))) m_state = State::Menu;
            if (ImGui::Button("退出程序", ImVec2(-1, 34))) m_wantsQuit = true;
        }
        ImGui::End();
        return;
    }

    GameObject* player = FindPlayer(world);
    auto*       component = player ? player->GetComponent<Survivors3DPlayerComponent>() : nullptr;
    const float hpRatio = component && component->maxHp > 0.0f ? Clamp01(component->hp / component->maxHp) : 0.0f;
    const float xpRatio = component && component->xpNext > 0.0f ? Clamp01(component->xp / component->xpNext) : 0.0f;

    ImGui::SetNextWindowPos(ImVec2(viewportX + 14.0f, viewportY + 34.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(320, 150), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.04f, 0.06f, 0.62f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
    ImGui::Begin("##Survivors3DHud", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar);
    ImGui::Text("生命 %.0f / %.0f", component ? component->hp : 0.0f, component ? component->maxHp : 0.0f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.85f, 0.25f, 0.28f, 1.0f));
    ImGui::ProgressBar(hpRatio, ImVec2(250, 12));
    ImGui::PopStyleColor();
    ImGui::Text("经验 %.0f / %.0f", component ? component->xp : 0.0f, component ? component->xpNext : 0.0f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.35f, 0.75f, 0.95f, 1.0f));
    ImGui::ProgressBar(xpRatio, ImVec2(250, 10));
    ImGui::PopStyleColor();
    ImGui::Text("等级 %d   击杀 %d   时间 %.0fs", component ? component->level : 1, m_kills, m_time);
    ImGui::Text("攻速 +%.0f%%   间隔 %.2fs   攻击 %.0f", component ? component->KillAttackSpeedBonus() * 100.0f : 0.0f,
                component ? component->attackInterval : 0.0f, component ? component->attackDamage : 0.0f);
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    ImGui::GetForegroundDrawList()->AddText(ImVec2(viewportX + 16, viewportY + viewportHeight - 30),
                                            IM_COL32(220, 225, 235, 180), "WASD 移动 · 自动攻击 · Esc 暂停");
}

void Survivors3DDirector::DrawCombatFeedback(World& world, const Camera& camera, float viewportX, float viewportY,
                                             float viewportWidth, float viewportHeight) {
    if (m_state != State::Playing) return;
    if (viewportWidth < 8.0f || viewportHeight < 8.0f) return;

    const Vec2 screenSize(viewportWidth, viewportHeight);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->PushClipRect(ImVec2(viewportX, viewportY),
                       ImVec2(viewportX + viewportWidth, viewportY + viewportHeight), true);

    // 头顶 3D 血条：锚在世界坐标、按透视投影到屏幕；被墓碑/柱子挡住时不画
    for (GameObject* enemy : g3dEnemies) {
        if (!enemy || !enemy->Active()) continue;
        auto* component = enemy->GetComponent<Survivors3DEnemyComponent>();
        if (!component || component->hp <= 0.0f) continue;

        // 亡灵模型是 2 倍缩放，最高约 1.65 m；血条放在头顶略上方
        const Vec3 head = enemy->Position() + Vec3(0.0f, 1.9f, 0.0f);
        Vec2       screen;
        if (!camera.WorldToScreen(head, screenSize, screen)) continue;

        const Vec3  occluderTarget = enemy->Position() + Vec3(0.0f, 0.9f, 0.0f);
        const float headDistance = Length(occluderTarget - camera.position);
        if (headDistance > 0.5f) {
            const Ray      visibilityRay{camera.position, Normalize(occluderTarget - camera.position)};
            World::RaycastHit blocker;
            if (world.Raycast(visibilityRay, headDistance - 0.25f, blocker) && blocker.object != enemy) continue;
        }

        const float distance = Length(camera.position - head);
        const float barWidth = Clamp(1500.0f / (std::max)(distance, 1.0f), 48.0f, 170.0f);
        const float barHeight = Clamp(barWidth * 0.075f, 6.0f, 11.0f);
        const float ratio = Clamp01(component->hp / (component->maxHp > 0.0f ? component->maxHp : 1.0f));
        const float px = viewportX + screen.x;
        const float py = viewportY + screen.y;
        const float left = px - barWidth * 0.5f;
        const float top = py - barHeight;

        draw->AddRectFilled(ImVec2(left - 1.0f, top - 1.0f), ImVec2(left + barWidth + 1.0f, top + barHeight + 1.0f),
                            IM_COL32(6, 7, 10, 210), 3.0f);
        draw->AddRectFilled(ImVec2(left, top), ImVec2(left + barWidth, top + barHeight), IM_COL32(28, 30, 36, 235),
                            2.0f);
        if (ratio > 0.0f) {
            const ImU32 fill = component->hitFlash > 0.0f ? IM_COL32(255, 255, 255, 255)
                                                           : IM_COL32(226, 64, 74, 245);
            draw->AddRectFilled(ImVec2(left, top), ImVec2(left + barWidth * ratio, top + barHeight), fill, 2.0f);
            draw->AddRectFilled(ImVec2(left + 1.0f, top + 1.0f),
                                ImVec2(left + barWidth * ratio - 1.0f, top + barHeight * 0.35f),
                                IM_COL32(255, 255, 255, 60), 1.0f);
        }
    }

    // 3D 伤害数字：世界坐标锚定、按透视投影与距离缩放，看起来有纵深
    for (const DamageNumber3D& number : g3dDamageNumbers) {
        Vec2 screen;
        if (!camera.WorldToScreen(number.position, screenSize, screen)) continue;
        const float distance = Length(camera.position - number.position);
        const float t = Clamp01(number.age / number.life);
        const float fontSize = Clamp(280.0f / (std::max)(distance, 1.0f), 13.0f, 32.0f) * Lerp(1.0f, 0.72f, t);
        char text[32];
        std::snprintf(text, sizeof(text), "-%.0f", number.value);
        const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
        const ImVec2 pos(viewportX + screen.x - textSize.x * 0.5f, viewportY + screen.y - textSize.y * 0.5f);
        const int alpha = static_cast<int>(255.0f * (1.0f - t));
        const ImU32 outline = IM_COL32(20, 12, 6, alpha);
        draw->AddText(ImGui::GetFont(), fontSize, ImVec2(pos.x - 1.5f, pos.y), outline, text);
        draw->AddText(ImGui::GetFont(), fontSize, ImVec2(pos.x + 1.5f, pos.y), outline, text);
        draw->AddText(ImGui::GetFont(), fontSize, ImVec2(pos.x, pos.y - 1.5f), outline, text);
        draw->AddText(ImGui::GetFont(), fontSize, ImVec2(pos.x, pos.y + 1.5f), outline, text);
        draw->AddText(ImGui::GetFont(), fontSize, pos, IM_COL32(255, 204, 84, alpha), text);
    }

    draw->PopClipRect();
}

// ================================================================ 关卡
void BuildSurvivors3DLevel(World& world, AssetManager& assets) {
    world.Clear();
    g3dEnemies.clear();
    g3dDamageNumbers.clear();
    g3dFxRng = 0.314159f;
    g3dKills = 0;

    // 光照
    {
        GameObject* sun = world.CreateGameObject("太阳");
        sun->SetRotation(Quat::FromEuler(ToRadians(-52.0f), ToRadians(-38.0f), 0.0f));
        auto* light = sun->AddComponent<DirectionalLightComponent>();
        light->color = Vec4(1.0f, 0.93f, 0.82f, 1.0f);
        light->intensity = 2.2f;
        world.sunIntensity = 2.2f;
        world.ambientUp = Vec3(0.16f, 0.18f, 0.24f);
        world.ambientDown = Vec3(0.05f, 0.05f, 0.07f);
    }

    // 地面
    {
        GameObject* ground = world.CreateGameObject("地面");
        ground->SetPosition(Vec3(0.0f, 0.0f, 0.0f));
        ground->SetScale(Vec3(76.0f, 1.0f, 76.0f));
        auto* renderer = ground->AddComponent<MeshRenderer>();
        renderer->mesh = "plane";
        renderer->albedo = Vec4(0.30f, 0.32f, 0.36f, 1.0f);
        renderer->roughness = 0.95f;
        renderer->texture = "noise";
        renderer->uvScale = 26.0f;
        renderer->castShadow = false;
    }

    // 墓碑/柱子/地穴（外部 OBJ，贴 colormap）
    {
        struct Prop {
            const char* mesh;
            float       x, z, scale;
        };
        const Prop props[] = {
            {"models/kenney/cross.obj", -7.0f, -5.0f, 1.4f},   {"models/kenney/cross-wood.obj", 6.0f, -7.0f, 1.3f},
            {"models/kenney/column-large.obj", -9.5f, 4.0f, 2.0f},
            {"models/kenney/column-large.obj", 9.5f, 5.0f, 2.0f},
            {"models/kenney/crypt-a.obj", 0.0f, -12.0f, 1.6f},  {"models/kenney/coffin.obj", -4.5f, 8.5f, 1.5f},
            {"models/kenney/altar-stone.obj", 5.5f, 9.0f, 1.6f},
            {"models/kenney/cross.obj", 12.0f, -2.0f, 1.2f},    {"models/kenney/cross-wood.obj", -12.5f, -1.5f, 1.2f},
            {"models/kenney/column-large.obj", 0.0f, 13.5f, 1.8f},
        };
        // 成排的十字架，营造墓园
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 5; ++col) {
                GameObject* grave = SpawnModel(world, "十字架", (row + col) % 2 == 0 ? "models/kenney/cross.obj"
                                                                                   : "models/kenney/cross-wood.obj",
                                               Vec3(-14.0f + col * 3.2f, 0.0f, 8.0f + row * 2.6f), Vec3(1.1f, 1.1f, 1.1f),
                                               Vec4(0.55f, 0.53f, 0.5f, 1.0f), false);
                grave->SetRotation(Quat::FromAxisAngle(Vec3(0, 1, 0), ToRadians((row * 5 + col) * 13.0f)));
            }
        }
        int index = 0;
        for (const Prop& prop : props) {
            GameObject* object = SpawnModel(world, "墓碑", prop.mesh, Vec3(prop.x, 0.0f, prop.z),
                                            Vec3(prop.scale, prop.scale, prop.scale), Vec4(0.58f, 0.56f, 0.53f, 1.0f),
                                            false);
            object->SetRotation(Quat::FromAxisAngle(Vec3(0, 1, 0), ToRadians(static_cast<float>(index) * 37.0f)));
            ++index;
        }
    }

    // 火把：Kenney 火焰贴图粒子（加法混合）
    {
        const float positions[2][2] = {{-11.0f, -9.0f}, {11.0f, -9.0f}};
        for (const auto& position : positions) {
            GameObject* torch = world.CreateGameObject("火把");
            torch->SetPosition(Vec3(position[0], 0.6f, position[1]));
            auto* renderer = torch->AddComponent<MeshRenderer>();
            renderer->mesh = "cylinder";
            renderer->albedo = Vec4(0.35f, 0.3f, 0.28f, 1.0f);
            auto* emitter = torch->AddComponent<ParticleEmitterComponent>();
            emitter->texture = "particles/flame_01.png";
            emitter->rate = 10.0f;
            emitter->lifetime = 0.55f;
            emitter->spread = 0.5f;
            emitter->sizeStart = 0.55f;
            emitter->sizeEnd = 0.05f;
            emitter->colorStart = Vec4(1.0f, 0.72f, 0.3f, 1.0f);
            emitter->colorEnd = Vec4(0.4f, 0.05f, 0.0f, 0.0f);
            emitter->gravity = Vec3(0.0f, 1.6f, 0.0f);
        }
    }

    // 玩家（Blocky character，外部 OBJ）
    {
        GameObject* player = SpawnModel(world, "玩家", "models/kenney/character-a.obj", Vec3(0.0f, 0.0f, 0.0f),
                                        Vec3(0.72f, 0.72f, 0.72f), Vec4(0.35f, 0.52f, 0.95f, 1.0f), false);
        auto* component = player->AddComponent<Survivors3DPlayerComponent>();
        component->arenaHalf = Vec2(30.0f, 30.0f);
    }

    LogInfo("3D 幸存者关卡：%zu 个对象（外部模型 + 粒子特效）", world.ObjectCount());
}

void RegisterSurvivors3DComponents() {
    Reflection::Register<Survivors3DPlayerComponent>("Survivors3D Player");
    Reflection::Register<Survivors3DEnemyComponent>("Survivors3D Enemy");
    Reflection::Register<Survivors3DProjectileComponent>("Survivors3D Projectile");
    Reflection::Register<Survivors3DGemComponent>("Survivors3D Gem");
    // 特效组件定义在本 TU 的匿名命名空间，注册函数也放在这里
    Reflection::Register<Survivors3DEffectComponent>("Survivors3D Effect");
}

} // namespace erm

// 反射：放在命名空间外会让 TypeInfo 找不到，这里补充 Describe 实现
namespace erm {

void Survivors3DPlayerComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<Survivors3DPlayerComponent>(); };
    t.Field("speed", FieldType::Float, &Survivors3DPlayerComponent::speed, "移动速度", 0.5f, 15.0f);
    t.Field("maxHp", FieldType::Float, &Survivors3DPlayerComponent::maxHp, "最大生命", 1.0f, 999.0f);
    t.Field("attackDamage", FieldType::Float, &Survivors3DPlayerComponent::attackDamage, "攻击力", 1.0f, 200.0f);
    t.Field("attackInterval", FieldType::Float, &Survivors3DPlayerComponent::attackInterval, "攻击间隔", 0.05f, 3.0f);
    t.Field("attackRange", FieldType::Float, &Survivors3DPlayerComponent::attackRange, "攻击范围", 1.0f, 30.0f);
    t.Field("projectileSpeed", FieldType::Float, &Survivors3DPlayerComponent::projectileSpeed, "弹速", 1.0f, 60.0f);
}

void Survivors3DEnemyComponent::OnDestroy() {
    // 对象真正释放时同步移除注册表项，避免 World::Clear/RemoveComponent 留下悬垂指针。
    // 只比较指针值，不解引用（此时 GameObject 正在析构）。
    if (!m_owner) return;
    g3dEnemies.erase(std::remove(g3dEnemies.begin(), g3dEnemies.end(), m_owner), g3dEnemies.end());
}

void Survivors3DEnemyComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<Survivors3DEnemyComponent>(); };
    t.Field("speed", FieldType::Float, &Survivors3DEnemyComponent::speed, "移动速度", 0.2f, 12.0f);
    t.Field("hp", FieldType::Float, &Survivors3DEnemyComponent::hp, "生命", 1.0f, 500.0f);
    t.Field("damage", FieldType::Float, &Survivors3DEnemyComponent::damage, "伤害", 1.0f, 100.0f);
    t.Field("xpValue", FieldType::Float, &Survivors3DEnemyComponent::xpValue, "经验值", 1.0f, 50.0f);
}

void Survivors3DProjectileComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<Survivors3DProjectileComponent>(); };
    t.Field("damage", FieldType::Float, &Survivors3DProjectileComponent::damage, "伤害", 1.0f, 200.0f);
    t.Field("life", FieldType::Float, &Survivors3DProjectileComponent::life, "寿命", 0.05f, 5.0f);
}

void Survivors3DGemComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<Survivors3DGemComponent>(); };
    t.Field("value", FieldType::Float, &Survivors3DGemComponent::value, "经验", 1.0f, 100.0f);
    t.Field("magnetRange", FieldType::Float, &Survivors3DGemComponent::magnetRange, "吸附范围", 0.0f, 20.0f);
}

} // namespace erm
