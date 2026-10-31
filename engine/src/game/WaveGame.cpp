// ErMao Engine - 刷怪 Demo 实现
#include "game/WaveGame.h"
#include "core/Log.h"
#include "game/ThirdPersonController.h"
#include "platform/Input.h"
#include "renderer/RenderScene.h"
#include "scene/Components.h"
#include "scene/GameObject.h"
#include "scene/World.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace erm {

namespace {

Vec3 SafeNormalize(const Vec3& v) {
    const float len = Length(v);
    return len > 1e-5f ? v / len : Vec3(0, 0, 0);
}

// 角色/敌人共用的"移动 + 静态几何推离"
void MoveWithSlide(World& world, GameObject* object, const Vec3& delta, float radius, float height) {
    Vec3 position = object->Position() + delta;

    std::vector<std::pair<GameObject*, AABB>> boxes;
    world.CollectMeshAABBs(boxes);
    const float bottom = position.y - height * 0.5f;
    const float top = position.y + height * 0.5f;

    for (const auto& entry : boxes) {
        if (entry.first == object) continue;
        if (entry.first->Parent() == object) continue;
        const AABB& box = entry.second;
        if (top < box.min.y + 0.01f || bottom > box.max.y - 0.01f) continue;

        const float cx = Clamp(position.x, box.min.x, box.max.x);
        const float cz = Clamp(position.z, box.min.z, box.max.z);
        const Vec3  diff(position.x - cx, 0.0f, position.z - cz);
        const float distance = Length(diff);
        if (distance >= radius || distance < 1e-4f) continue;
        position += diff / distance * (radius - distance);
    }
    object->SetPosition(position);
}

} // namespace

// ================================================================ 组件
void HealthComponent::OnUpdate(float dt) {
    if (flash > 0.0f) flash = (std::max)(0.0f, flash - dt * 4.0f);
}

void HealthComponent::ApplyDamage(float amount) {
    if (health <= 0.0f) return;
    health = (std::max)(0.0f, health - amount);
    flash = 1.0f;
}

namespace {
bool g_autoFire = false;
}

void CombatComponent::SetAutoFire(bool enabled) { g_autoFire = enabled; }
bool CombatComponent::AutoFire() { return g_autoFire; }

void CombatComponent::OnUpdate(float dt) {
    m_cooldown = (std::max)(0.0f, m_cooldown - dt);
    if (!m_owner || m_cooldown > 0.0f) return;
    if (g_autoFire) {
        // 自动开火不受输入开关影响（--demo 截图用）
    } else {
        if (!ThirdPersonController::InputEnabled()) return;
        if (!Input::IsMousePressed(0)) return;
    }

    World* world = GetWorld();
    if (!world) return;

    Vec3 forward = m_owner->Forward();
    if (Camera* camera = world->GetActiveCamera()) forward = camera->Forward();
    forward = SafeNormalize(forward);

    const Vec3 origin = m_owner->Position() + Vec3(0, 1.1f, 0) + forward * 0.9f;

    GameObject* projectile = world->CreateGameObject("子弹");
    projectile->SetPosition(origin);
    projectile->SetScale(Vec3(0.16f, 0.16f, 0.16f));
    auto* renderer = projectile->AddComponent<MeshRenderer>();
    renderer->mesh = "sphere";
    renderer->albedo = Vec4(1.0f, 0.85f, 0.35f, 1.0f);
    renderer->emissive = 4.0f;
    renderer->castShadow = false;

    auto* bullet = projectile->AddComponent<ProjectileComponent>();
    bullet->velocity = forward * projectileSpeed;
    bullet->damage = damage;
    bullet->life = projectileLife;

    m_cooldown = fireInterval;
}

void ProjectileComponent::OnUpdate(float dt) {
    if (!m_owner) return;
    World* world = GetWorld();
    if (!world) return;

    life -= dt;
    if (life <= 0.0f) {
        world->DestroyObject(m_owner);
        return;
    }

    const Vec3 position = m_owner->Position();
    m_owner->SetPosition(position + velocity * dt);

    // 命中检测：球-球
    bool hit = false;
    for (GameObject* root : world->RootObjects()) {
        root->ForEach([&](GameObject* object) {
            if (hit || object == m_owner) return;
            auto* health = object->GetComponent<HealthComponent>();
            if (!health || health->isPlayer || health->health <= 0.0f) return;
            const Vec3 toTarget = object->WorldPosition() + Vec3(0, 0.6f, 0) - m_owner->Position();
            if (Length(toTarget) < 0.75f) {
                health->ApplyDamage(damage);
                hit = true;
            }
        });
        if (hit) break;
    }

    if (hit) world->DestroyObject(m_owner);
}

void EnemyComponent::OnUpdate(float dt) {
    if (!m_owner) return;
    World* world = GetWorld();
    if (!world) return;

    auto* health = m_owner->GetComponent<HealthComponent>();
    if (health && health->health <= 0.0f) return;

    GameObject* player = world->FindByName("玩家");
    if (!player) return;

    m_attackTimer = (std::max)(0.0f, m_attackTimer - dt);

    Vec3 toPlayer = player->Position() - m_owner->Position();
    toPlayer.y = 0.0f;
    const float distance = Length(toPlayer);
    const Vec3  direction = SafeNormalize(toPlayer);

    // 面向玩家
    if (LengthSq(direction) > 0.0f) {
        const float targetYaw = std::atan2(-direction.x, -direction.z);
        m_owner->SetRotation(Quat::FromEuler(targetYaw, 0.0f, 0.0f));
    }

    if (distance > attackRange) {
        MoveWithSlide(*world, m_owner, direction * (moveSpeed * dt), 0.42f, 1.7f);
    } else if (m_attackTimer <= 0.0f) {
        if (auto* playerHealth = player->GetComponent<HealthComponent>()) {
            playerHealth->ApplyDamage(attackDamage);
        }
        m_attackTimer = attackInterval;
    }
}

// ================================================================ 波次导演
GameObject* WaveGame::FindPlayer(World& world) const { return world.FindByName("玩家"); }

int WaveGame::CountAlive(World& world) const {
    int alive = 0;
    for (GameObject* root : world.RootObjects()) {
        root->ForEach([&](GameObject* object) {
            auto* health = object->GetComponent<HealthComponent>();
            auto* enemy = object->GetComponent<EnemyComponent>();
            if (health && enemy && health->health > 0.0f) ++alive;
        });
    }
    return alive;
}

void WaveGame::SpawnEnemy(World& world, const Vec3& position) {
    static int counter = 0;
    GameObject* enemy = world.CreateGameObject("敌人_" + std::to_string(counter++));
    enemy->SetPosition(position);
    enemy->SetScale(Vec3(0.8f, 0.9f, 0.8f));

    auto* renderer = enemy->AddComponent<MeshRenderer>();
    renderer->mesh = "capsule";
    renderer->albedo = Vec4(0.78f, 0.24f, 0.28f, 1.0f);
    renderer->roughness = 0.55f;

    auto* health = enemy->AddComponent<HealthComponent>();
    health->maxHealth = 40.0f + static_cast<float>(m_wave) * 10.0f;
    health->health = health->maxHealth;
    health->barWidth = 1.1f;
    health->barOffsetY = 1.55f;

    auto* ai = enemy->AddComponent<EnemyComponent>();
    ai->moveSpeed = 2.5f + static_cast<float>(m_wave) * 0.18f;
    ai->attackDamage = 7.0f + static_cast<float>(m_wave) * 1.2f;
}

void WaveGame::SpawnWave(World& world) {
    ++m_wave;
    const int count = 2 + m_wave;
    for (int i = 0; i < count; ++i) {
        const float angle = static_cast<float>(i) / count * kTwoPi + static_cast<float>(m_wave) * 0.7f;
        const float radius = 13.0f + static_cast<float>(i % 3) * 2.5f;
        const Vec3 position{std::cos(angle) * radius, 1.0f, std::sin(angle) * radius};
        SpawnEnemy(world, position);
    }
    m_nextWaveTimer = 2.5f;
    LogInfo("[刷怪] 第 %d 波：%d 个敌人", m_wave, count);
}

void WaveGame::StartRun(World& world) {
    // 清掉上一局的敌人/子弹
    std::vector<GameObject*> toDestroy;
    for (GameObject* root : world.RootObjects()) {
        root->ForEach([&](GameObject* object) {
            if (object->GetComponent<EnemyComponent>() || object->GetComponent<ProjectileComponent>()) {
                toDestroy.push_back(object);
            }
        });
    }
    for (GameObject* object : toDestroy) world.DestroyObject(object);
    world.ProcessPendingDestruction();

    if (GameObject* player = FindPlayer(world)) {
        player->SetPosition(Vec3(0.0f, 1.0f, 0.0f));
        if (auto* health = player->GetComponent<HealthComponent>()) {
            health->health = health->maxHealth;
            health->flash = 0.0f;
        }
    }

    m_wave = 0;
    m_score = 0;
    m_nextWaveTimer = 0.8f;
    m_state = State::Playing;
    LogInfo("[刷怪] 开始：清空旧敌人，等待第一波");
}

void WaveGame::Reset(World& world) { StartRun(world); }

void WaveGame::Update(World& world, float dt, bool inputEnabled) {
    (void)inputEnabled;

    if (m_state == State::Menu || m_state == State::GameOver) return;

    // Esc 暂停 / 继续
    if (Input::IsKeyPressed(VK_ESCAPE)) {
        m_state = (m_state == State::Playing) ? State::Paused : State::Playing;
    }
    if (m_state != State::Playing) return;

    // 敌人死亡结算
    std::vector<GameObject*> deadEnemies;
    for (GameObject* root : world.RootObjects()) {
        root->ForEach([&](GameObject* object) {
            auto* health = object->GetComponent<HealthComponent>();
            auto* enemy = object->GetComponent<EnemyComponent>();
            if (health && enemy && health->health <= 0.0f) deadEnemies.push_back(object);
        });
    }
    for (GameObject* enemy : deadEnemies) {
        m_score += 100;
        world.DestroyObject(enemy);
    }

    // 玩家死亡
    if (GameObject* player = FindPlayer(world)) {
        if (auto* health = player->GetComponent<HealthComponent>()) {
            if (health->health <= 0.0f) {
                m_state = State::GameOver;
                LogInfo("[刷怪] 游戏结束：得分 %d，波次 %d", m_score, m_wave);
                return;
            }
        }
    }

    // 波次推进
    m_nextWaveTimer -= dt;
    if (m_nextWaveTimer <= 0.0f && CountAlive(world) == 0) {
        SpawnWave(world);
    }
}

void WaveGame::DrawHealthBars(World& world, const Camera& camera, float viewportX, float viewportY,
                              float viewportWidth, float viewportHeight) {
    if (viewportWidth < 8.0f || viewportHeight < 8.0f) return;
    if (m_state != State::Playing) return; // 菜单/暂停时不画血条，避免盖住菜单

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->PushClipRect(ImVec2(viewportX, viewportY), ImVec2(viewportX + viewportWidth, viewportY + viewportHeight),
                       true);

    for (GameObject* root : world.RootObjects()) {
        root->ForEach([&](GameObject* object) {
            auto* health = object->GetComponent<HealthComponent>();
            if (!health || !health->showBar || health->health <= 0.0f) return;

            // 遮挡判定瞄准"角色头部"，不是血条本身：
            // 否则"人被墙挡住、血条露在墙上方"时会画出一条悬空血条（审核 P3-52）
            const Vec3 head = object->WorldPosition() + Vec3(0, health->barOffsetY, 0);
            const Vec3 occluderTarget = object->WorldPosition() + Vec3(0, 0.9f, 0);
            Vec2       screen;
            if (!camera.WorldToScreen(head, Vec2(viewportWidth, viewportHeight), screen)) return;

            const float distance = Length(camera.position - head);
            // 遮挡剔除：相机到血条之间如果撞到几何体就不画（否则会"透视"）
            Ray       visibilityRay{camera.position, Normalize(occluderTarget - camera.position)};
            World::RaycastHit blocker;
            const float headDistance = Length(occluderTarget - camera.position);
            if (world.Raycast(visibilityRay, headDistance - 0.25f, blocker) && blocker.object != object) return;
            const float barWidth = Clamp(1600.0f / (std::max)(distance, 1.0f), 44.0f, 170.0f);
            const float barHeight = 9.0f;
            const float px = viewportX + screen.x;
            const float py = viewportY + screen.y;
            const float ratio = Clamp01(health->health / (health->maxHealth > 0.0f ? health->maxHealth : 1.0f));

            // 底框 + 填充 + 受击白闪
            draw->AddRectFilled(ImVec2(px - barWidth * 0.5f - 1, py - 1), ImVec2(px + barWidth * 0.5f + 1, py + barHeight + 1),
                                IM_COL32(8, 9, 12, 200), 3.0f);
            ImU32 fill = ImGui::ColorConvertFloat4ToU32(
                ImVec4(health->color.x, health->color.y, health->color.z, 1.0f));
            if (health->flash > 0.0f) {
                fill = ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 1));
            }
            draw->AddRectFilled(ImVec2(px - barWidth * 0.5f, py), ImVec2(px - barWidth * 0.5f + barWidth * ratio, py + barHeight),
                                fill, 2.0f);

            if (health->isPlayer) {
                char label[64];
                std::snprintf(label, sizeof(label), "%.0f / %.0f", health->health, health->maxHealth);
                const ImVec2 textSize = ImGui::CalcTextSize(label);
                draw->AddText(ImVec2(px - textSize.x * 0.5f, py - textSize.y - 2), IM_COL32(235, 238, 245, 220), label);
            }
        });
    }

    draw->PopClipRect();
}
bool WaveGame::DrawUI(World& world, float viewportX, float viewportY, float viewportWidth, float viewportHeight,
                      bool viewportHovered) {
    (void)viewportHovered;

    const ImGuiViewport* main = ImGui::GetMainViewport();
    // 菜单以视口为中心；HUD 贴视口左上角（不要盖住编辑器面板）
    const ImVec2 center(viewportX + viewportWidth * 0.5f, viewportY + viewportHeight * 0.5f);

    auto centeredWindow = [&](const char* name, const ImVec2& size) {
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        return ImGui::Begin(name, nullptr,
                            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_NoCollapse);
    };

    if (m_state == State::Menu) {
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Space)) StartRun(world);
        if (centeredWindow("##MainMenu", ImVec2(460, 300))) {
            ImGui::Dummy(ImVec2(0, 6));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.30f, 1.0f));
            ImGui::SetCursorPosX(120);
            ImGui::Text("刷怪生存 Demo");
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::TextWrapped("WASD 移动，Shift 奔跑，空格跳跃，右键拖动转视角，左键射击。");
            ImGui::TextWrapped("每一波敌人会更强；清空当前波次后自动进入下一波。");
            ImGui::Dummy(ImVec2(0, 10));
            if (ImGui::Button("开始游戏", ImVec2(200, 36))) StartRun(world);
            ImGui::SameLine();
            if (ImGui::Button("退出", ImVec2(120, 36))) m_wantsQuit = true;
            ImGui::Dummy(ImVec2(0, 6));
            ImGui::TextDisabled("Enter / 空格 开始 · Esc 暂停 / 继续");
        }
        ImGui::End();
        return true;
    }

    if (m_state == State::Paused) {
        if (centeredWindow("##PauseMenu", ImVec2(360, 220))) {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f), "已暂停");
            ImGui::Separator();
            if (ImGui::Button("继续游戏", ImVec2(-1, 32))) m_state = State::Playing;
            if (ImGui::Button("重新开始", ImVec2(-1, 32))) StartRun(world);
            if (ImGui::Button("返回主菜单", ImVec2(-1, 32))) m_state = State::Menu;
            if (ImGui::Button("退出程序", ImVec2(-1, 32))) m_wantsQuit = true;
        }
        ImGui::End();
        return true;
    }

    if (m_state == State::GameOver) {
        if (centeredWindow("##GameOver", ImVec2(380, 220))) {
            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.32f, 1.0f), "你被击倒了");
            ImGui::Separator();
            ImGui::Text("坚持到第 %d 波", m_wave);
            ImGui::Text("得分：%d", m_score);
            ImGui::Dummy(ImVec2(0, 6));
            if (ImGui::Button("重新开始", ImVec2(-1, 34))) StartRun(world);
            if (ImGui::Button("返回主菜单", ImVec2(-1, 34))) m_state = State::Menu;
            if (ImGui::Button("退出程序", ImVec2(-1, 34))) m_wantsQuit = true;
        }
        ImGui::End();
        return true;
    }

    // ------------------------------------------------ 游戏内 HUD
    ImGui::SetNextWindowPos(ImVec2(viewportX + 14.0f, viewportY + 34.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(280, 118), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.04f, 0.06f, 0.62f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
    ImGui::Begin("##Hud", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar);

    float healthRatio = 1.0f;
    if (GameObject* player = FindPlayer(world)) {
        if (auto* health = player->GetComponent<HealthComponent>()) {
            healthRatio = Clamp01(health->health / health->maxHealth);
        }
    }
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "生命 %.0f%%", healthRatio * 100.0f);
    ImGui::TextColored(ImVec4(0.88f, 0.90f, 0.94f, 1), "%s", buffer);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, healthRatio > 0.3f ? ImVec4(0.35f, 0.85f, 0.45f, 1.0f)
                                                                     : ImVec4(0.90f, 0.25f, 0.28f, 1.0f));
    ImGui::ProgressBar(healthRatio, ImVec2(220, 14));
    ImGui::PopStyleColor();
    ImGui::Text("波次 %d    得分 %d", m_wave, m_score);
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // 准星
    const ImVec2 screenCenter(center);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImU32 crossColor = IM_COL32(255, 255, 255, 160);
    draw->AddLine(ImVec2(screenCenter.x - 9, screenCenter.y), ImVec2(screenCenter.x + 9, screenCenter.y),
                  crossColor, 1.5f);
    draw->AddLine(ImVec2(screenCenter.x, screenCenter.y - 9), ImVec2(screenCenter.x, screenCenter.y + 9),
                  crossColor, 1.5f);

    ImGui::SetNextWindowPos(ImVec2(center.x, viewportY + viewportHeight - 34.0f), ImGuiCond_Always, ImVec2(0.5f, 0));
    ImGui::Begin("##Hint", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextDisabled("左键射击 · WASD 移动 · 空格跳跃 · 右键转视角 · Esc 暂停");
    ImGui::End();

    return false;
}

// ================================================================ 反射
void HealthComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<HealthComponent>(); };
    t.Field("maxHealth", FieldType::Float, &HealthComponent::maxHealth, "最大生命", 1.0f, 1000.0f);
    t.Field("health", FieldType::Float, &HealthComponent::health, "当前生命", 0.0f, 1000.0f);
    t.Field("showBar", FieldType::Bool, &HealthComponent::showBar, "显示血条");
    t.Field("barWidth", FieldType::Float, &HealthComponent::barWidth, "血条宽度", 0.2f, 5.0f);
    t.Field("barOffsetY", FieldType::Float, &HealthComponent::barOffsetY, "血条高度", 0.2f, 5.0f);
    t.Field("isPlayer", FieldType::Bool, &HealthComponent::isPlayer, "是玩家");
}

void CombatComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<CombatComponent>(); };
    t.Field("fireInterval", FieldType::Float, &CombatComponent::fireInterval, "射击间隔", 0.02f, 2.0f);
    t.Field("projectileSpeed", FieldType::Float, &CombatComponent::projectileSpeed, "弹速", 5.0f, 100.0f);
    t.Field("damage", FieldType::Float, &CombatComponent::damage, "伤害", 1.0f, 200.0f);
    t.Field("projectileLife", FieldType::Float, &CombatComponent::projectileLife, "存活时间", 0.2f, 10.0f);
}

void ProjectileComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<ProjectileComponent>(); };
    t.Field("damage", FieldType::Float, &ProjectileComponent::damage, "伤害", 1.0f, 200.0f);
    t.Field("life", FieldType::Float, &ProjectileComponent::life, "剩余寿命", 0.0f, 10.0f);
}

void EnemyComponent::Describe(TypeInfo& t) {
    t.factory = [](GameObject* go) -> Component* { return go->AddComponent<EnemyComponent>(); };
    t.Field("moveSpeed", FieldType::Float, &EnemyComponent::moveSpeed, "移动速度", 0.2f, 12.0f);
    t.Field("attackRange", FieldType::Float, &EnemyComponent::attackRange, "攻击距离", 0.5f, 5.0f);
    t.Field("attackDamage", FieldType::Float, &EnemyComponent::attackDamage, "攻击伤害", 0.5f, 100.0f);
    t.Field("attackInterval", FieldType::Float, &EnemyComponent::attackInterval, "攻击间隔", 0.1f, 5.0f);
}

} // namespace erm
