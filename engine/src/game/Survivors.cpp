// ErMao Engine - 2D 幸存者玩法实现
#include "game/Survivors.h"

#include "asset/AssetManager.h"
#include "core/Log.h"
#include "platform/Audio.h"
#include "platform/Input.h"
#include "renderer/RenderScene.h"
#include "scene/Components.h"
#include "scene/GameObject.h"
#include "scene/World.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace erm {

namespace {

std::vector<GameObject*> g_enemies; // 敌人注册表（投射物/自动攻击用，避免遍历整个场景）

bool  g_inputEnabled = true;
int   g_kills = 0;
float g_rng = 0.12345f;

float Random01() {
    // 简单 LCG，保证可复现
    g_rng = std::fmod(g_rng * 1103515245.0f + 12345.0f, 2147483648.0f);
    return g_rng / 2147483648.0f;
}

Vec2 SafeDir(const Vec2& v) {
    const float len = Length(v);
    return len > 1e-5f ? v / len : Vec2(0.0f, 0.0f);
}

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
    auto* component = gem->AddComponent<SurvivorsGemComponent>();
    component->value = value;
}

} // namespace

void SurvivorsPlayerComponent::SetInputEnabled(bool enabled) { g_inputEnabled = enabled; }
bool SurvivorsPlayerComponent::InputEnabled() { return g_inputEnabled; }

// ================================================================ 玩家
void SurvivorsPlayerComponent::OnUpdate(float dt) {
    if (!alive || !m_owner) return;

    invulnerable = (std::max)(0.0f, invulnerable - dt);
    hitFlash = (std::max)(0.0f, hitFlash - dt * 3.0f);

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
        position.x += direction.x * speed * dt;
        position.y += direction.y * speed * dt;
        position.x = Clamp(position.x, -arenaHalf.x, arenaHalf.x);
        position.y = Clamp(position.y, -arenaHalf.y, arenaHalf.y);
        m_owner->SetPosition(position);

        if (sprite && sprite->texture != "sprites/soldier_walk.png") {
            sprite->texture = "sprites/soldier_walk.png";
            if (animator) animator->Play(12.0f);
        }
        if (sprite) sprite->flipX = direction.x < 0.0f;
    } else if (sprite && sprite->texture != "sprites/soldier_idle.png") {
        sprite->texture = "sprites/soldier_idle.png";
        if (animator) animator->Play(6.0f);
    }

    // 自动攻击最近的敌人
    m_attackTimer -= dt;
    if (m_attackTimer <= 0.0f) {
        FireAtNearestEnemy(Vec2(m_owner->Position().x, m_owner->Position().y));
        m_attackTimer = attackInterval;
    }

    if (hitFlash > 0.0f && sprite) {
        sprite->color = Vec4(1.0f, 0.45f, 0.45f, 1.0f);
    } else if (sprite) {
        sprite->color = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

void SurvivorsPlayerComponent::FireAtNearestEnemy(const Vec2& origin) {
    World* world = GetWorld();
    if (!world) return;

    GameObject* nearest = nullptr;
    float       bestDistance = attackRange;
    for (GameObject* enemy : g_enemies) {
        if (!enemy || !enemy->Active()) continue;
        const Vec2  position(enemy->Position().x, enemy->Position().y);
        const float distance = Length(position - origin);
        if (distance < bestDistance) {
            bestDistance = distance;
            nearest = enemy;
        }
    }
    if (!nearest) return;

    const Vec2  target(nearest->Position().x, nearest->Position().y);
    const Vec2  direction = SafeDir(target - origin);
    const Vec2  spawn = origin + direction * 0.55f;
    const float angle = std::atan2(direction.y, direction.x);

    GameObject* arrow = SpawnSprite(*world, "箭", "sprites/arrow.png", spawn, Vec2(0.9f, 0.9f), 2);
    arrow->SetPosition(Vec3(spawn.x, spawn.y, 0.0f));
    if (auto* sprite = arrow->GetComponent<SpriteRendererComponent>()) {
        sprite->uv = Vec4(0.219f, 0.406f, 0.812f, 0.625f);
        sprite->rotation = angle;
        sprite->color = Vec4(1.0f, 0.95f, 0.6f, 1.0f);
    }

    auto* projectile = arrow->AddComponent<SurvivorsProjectileComponent>();
    projectile->velocity = direction * projectileSpeed;
    projectile->damage = attackDamage;
}

void SurvivorsPlayerComponent::AddXp(float amount) {
    xp += amount;
    while (xp >= xpNext) {
        xp -= xpNext;
        ++level;
        xpNext *= 1.35f;
        attackDamage += 4.0f;
        attackInterval = (std::max)(0.18f, attackInterval * 0.9f);
        maxHp += 8.0f;
        hp = (std::min)(maxHp, hp + 8.0f);
    }
}

// ================================================================ 敌人
void SurvivorsEnemyComponent::OnUpdate(float dt) {
    if (!m_owner) return;
    World* world = GetWorld();
    if (!world) return;

    hitFlash = (std::max)(0.0f, hitFlash - dt * 3.0f);
    if (auto* sprite = m_owner->GetComponent<SpriteRendererComponent>()) {
        sprite->color = hitFlash > 0.0f ? Vec4(1.0f, 0.5f, 0.5f, 1.0f) : Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    if (hp <= 0.0f) {
        SpawnGem(*world, Vec2(m_owner->Position().x, m_owner->Position().y), xpValue);
        ++g_kills;
        g_enemies.erase(std::remove(g_enemies.begin(), g_enemies.end(), m_owner), g_enemies.end());
        world->DestroyObject(m_owner);
        return;
    }

    GameObject* player = FindPlayer(*world);
    if (!player) return;

    const Vec2 position(m_owner->Position().x, m_owner->Position().y);
    const Vec2 target(player->Position().x, player->Position().y);
    const Vec2 toTarget = target - position;
    const float distance = Length(toTarget);

    if (auto* sprite = m_owner->GetComponent<SpriteRendererComponent>()) {
        sprite->flipX = toTarget.x > 0.0f; // 兽人贴图默认朝左
    }

    if (distance > 0.85f) {
        const Vec2 direction = SafeDir(toTarget);
        Vec3       next = m_owner->Position();
        next.x += direction.x * speed * dt;
        next.y += direction.y * speed * dt;
        m_owner->SetPosition(next);
    } else {
        m_attackTimer -= dt;
        if (m_attackTimer <= 0.0f) {
            if (auto* playerComponent = player->GetComponent<SurvivorsPlayerComponent>()) {
                if (playerComponent->invulnerable <= 0.0f) {
                    playerComponent->hp -= damage;
                    playerComponent->invulnerable = 0.35f;
                    playerComponent->hitFlash = 1.0f;
                }
            }
            m_attackTimer = attackInterval;
        }
    }
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
            }
            world->DestroyObject(m_owner);
            return;
        }
    }
}

// ================================================================ 宝石
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

    if (distance < magnetRange) {
        const Vec2 direction = SafeDir(toTarget);
        Vec3       next = m_owner->Position();
        const float speed = 3.0f + age * 4.0f;
        next.x += direction.x * speed * dt;
        next.y += direction.y * speed * dt;
        m_owner->SetPosition(next);
    }

    if (distance < 0.45f) {
        if (auto* playerComponent = player->GetComponent<SurvivorsPlayerComponent>()) {
            playerComponent->AddXp(value);
        }
        world->DestroyObject(m_owner);
    }
}

// ================================================================ 导演
void SurvivorsDirector::SpawnEnemy(World& world, const Vec2& position, float hpScale, float speedScale) {
    GameObject* enemy = SpawnSprite(world, "兽人", "sprites/orc_walk.png", position, Vec2(1.7f, 1.7f), 1);
    auto* animator = enemy->AddComponent<SpriteAnimatorComponent>();
    animator->fps = 10.0f;
    animator->frameMin = Vec2(0.440f, 0.410f);
    animator->frameMax = Vec2(0.660f, 0.580f);
    auto* component = enemy->AddComponent<SurvivorsEnemyComponent>();
    component->hp *= hpScale;
    component->speed *= speedScale;
    g_enemies.push_back(enemy);
}

void SurvivorsDirector::SpawnWave(World& world) {
    if (g_enemies.size() > 120) return; // 上限，避免刷爆
    GameObject* player = FindPlayer(world);
    const Vec2  center = player ? Vec2(player->Position().x, player->Position().y) : Vec2(0, 0);

    const int count = 2 + static_cast<int>(m_time / 15.0f);
    for (int i = 0; i < count; ++i) {
        const float angle = Random01() * kTwoPi;
        const float radius = 13.0f + Random01() * 4.0f;
        const Vec2  position(center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius);
        SpawnEnemy(world, position, 1.0f + m_time / 40.0f, 1.0f + m_time / 150.0f);
    }
}

void SurvivorsDirector::Reset(World& world) {
    // 清掉上一局生成的敌人/箭/宝石
    std::vector<GameObject*> toDestroy;
    for (GameObject* root : world.RootObjects()) {
        root->ForEach([&](GameObject* object) {
            if (object->GetComponent<SurvivorsEnemyComponent>() ||
                object->GetComponent<SurvivorsProjectileComponent>() ||
                object->GetComponent<SurvivorsGemComponent>()) {
                toDestroy.push_back(object);
            }
        });
    }
    for (GameObject* object : toDestroy) world.DestroyObject(object);
    world.ProcessPendingDestruction();
    g_enemies.clear();

    if (GameObject* player = FindPlayer(world)) {
        player->SetPosition(Vec3(0.0f, 0.0f, 0.0f));
        if (auto* component = player->GetComponent<SurvivorsPlayerComponent>()) {
            component->hp = component->maxHp;
            component->xp = 0.0f;
            component->level = 1;
            component->alive = true;
            component->invulnerable = 0.0f;
        }
    }

    g_rng = 0.12345f;
    m_state = State::Playing;
    m_kills = 0;
    g_kills = 0;
    m_time = 0.0f;
    m_spawnTimer = 1.0f;
    LogInfo("[2D] 开始游戏");
}

void SurvivorsDirector::Update(World& world, float dt) {
    if (m_state != State::Playing) return;

    m_time += dt;
    m_kills = g_kills;
    m_spawnTimer -= dt;
    if (m_spawnTimer <= 0.0f) {
        SpawnWave(world);
        m_spawnTimer = (std::max)(0.9f, 2.4f - m_time * 0.01f);
    }

    GameObject* player = FindPlayer(world);
    if (!player) return;
    auto* component = player->GetComponent<SurvivorsPlayerComponent>();
    if (!component) return;
    if (component->hp <= 0.0f && component->alive) {
        component->hp = 0.0f;
        component->alive = false;
        m_state = State::GameOver;
        LogInfo("[2D] 游戏结束：存活 %.1f 秒，击杀 %d", m_time, m_kills);
    }
}

void SurvivorsDirector::DrawUI(World& world, float viewportX, float viewportY, float viewportWidth,
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
        if (centeredWindow("##SurvivorsMenu", ImVec2(520, 360))) {
            ImGui::Dummy(ImVec2(0, 8));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.78f, 0.35f, 1.0f));
            ImGui::SetCursorPosX(160);
            ImGui::Text("二毛幸存者（2D Demo）");
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::TextWrapped("WASD 移动，角色会自动朝最近的敌人射箭。");
            ImGui::TextWrapped("击杀敌人掉落经验宝石，靠近自动拾取；升级会提高攻击力与生命。");
            ImGui::TextWrapped("坚持越久，敌人越多、越强。");
            ImGui::Dummy(ImVec2(0, 12));
            if (ImGui::Button("开始游戏", ImVec2(220, 40))) { PlaySfx("click"); Reset(world); }
            ImGui::SameLine();
            if (ImGui::Button("退出", ImVec2(120, 40))) m_wantsQuit = true;
            ImGui::Dummy(ImVec2(0, 8));
            ImGui::TextDisabled("Esc 暂停 / 继续");
        }
        ImGui::End();
        return;
    }

    if (m_state == State::Paused) {
        if (centeredWindow("##SurvivorsPause", ImVec2(360, 240))) {
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
        if (centeredWindow("##SurvivorsOver", ImVec2(420, 260))) {
            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.32f, 1.0f), "你被兽人淹没了");
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

    // ------------------------------------------------ HUD
    GameObject* player = FindPlayer(world);
    auto*       component = player ? player->GetComponent<SurvivorsPlayerComponent>() : nullptr;
    const float hpRatio = component && component->maxHp > 0.0f ? Clamp01(component->hp / component->maxHp) : 0.0f;
    const float xpRatio = component && component->xpNext > 0.0f ? Clamp01(component->xp / component->xpNext) : 0.0f;

    ImGui::SetNextWindowPos(ImVec2(viewportX + 14.0f, viewportY + 34.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(300, 128), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.04f, 0.06f, 0.62f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
    ImGui::Begin("##SurvivorsHud", nullptr,
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
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // 准星/提示
    ImGui::GetForegroundDrawList()->AddText(ImVec2(viewportX + 16, viewportY + viewportHeight - 30),
                                            IM_COL32(220, 225, 235, 180), "WASD 移动 · 自动攻击 · Esc 暂停");
}

// ================================================================ 关卡
void BuildSurvivorsLevel(World& world, AssetManager& assets) {
    world.Clear();
    g_enemies.clear();

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

    // 地面：程序化石砖（32x32），1x1 铺满 56x30
    {
        const int size = 32;
        std::vector<u8> pixels(size * size * 4, 0);
        unsigned seed = 1337u;
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                seed = seed * 1664525u + 1013904223u;
                const int noise = static_cast<int>((seed >> 16) & 0x7) - 3;
                int shade = 108 + noise;
                if (x == 0 || y == 0) shade = 74;
                if (x == size - 1 || y == size - 1) shade = 80;
                if ((x % 16 == 0) || (y % 16 == 0)) shade -= 8;
                u8* p = &pixels[(y * size + x) * 4];
                p[0] = static_cast<u8>(Clamp(shade + 4, 0, 255));
                p[1] = static_cast<u8>(Clamp(shade + 3, 0, 255));
                p[2] = static_cast<u8>(Clamp(shade + 8, 0, 255));
                p[3] = 255;
            }
        }
        assets.CreateTextureRGBA("floor_proc", size, size, pixels.data());

        for (int y = -15; y < 15; ++y) {
            for (int x = -28; x < 28; ++x) {
                SpawnSprite(world, "地面", "floor_proc",
                            Vec2(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f), Vec2(1.0f, 1.0f), -10);
            }
        }
    }

    // 玩家
    GameObject* player = SpawnSprite(world, "玩家", "sprites/soldier_idle.png", Vec2(0.0f, 0.0f), Vec2(1.8f, 1.8f), 5);
    auto* animator = player->AddComponent<SpriteAnimatorComponent>();
    animator->fps = 6.0f;
    animator->frameMin = Vec2(0.410f, 0.380f); // 裁掉 100x100 帧里的透明留白
    animator->frameMax = Vec2(0.580f, 0.600f);
    auto* playerComponent = player->AddComponent<SurvivorsPlayerComponent>();
    playerComponent->arenaHalf = Vec2(27.0f, 14.5f);

    LogInfo("2D 幸存者关卡：%zu 个对象", world.ObjectCount());
}


// ================================================================ 反射
void SurvivorsPlayerComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<SurvivorsPlayerComponent>(); };
    t.Field("speed", FieldType::Float, &SurvivorsPlayerComponent::speed, "移动速度", 0.5f, 15.0f);
    t.Field("maxHp", FieldType::Float, &SurvivorsPlayerComponent::maxHp, "最大生命", 1.0f, 999.0f);
    t.Field("attackDamage", FieldType::Float, &SurvivorsPlayerComponent::attackDamage, "攻击力", 1.0f, 200.0f);
    t.Field("attackInterval", FieldType::Float, &SurvivorsPlayerComponent::attackInterval, "攻击间隔", 0.05f, 3.0f);
    t.Field("attackRange", FieldType::Float, &SurvivorsPlayerComponent::attackRange, "攻击范围", 1.0f, 30.0f);
    t.Field("projectileSpeed", FieldType::Float, &SurvivorsPlayerComponent::projectileSpeed, "箭速", 1.0f, 60.0f);
}

void SurvivorsEnemyComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<SurvivorsEnemyComponent>(); };
    t.Field("speed", FieldType::Float, &SurvivorsEnemyComponent::speed, "移动速度", 0.2f, 12.0f);
    t.Field("hp", FieldType::Float, &SurvivorsEnemyComponent::hp, "生命", 1.0f, 500.0f);
    t.Field("damage", FieldType::Float, &SurvivorsEnemyComponent::damage, "伤害", 1.0f, 100.0f);
    t.Field("xpValue", FieldType::Float, &SurvivorsEnemyComponent::xpValue, "经验值", 1.0f, 50.0f);
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
} // namespace erm
