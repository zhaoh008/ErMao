// ErMao Engine - 2D 幸存者 UI（按 ermao-ui-v2 原型重做）
//
// 设计基准：1600×900 逻辑画布；窗口高度决定整体缩放，宽度不足时逻辑画布变窄。
// 切图面板/按钮/进度条/技能槽全部走九宫格；背景使用 live 战场或 title 主视觉。
#include "game/Survivors.h"

#include "asset/AssetManager.h"
#include "core/FileSystem.h"
#include "core/Log.h"
#include "platform/Audio.h"
#include "rhi/Texture.h"
#include "scene/GameObject.h"
#include "scene/World.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace erm {

namespace {

// ---------------------------------------------------------------- 调色板（取 index.html）
const ImU32 kText = IM_COL32(244, 229, 207, 255);
const ImU32 kTextDim = IM_COL32(194, 175, 189, 255);
const ImU32 kTextMuted = IM_COL32(154, 140, 157, 255);
const ImU32 kGold = IM_COL32(239, 182, 112, 255);
const ImU32 kGoldBright = IM_COL32(255, 210, 154, 255);
const ImU32 kTag = IM_COL32(228, 171, 106, 255);
const ImU32 kMint = IM_COL32(125, 214, 194, 255);
const ImU32 kRed = IM_COL32(255, 141, 144, 255);
const ImU32 kCard = IM_COL32(40, 32, 46, 255);
const ImU32 kCardHover = IM_COL32(55, 39, 51, 255);
const ImU32 kCardBorder = IM_COL32(134, 96, 68, 255);
const ImU32 kCardBorderHover = IM_COL32(246, 188, 116, 255);
const ImU32 kRule = IM_COL32(105, 71, 53, 255);
const ImU32 kPanelInner = IM_COL32(26, 24, 34, 255);
const ImU32 kSlotText = IM_COL32(236, 214, 188, 255);

// ---------------------------------------------------------------- 技能元数据
struct SkillMeta {
    const char* name;
    const char* desc;
};

const SkillMeta kSkills[10] = {
    {"箭矢", "自动攻击最近敌人，散射逐级增加方向数"},
    {"雷霆", "多道落雷，范围和数量随等级扩大"},
    {"回旋刃", "飞刃环绕；对冻结目标伤害提高"},
    {"霜环", "冻结周围敌人，为飞刃创造输出窗口"},
    {"毒雾", "持续伤害；中毒目标受到伤害提高"},
    {"火球", "追踪火球，高等级爆炸范围巨大"},
    {"陨石", "天降陨石，落点范围爆炸"},
    {"黑洞", "吸附周围敌人并持续腐蚀"},
    {"棱镜光束", "旋转光束，穿透整条路径"},
    {"毒爆", "引爆中毒目标，让毒雾连锁扩散"},
};

struct Canvas {
    ImVec2 origin;
    float  scale = 1.0f;
    float  w = 1600.0f;
    float  h = 900.0f;

    ImVec2 P(float x, float y) const { return ImVec2(origin.x + x * scale, origin.y + y * scale); }
    ImVec2 S(float x, float y) const { return ImVec2(x * scale, y * scale); }
    float  V(float v) const { return v * scale; }
};

Canvas MakeCanvas(float vx, float vy, float vw, float vh) {
    Canvas c;
    c.origin = ImVec2(vx, vy);
#ifdef ERMAO_WEB
    // 手机横屏高度通常 360~430 CSS px：允许缩到 0.40，保证 900 设计高度能完整放下。
    c.scale = Clamp(vh / 900.0f, 0.40f, 1.25f);
#else
    c.scale = Clamp(vh / 900.0f, 0.70f, 1.25f);
#endif
    c.w = vw / c.scale;
    c.h = vh / c.scale;
    return c;
}

ImTextureID TexId(Texture* texture) {
    // D3D11 返回 SRV 指针，Web 返回 GLuint；两端都通过 Texture::ImGuiTexId() 暴露。
    return static_cast<ImTextureID>(texture ? texture->ImGuiTexId() : 0);
}

void SetSamplerNearest(ImDrawList* draw) {
    draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest);
}

void SetSamplerLinear(ImDrawList* draw) {
    draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear);
}

ImFont* PickFont(const SurvivorsUIAssets& ui, float size) {
    if (size >= 60.0f && ui.display) return ui.display;
    if (size >= 30.0f && ui.large) return ui.large;
    if (ui.body) return ui.body;
    return ImGui::GetFont();
}

ImVec2 MeasureText(const SurvivorsUIAssets& ui, float size, const char* text) {
    ImFont* font = PickFont(ui, size);
    if (!font || !text) return ImVec2(0, 0);
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
}

void DrawText(ImDrawList* draw, const SurvivorsUIAssets& ui, const Canvas& c, float size, float x, float y,
              ImU32 color, const char* text) {
    if (!text || !*text) return;
    ImFont* font = PickFont(ui, size);
    if (!font) return;
    draw->AddText(font, c.V(size), c.P(x, y), color, text);
}

void DrawTextRight(ImDrawList* draw, const SurvivorsUIAssets& ui, const Canvas& c, float size, float rightX,
                   float y, ImU32 color, const char* text) {
    const ImVec2 measured = MeasureText(ui, c.V(size), text);
    DrawText(draw, ui, c, size, rightX - measured.x / c.scale, y, color, text);
}

void DrawTextCentered(ImDrawList* draw, const SurvivorsUIAssets& ui, const Canvas& c, float size, float centerX,
                      float y, ImU32 color, const char* text) {
    const ImVec2 measured = MeasureText(ui, c.V(size), text);
    DrawText(draw, ui, c, size, centerX - measured.x / c.scale * 0.5f, y, color, text);
}

void DrawTextShadow(ImDrawList* draw, const SurvivorsUIAssets& ui, const Canvas& c, float size, float x, float y,
                    ImU32 color, ImU32 shadow1, ImU32 shadow2, const char* text) {
    DrawText(draw, ui, c, size, x, y + 5.0f, shadow1, text);
    DrawText(draw, ui, c, size, x, y + 9.0f, shadow2, text);
    DrawText(draw, ui, c, size, x, y, color, text);
}

size_t Utf8CharLength(unsigned char lead) {
    if ((lead & 0x80u) == 0) return 1;
    if ((lead & 0xE0u) == 0xC0u) return 2;
    if ((lead & 0xF0u) == 0xE0u) return 3;
    if ((lead & 0xF8u) == 0xF0u) return 4;
    return 1;
}

// 中文/符号均可断行；返回逻辑像素行高。
float DrawWrapped(ImDrawList* draw, const SurvivorsUIAssets& ui, const Canvas& c, float size, float x, float y,
                  float wrapWidth, ImU32 color, const char* text, float lineGap = 1.45f) {
    if (!text || !*text) return 0.0f;
    ImFont* font = PickFont(ui, size);
    if (!font) return 0.0f;
    const float maxPx = c.V(wrapWidth);
    float       cursorY = y;
    std::string line;
    std::string candidate;
    auto emit = [&]() {
        if (line.empty()) return;
        draw->AddText(font, c.V(size), c.P(x, cursorY), color, line.c_str());
        cursorY += size * lineGap;
        line.clear();
    };

    for (const char* p = text; *p;) {
        if (*p == '\n') {
            emit();
            ++p;
            continue;
        }
        const size_t length = Utf8CharLength(static_cast<unsigned char>(*p));
        candidate = line;
        candidate.append(p, length);
        const ImVec2 measured = font->CalcTextSizeA(c.V(size), FLT_MAX, 0.0f, candidate.c_str());
        if (!line.empty() && measured.x > maxPx) {
            emit();
            line.assign(p, length);
        } else {
            line.swap(candidate);
        }
        p += length;
    }
    emit();
    return cursorY - y;
}

void DrawImage(ImDrawList* draw, const Canvas& c, Texture* texture, float x, float y, float w, float h,
               ImU32 tint = IM_COL32_WHITE) {
    if (!texture) return;
    SetSamplerNearest(draw);
    draw->AddImage(TexId(texture), c.P(x, y), c.P(x + w, y + h), ImVec2(0, 0), ImVec2(1, 1), tint);
    SetSamplerLinear(draw);
}

void DrawNineSlice(ImDrawList* draw, const Canvas& c, Texture* texture, float x, float y, float w, float h,
                   float borderPx, ImU32 tint = IM_COL32_WHITE) {
    if (w <= 0.0f || h <= 0.0f) return;
    if (!texture) {
        draw->AddRectFilled(c.P(x, y), c.P(x + w, y + h), kPanelInner, c.V(4.0f));
        draw->AddRect(c.P(x, y), c.P(x + w, y + h), kCardBorder, c.V(2.0f));
        return;
    }
    const float tw = static_cast<float>(texture->Width());
    const float th = static_cast<float>(texture->Height());
    if (tw <= 0.0f || th <= 0.0f) return;
    float border = (std::min)(borderPx, (std::min)(tw, th) * 0.5f);
    float left = border * c.scale;
    float right = border * c.scale;
    if (left + right > c.V(w)) {
        left = c.V(w) * 0.5f;
        right = c.V(w) - left;
    }
    float top = border * c.scale;
    float bottom = border * c.scale;
    if (top + bottom > c.V(h)) {
        top = c.V(h) * 0.5f;
        bottom = c.V(h) - top;
    }
    const float x0 = c.P(x, y).x;
    const float y0 = c.P(x, y).y;
    const float x1 = x0 + left;
    const float x2 = x0 + c.V(w) - right;
    const float x3 = x0 + c.V(w);
    const float y1 = y0 + top;
    const float y2 = y0 + c.V(h) - bottom;
    const float y3 = y0 + c.V(h);
    const float u0 = 0.0f;
    const float u1 = border / tw;
    const float u2 = 1.0f - border / tw;
    const float u3 = 1.0f;
    const float v0 = 0.0f;
    const float v1 = border / th;
    const float v2 = 1.0f - border / th;
    const float v3 = 1.0f;
    const ImTextureID id = TexId(texture);
    SetSamplerNearest(draw);
    draw->AddImage(id, ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(u0, v0), ImVec2(u1, v1), tint);
    draw->AddImage(id, ImVec2(x1, y0), ImVec2(x2, y1), ImVec2(u1, v0), ImVec2(u2, v1), tint);
    draw->AddImage(id, ImVec2(x2, y0), ImVec2(x3, y1), ImVec2(u2, v0), ImVec2(u3, v1), tint);
    draw->AddImage(id, ImVec2(x0, y1), ImVec2(x1, y2), ImVec2(u0, v1), ImVec2(u1, v2), tint);
    draw->AddImage(id, ImVec2(x1, y1), ImVec2(x2, y2), ImVec2(u1, v1), ImVec2(u2, v2), tint);
    draw->AddImage(id, ImVec2(x2, y1), ImVec2(x3, y2), ImVec2(u2, v1), ImVec2(u3, v2), tint);
    draw->AddImage(id, ImVec2(x0, y2), ImVec2(x1, y3), ImVec2(u0, v2), ImVec2(u1, v3), tint);
    draw->AddImage(id, ImVec2(x1, y2), ImVec2(x2, y3), ImVec2(u1, v2), ImVec2(u2, v3), tint);
    draw->AddImage(id, ImVec2(x2, y2), ImVec2(x3, y3), ImVec2(u2, v2), ImVec2(u3, v3), tint);
    SetSamplerLinear(draw);
}

void DrawPanel(ImDrawList* draw, const SurvivorsUIAssets& ui, const Canvas& c, float x, float y, float w, float h) {
    draw->AddRectFilled(c.P(x + 7.0f, y + 9.0f), c.P(x + w + 7.0f, y + h + 9.0f), IM_COL32(0, 0, 0, 100), c.V(7.0f));
    DrawNineSlice(draw, c, ui.panel, x, y, w, h, 20.0f);
}

void DrawCard(ImDrawList* draw, const Canvas& c, float x, float y, float w, float h, bool hovered) {
    draw->AddRectFilled(c.P(x, y), c.P(x + w, y + h), hovered ? kCardHover : kCard, c.V(2.0f));
    draw->AddRect(c.P(x, y), c.P(x + w, y + h), hovered ? kCardBorderHover : kCardBorder, c.V(2.0f),
                  hovered ? c.V(2.0f) : c.V(1.5f));
}

void DrawRule(ImDrawList* draw, const Canvas& c, float x, float y, float w) {
    draw->AddLine(c.P(x, y), c.P(x + w, y), kRule, (std::max)(1.0f, c.V(1.0f)));
}

void DrawBar(ImDrawList* draw, const SurvivorsUIAssets& ui, const Canvas& c, float x, float y, float w, float h,
             float ratio, Texture* fillTexture) {
    DrawNineSlice(draw, c, ui.barTrack, x, y, w, h, 10.0f);
    ratio = Clamp01(ratio);
    if (ratio <= 0.0f || !fillTexture) return;
    const float insetX = 5.0f;
    const float insetY = 4.0f;
    const float innerW = (std::max)(0.0f, w - insetX * 2.0f);
    const float innerH = (std::max)(0.0f, h - insetY * 2.0f);
    const float fillW = innerW * ratio;
    if (fillW <= 0.0f || innerH <= 0.0f) return;
    SetSamplerLinear(draw);
    draw->AddImage(TexId(fillTexture), c.P(x + insetX, y + insetY), c.P(x + insetX + fillW, y + insetY + innerH),
                   ImVec2(0.0f, 0.0f), ImVec2(ratio, 1.0f), IM_COL32_WHITE);
}

bool DrawButton(ImDrawList* draw, const SurvivorsUIAssets& ui, const Canvas& c, const char* id, const char* label,
                float x, float y, float w, float h, bool primary, bool disabled, float fontSize = 20.0f) {
    ImGui::SetCursorScreenPos(c.P(x, y));
    const bool pressed = ImGui::InvisibleButton(id, c.S(w, h));
    const bool hovered = ImGui::IsItemHovered() && !disabled;
    const bool active = ImGui::IsItemActive() && !disabled;

    Texture* texture = disabled ? ui.buttonDisabled : (active ? ui.buttonPressed : (hovered ? ui.buttonHover : (primary ? ui.buttonPrimary : ui.buttonNormal)));
    if (!texture) texture = primary ? ui.buttonPrimary : ui.buttonNormal;
    DrawNineSlice(draw, c, texture, x, y, w, h, 12.0f);

    const ImU32 color = disabled ? kTextMuted : kText;
    const float textY = y + (h - fontSize) * 0.5f - 1.0f;
    DrawTextCentered(draw, ui, c, fontSize, x + w * 0.5f, textY, color, label);
    return pressed && !disabled;
}

void DrawStatColumns(ImDrawList* draw, const SurvivorsUIAssets& ui, const Canvas& c, float x, float y, float w,
                     const std::string& time, const std::string& kills, const std::string& level) {
    const float columnW = w / 3.0f;
    const char* labels[3] = {"存活时间", "击杀", "等级"};
    const std::string* values[3] = {&time, &kills, &level};
    for (int i = 0; i < 3; ++i) {
        const float cx = x + columnW * (static_cast<float>(i) + 0.5f);
        DrawTextCentered(draw, ui, c, 28.0f, cx, y, kGold, values[i]->c_str());
        DrawTextCentered(draw, ui, c, 14.0f, cx, y + 38.0f, kTextDim, labels[i]);
    }
}

std::string FormatClock(float seconds) {
    const int total = (std::max)(0, static_cast<int>(seconds));
    char      buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", total / 60, total % 60);
    return buffer;
}

int SkillLevel(const SurvivorsPlayerComponent& player, int skill) { return player.SkillLevel(skill); }

int SkillSubLevel(const SurvivorsPlayerComponent& player, int skill, int sub) {
    if (skill == 0) {
        switch (sub) {
            case 0: return player.arrowSpreadLevel;
            case 1: return player.arrowSpeedLevel;
            case 2: return player.arrowDamageLevel;
            case 3: return player.arrowRangeLevel;
            default: return 0;
        }
    }
    return SkillLevel(player, skill);
}

std::string SkillStatLine(const SurvivorsPlayerComponent& player, int skill) {
    char buffer[192];
    switch (skill) {
        case 0:
            std::snprintf(buffer, sizeof(buffer), "%s · 伤害 %.0f · 攻速 +%.0f%% · 范围 %.1f",
                          player.ArrowDirections() > 1
                              ? (std::to_string(player.ArrowDirections()) + " 方向 0.65x").c_str()
                              : "单发锁定",
                          player.ArrowDamage(), player.ArrowSpeedBonus() * 100.0f, player.ArrowRange());
            break;
        case 1:
            std::snprintf(buffer, sizeof(buffer), "伤害 %.0f · 目标 %d · 连锁 %d · CD %.1fs", player.LightningDamage(),
                          player.LightningTargets(), player.LightningChains(), player.LightningCooldown());
            break;
        case 2:
            std::snprintf(buffer, sizeof(buffer), "伤害 %.0f · %d 刃 · 半径 %.1f · 冻结目标 x1.75", player.BladeDamage(),
                          player.BladeCount(), player.BladeRadius());
            break;
        case 3:
            std::snprintf(buffer, sizeof(buffer), "伤害 %.0f · 半径 %.1f · 冻结 %.1fs", player.FrostDamage(),
                          player.FrostRadius(), player.FrostFreezeDuration());
            break;
        case 4:
            std::snprintf(buffer, sizeof(buffer), "DPS %.0f · 半径 %.1f · 中毒易伤 +15%%", player.PoisonDps(),
                          player.PoisonRadius());
            break;
        case 5:
            std::snprintf(buffer, sizeof(buffer), "伤害 %.0f · 爆炸 %.1f · CD %.1fs", player.FireballDamage(),
                          player.FireballExplosionRadius(), player.FireballCooldown());
            break;
        case 6:
            std::snprintf(buffer, sizeof(buffer), "伤害 %.0f · %d 发 · 范围 %.1f · CD %.1fs", player.MeteorDamage(),
                          player.MeteorCount(), player.MeteorRadius(), player.MeteorCooldown());
            break;
        case 7:
            std::snprintf(buffer, sizeof(buffer), "DPS %.0f · 半径 %.1f · %.1fs · CD %.1fs",
                          player.SingularityDps(), player.SingularityRadius(), player.SingularityDuration(),
                          player.SingularityCooldown());
            break;
        case 8:
            std::snprintf(buffer, sizeof(buffer), "伤害 %.0f · %d 束 · 长度 %.1f", player.PrismDamage(),
                          player.PrismBeamCount(), player.PrismBeamLength());
            break;
        case 9:
            std::snprintf(buffer, sizeof(buffer), "伤害 %.0f · 半径 %.1f · CD %.1fs", player.PlagueDamage(),
                          player.PlagueRadius(), player.PlagueCooldown());
            break;
        default: buffer[0] = '\0'; break;
    }
    return buffer;
}

struct LevelCardInfo {
    std::string tag;
    std::string name;
    std::string level;
    std::string desc;
    std::string effect;
};

LevelCardInfo MakeLevelCardInfo(const SurvivorsPlayerComponent& player, const SurvivorsLevelUpChoice& choice) {
    LevelCardInfo info;
    if (choice.skill == 100) {
        info.tag = "补给 · 技能已满";
        if (choice.sub == 0) {
            info.name = "生命恢复";
            info.level = "恢复 30%";
            info.desc = "所有携带技能都已达到上限，回复一部分生命。";
            info.effect = "生命 +30% 与 10 点";
        } else if (choice.sub == 1) {
            info.name = "生命上限";
            info.level = "上限 +8";
            info.desc = "所有携带技能都已达到上限，提高生命上限并回复同等生命。";
            info.effect = "最大生命 +8";
        } else {
            info.name = "完全恢复";
            info.level = "回满";
            info.desc = "所有携带技能都已达到上限，将生命回满。";
            info.effect = "生命回满";
        }
        return info;
    }
    if (choice.skill == 0) {
        const int current = SkillSubLevel(player, 0, choice.sub);
        if (choice.sub == 0) {
            info.tag = "箭矢 · 强化";
            info.name = "箭矢散射";
            info.level = "Lv." + std::to_string(current) + " → Lv." + std::to_string(current + 1);
            info.desc = "每级增加散射方向数（2 / 4 / 6 / 8 / 12），单箭伤害 0.65x。";
            info.effect = std::to_string(player.ArrowDirections()) + " → " +
                          std::to_string(kSurvivorsArrowDirectionCounts[Clamp(current + 1, 0, kSurvivorsMaxArrowSpreadLevel)]) +
                          " 方向";
            return info;
        }
        info.tag = "箭矢 · 强化";
        switch (choice.sub) {
            case 1: info.name = "箭矢攻速"; break;
            case 2: info.name = "箭矢攻击"; break;
            default: info.name = "箭矢范围"; break;
        }
        info.level = "Lv." + std::to_string(current) + " → Lv." + std::to_string(current + 1);
        info.desc = choice.sub == 1 ? "缩短自动射击的间隔。" : (choice.sub == 2 ? "提高每支箭的伤害。" : "扩大索敌与箭矢飞行距离。");

        SurvivorsPlayerComponent after = player;
        after.ApplyLevelUpUpgrade(choice.skill, choice.sub);
        after.level = player.level + 1;
        after.RefreshDerivedStats();
        char buffer[128];
        if (choice.sub == 1) {
            const float gain = player.attackInterval > 1e-5f ? (1.0f - after.attackInterval / player.attackInterval) * 100.0f : 0.0f;
            std::snprintf(buffer, sizeof(buffer), "攻速 +%.0f%%", gain);
        } else if (choice.sub == 2) {
            std::snprintf(buffer, sizeof(buffer), "伤害 %.0f → %.0f", player.ArrowDamage(), after.ArrowDamage());
        } else {
            std::snprintf(buffer, sizeof(buffer), "范围 %.1f → %.1f", player.ArrowRange(), after.ArrowRange());
        }
        info.effect = buffer;
        return info;
    }

    info.tag = "技能 · 强化";
    info.name = kSkills[choice.skill].name;
    info.level = "Lv." + std::to_string(SkillLevel(player, choice.skill)) + " → Lv." +
                 std::to_string(SkillLevel(player, choice.skill) + 1);
    info.desc = kSkills[choice.skill].desc;

    SurvivorsPlayerComponent after = player;
    after.ApplyLevelUpUpgrade(choice.skill, choice.sub);
    after.level = player.level + 1;
    after.RefreshDerivedStats();
    char buffer[160];
    switch (choice.skill) {
        case 1:
            std::snprintf(buffer, sizeof(buffer), "伤害 %.0f → %.0f · CD %.1fs → %.1fs", player.LightningDamage(),
                          after.LightningDamage(), player.LightningCooldown(), after.LightningCooldown());
            break;
        case 2:
            std::snprintf(buffer, sizeof(buffer), "伤害 %.0f → %.0f · 刃数 %d → %d", player.BladeDamage(),
                          after.BladeDamage(), player.BladeCount(), after.BladeCount());
            break;
        case 3:
            std::snprintf(buffer, sizeof(buffer), "伤害 %.0f → %.0f · 半径 %.1f → %.1f · 冻结 %.1fs → %.1fs",
                          player.FrostDamage(), after.FrostDamage(), player.FrostRadius(), after.FrostRadius(),
                          player.FrostFreezeDuration(), after.FrostFreezeDuration());
            break;
        case 4:
            std::snprintf(buffer, sizeof(buffer), "DPS %.0f → %.0f · 半径 %.1f → %.1f", player.PoisonDps(),
                          after.PoisonDps(), player.PoisonRadius(), after.PoisonRadius());
            break;
        case 5:
            std::snprintf(buffer, sizeof(buffer), "伤害 %.0f → %.0f · 爆炸 %.1f → %.1f", player.FireballDamage(),
                          after.FireballDamage(), player.FireballExplosionRadius(),
                          after.FireballExplosionRadius());
            break;
        case 6:
            std::snprintf(buffer, sizeof(buffer), "伤害 %.0f → %.0f · 范围 %.1f → %.1f", player.MeteorDamage(),
                          after.MeteorDamage(), player.MeteorRadius(), after.MeteorRadius());
            break;
        case 7:
            std::snprintf(buffer, sizeof(buffer), "DPS %.0f → %.0f · 半径 %.1f → %.1f", player.SingularityDps(),
                          after.SingularityDps(), player.SingularityRadius(), after.SingularityRadius());
            break;
        case 8:
            std::snprintf(buffer, sizeof(buffer), "伤害 %.0f → %.0f · 束数 %d → %d", player.PrismDamage(),
                          after.PrismDamage(), player.PrismBeamCount(), after.PrismBeamCount());
            break;
        case 9:
            std::snprintf(buffer, sizeof(buffer), "伤害 %.0f → %.0f · 半径 %.1f → %.1f", player.PlagueDamage(),
                          after.PlagueDamage(), player.PlagueRadius(), after.PlagueRadius());
            break;
        default: buffer[0] = '\0'; break;
    }
    info.effect = buffer;
    return info;
}

SurvivorsPlayerComponent* PlayerOf(World& world) {
    if (GameObject* object = world.FindByName("玩家")) return object->GetComponent<SurvivorsPlayerComponent>();
    return nullptr;
}

void DrawTitleTag(ImDrawList* draw, const SurvivorsUIAssets& ui, const Canvas& c, float x, float y, const char* text,
                  bool centered, float centerX) {
    if (centered) {
        DrawTextCentered(draw, ui, c, 14.0f, centerX, y, kTag, text);
    } else {
        DrawText(draw, ui, c, 14.0f, x, y, kTag, text);
    }
}

void DrawSectionTitle(ImDrawList* draw, const SurvivorsUIAssets& ui, const Canvas& c, float x, float y, float w,
                      const char* tag, const char* title, const char* subtitle, bool centered) {
    if (centered) {
        const float cx = x + w * 0.5f;
        DrawTitleTag(draw, ui, c, x, y, tag, true, cx);
        DrawTextCentered(draw, ui, c, 34.0f, cx, y + 22.0f, kText, title);
        if (subtitle && *subtitle) {
            DrawTextCentered(draw, ui, c, 16.0f, cx, y + 66.0f, kTextDim, subtitle);
        }
    } else {
        DrawTitleTag(draw, ui, c, x, y, tag, false, x);
        DrawText(draw, ui, c, 34.0f, x, y + 22.0f, kText, title);
        if (subtitle && *subtitle) DrawText(draw, ui, c, 16.0f, x, y + 66.0f, kTextDim, subtitle);
    }
}

// 背景：封面式裁切，保证主视觉不变形
void DrawCoverImage(ImDrawList* draw, const Canvas& c, Texture* texture) {
    if (!texture) {
        draw->AddRectFilled(c.P(0, 0), c.P(c.w, c.h), IM_COL32(16, 13, 20, 255));
        return;
    }
    const float texW = static_cast<float>(texture->Width());
    const float texH = static_cast<float>(texture->Height());
    const float viewAspect = c.w / c.h;
    const float imageAspect = texW / texH;
    float       u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    if (imageAspect > viewAspect) {
        const float visible = viewAspect / imageAspect;
        u0 = (1.0f - visible) * 0.5f;
        u1 = 1.0f - u0;
    } else if (imageAspect < viewAspect) {
        const float visible = imageAspect / viewAspect;
        v0 = (1.0f - visible) * 0.5f;
        v1 = 1.0f - v0;
    }
    SetSamplerNearest(draw);
    draw->AddImage(TexId(texture), c.P(0, 0), c.P(c.w, c.h), ImVec2(u0, v0), ImVec2(u1, v1));
    SetSamplerLinear(draw);
}

void DrawDim(ImDrawList* draw, const Canvas& c, int alpha) {
    draw->AddRectFilled(c.P(0, 0), c.P(c.w, c.h), IM_COL32(11, 8, 19, alpha));
}

bool DrawSlider(ImDrawList* draw, const SurvivorsUIAssets& ui, const Canvas& c, const char* id, float x, float y,
                float w, float h, float& value, bool* released) {
    ImGui::SetCursorScreenPos(c.P(x, y));
    ImGui::InvisibleButton(id, c.S(w, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const float trackY = y + h * 0.5f;
    const float trackH = 8.0f;
    draw->AddRectFilled(c.P(x, trackY - trackH * 0.5f), c.P(x + w, trackY + trackH * 0.5f),
                        IM_COL32(13, 11, 18, 255), c.V(4.0f));
    draw->AddRect(c.P(x, trackY - trackH * 0.5f), c.P(x + w, trackY + trackH * 0.5f), kCardBorder, c.V(4.0f),
                  c.V(1.0f));
    const float knobX = x + w * Clamp01(value);
    draw->AddRectFilled(c.P(x, trackY - trackH * 0.5f), c.P(knobX, trackY + trackH * 0.5f), kGold, c.V(4.0f));
    draw->AddCircleFilled(c.P(knobX, trackY), c.V(active ? 9.0f : 7.0f),
                          active ? kGoldBright : (hovered ? kGold : IM_COL32(222, 156, 91, 255)), 18);
    draw->AddCircle(c.P(knobX, trackY), c.V(active ? 9.0f : 7.0f), IM_COL32(24, 16, 26, 180), 18, c.V(1.0f));

    bool changed = false;
    if (active) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float  next = Clamp01((mouse.x - c.P(x, 0).x) / c.V(w));
        if (std::fabs(next - value) > 1e-4f) {
            value = next;
            changed = true;
        }
    }
    if (released) *released = ImGui::IsItemDeactivated();
    return changed;
}

// ---------------------------------------------------------------- 抽奖动画工具
ImU32 ToImColor(const Vec4& color, float alphaScale = 1.0f) {
    const int r = static_cast<int>(Clamp01(color.x) * 255.0f);
    const int g = static_cast<int>(Clamp01(color.y) * 255.0f);
    const int b = static_cast<int>(Clamp01(color.z) * 255.0f);
    const int a = static_cast<int>(Clamp01(color.w * alphaScale) * 255.0f);
    return IM_COL32(r, g, b, a);
}

ImU32 ToImColorFromU32(ImU32 color, float alphaScale) {
    const int a = static_cast<int>(((color >> IM_COL32_A_SHIFT) & 0xFF) * Clamp01(alphaScale));
    return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
}

float Hash01(int n) {
    float x = std::sin(static_cast<float>(n) * 12.9898f + 78.233f) * 43758.5453f;
    return x - std::floor(x);
}

float EaseOutCubic(float t) {
    t = Clamp01(t);
    return 1.0f - std::pow(1.0f - t, 3.0f);
}

float EaseOutBack(float t) {
    t = Clamp01(t);
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
}

// 以中心为原点、可旋转地画一张精灵表里的单帧（用于宝箱开启序列帧）。
void DrawSheetFrameRotated(ImDrawList* draw, const Canvas& c, Texture* texture, int columns, int rows, int frame,
                           float cx, float cy, float width, float height, ImU32 tint, float rotation) {
    if (!texture || columns <= 0 || rows <= 0) return;
    frame = Clamp(frame, 0, columns * rows - 1);
    const float u0 = static_cast<float>(frame % columns) / static_cast<float>(columns);
    const float v0 = static_cast<float>(frame / columns) / static_cast<float>(rows);
    const float u1 = u0 + 1.0f / static_cast<float>(columns);
    const float v1 = v0 + 1.0f / static_cast<float>(rows);
    const float cosR = std::cos(rotation);
    const float sinR = std::sin(rotation);
    const Vec2  right(cosR * width * 0.5f, sinR * width * 0.5f);
    const Vec2  up(-sinR * height * 0.5f, cosR * height * 0.5f);
    const ImVec2 p0 = c.P(cx - right.x - up.x, cy - right.y - up.y);
    const ImVec2 p1 = c.P(cx + right.x - up.x, cy + right.y - up.y);
    const ImVec2 p2 = c.P(cx + right.x + up.x, cy + right.y + up.y);
    const ImVec2 p3 = c.P(cx - right.x + up.x, cy - right.y + up.y);
    SetSamplerNearest(draw);
    draw->AddImageQuad(TexId(texture), p0, p1, p2, p3, ImVec2(u0, v0), ImVec2(u1, v0), ImVec2(u1, v1),
                       ImVec2(u0, v1), tint);
    SetSamplerLinear(draw);
}

// 爆炸序列帧（8×8，前 40 帧是火球爆炸主体）。
void DrawExplosionFrame(ImDrawList* draw, const Canvas& c, Texture* texture, int frame, float cx, float cy,
                        float size, ImU32 tint) {
    if (!texture) return;
    constexpr int columns = 8;
    constexpr int rows = 8;
    frame = Clamp(frame, 0, columns * rows - 1);
    const float u0 = static_cast<float>(frame % columns) / static_cast<float>(columns);
    const float v0 = static_cast<float>(frame / columns) / static_cast<float>(rows);
    const ImVec2 p0 = c.P(cx - size * 0.5f, cy - size * 0.5f);
    const ImVec2 p1 = c.P(cx + size * 0.5f, cy + size * 0.5f);
    SetSamplerLinear(draw);
    draw->AddImage(TexId(texture), p0, p1, ImVec2(u0, v0), ImVec2(u0 + 1.0f / columns, v0 + 1.0f / rows), tint);
}

} // namespace

// ================================================================ 资源加载
bool SurvivorsUIAssets::Load(AssetManager& assets, ImFont* bodyFont, ImFont* largeFont, ImFont* displayFont) {
    body = bodyFont;
    large = largeFont;
    display = displayFont;

    panel = assets.GetTextureAsset("sprites/ui/panel.png");
    slot = assets.GetTextureAsset("sprites/ui/slot.png");
    buttonNormal = assets.GetTextureAsset("sprites/ui/button_normal.png");
    buttonHover = assets.GetTextureAsset("sprites/ui/button_hover.png");
    buttonPressed = assets.GetTextureAsset("sprites/ui/button_pressed.png");
    buttonDisabled = assets.GetTextureAsset("sprites/ui/button_disabled.png");
    buttonPrimary = assets.GetTextureAsset("sprites/ui/button_primary.png");
    barTrack = assets.GetTextureAsset("sprites/ui/bar_track.png");
    barHealthFill = assets.GetTextureAsset("sprites/ui/bar_health_fill.png");
    barXpFill = assets.GetTextureAsset("sprites/ui/bar_xp_fill.png");
    crest = assets.GetTextureAsset("sprites/ui/crest.png");
    iconHealth = assets.GetTextureAsset("sprites/ui/icon_health.png");
    iconXp = assets.GetTextureAsset("sprites/ui/icon_xp.png");
    titleBackground = assets.GetTextureAsset("sprites/ui/title.png");

    static const char* kIconPaths[10] = {"sprites/ui/icon_arrow.png",      "sprites/ui/icon_lightning.png",
                                         "sprites/ui/icon_blade.png",      "sprites/ui/icon_frost.png",
                                         "sprites/ui/icon_poison.png",     "sprites/ui/icon_fireball.png",
                                         "sprites/ui/icon_meteor.png",     "sprites/ui/icon_singularity.png",
                                         "sprites/ui/icon_prism.png",      "sprites/ui/icon_plague.png"};
    for (int i = 0; i < 10; ++i) icon[i] = assets.GetTextureAsset(kIconPaths[i]);

    // 关卡里程序化生成的地面，用于出战准备的地图卡片
    floor = assets.GetTexture("floor_big");
    // 增益宝箱图标/开箱序列帧（均为 CC0；图标取自 Kenney Tiny Dungeon，开箱帧来自 OpenGameArt）
    chestSheet = assets.GetTextureAsset("sprites/chest_sheet.png");
    chestTiles = assets.GetTextureAsset("sprites/kenney_tilemap.png");

    // 技能特效：CC0 序列帧/精灵表（下载来源见 engine/assets/vfx/LICENSE-VFX.txt）
    fxArcane = assets.GetTextureAsset("vfx/arcane_strip.png");
    fxWater = assets.GetTextureAsset("vfx/water_strip.png");
    fxFireball = assets.GetTextureAsset("vfx/fireball.png");
    fxExplosion = assets.GetTextureAsset("vfx/explosion.png");
    fxPoison = assets.GetTextureAsset("vfx/poison_mist.png");

    const bool ok = panel && slot && buttonNormal && buttonPrimary && barTrack && barHealthFill && barXpFill &&
                    titleBackground && chestSheet && chestTiles && body;
    if (!ok) LogWarn("[2D] UI 部分切图/字体缺失，界面会退回纯色绘制");
    return ok;
}

// ================================================================ 导演：主绘制入口
void SurvivorsDirector::DrawUI(World& world, float viewportX, float viewportY, float viewportWidth,
                               float viewportHeight) {
    EnsureMusic();
    if (viewportWidth < 8.0f || viewportHeight < 8.0f) return;

    Canvas c = MakeCanvas(viewportX, viewportY, viewportWidth, viewportHeight);

    ImGui::SetNextWindowPos(ImVec2(viewportX, viewportY));
    ImGui::SetNextWindowSize(ImVec2(viewportWidth, viewportHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::Begin("##SurvivorsUI", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->PushClipRect(ImVec2(viewportX, viewportY), ImVec2(viewportX + viewportWidth, viewportY + viewportHeight),
                       true);

    if (m_showSettings) {
        if (m_settingsReturn == State::Paused) {
            DrawHudScreen(world, c.origin.x, c.origin.y, viewportWidth, viewportHeight, false);
        }
        DrawDim(draw, c, 170);
        DrawSettingsScreen(c.origin.x, c.origin.y, viewportWidth, viewportHeight);
    } else {
        switch (m_state) {
            case State::Menu: DrawMenuScreen(world, c.origin.x, c.origin.y, viewportWidth, viewportHeight); break;
            case State::Prepare: {
                DrawDim(draw, c, 130);
                DrawPrepareScreen(world, c.origin.x, c.origin.y, viewportWidth, viewportHeight);
                break;
            }
            case State::Playing: {
                DrawHudScreen(world, c.origin.x, c.origin.y, viewportWidth, viewportHeight, true);
                break;
            }
            case State::Paused: {
                DrawHudScreen(world, c.origin.x, c.origin.y, viewportWidth, viewportHeight, false);
                DrawDim(draw, c, 175);
                DrawPauseScreen(world, c.origin.x, c.origin.y, viewportWidth, viewportHeight);
                break;
            }
            case State::LevelUp: {
                DrawHudScreen(world, c.origin.x, c.origin.y, viewportWidth, viewportHeight, false);
                DrawDim(draw, c, 180);
                DrawLevelUpScreen(world, c.origin.x, c.origin.y, viewportWidth, viewportHeight);
                break;
            }
            case State::Gacha: {
                DrawHudScreen(world, c.origin.x, c.origin.y, viewportWidth, viewportHeight, false);
                DrawGachaScreen(world, c.origin.x, c.origin.y, viewportWidth, viewportHeight);
                break;
            }
            case State::GameOver:
                DrawResultScreen(world, c.origin.x, c.origin.y, viewportWidth, viewportHeight);
                break;
            case State::Codex: {
                DrawDim(draw, c, 185);
                DrawCodexScreen(c.origin.x, c.origin.y, viewportWidth, viewportHeight);
                break;
            }
            default: break;
        }
    }

    draw->PopClipRect();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // Toast 放在最上层；计时器与旧的拾取提示共用。
    m_toastTimer = (std::max)(0.0f, m_toastTimer - ImGui::GetIO().DeltaTime);
    std::string toast = m_toastTimer > 0.0f ? m_toastText : std::string();
    if (toast.empty()) {
        if (const SurvivorsPlayerComponent* player = PlayerOf(world)) {
            if (player->pickupToastTimer > 0.0f) toast = player->pickupToast;
        }
    }
    if (!toast.empty() && m_state != State::LevelUp && m_state != State::Gacha) {
        const float timer = m_toastTimer > 0.0f ? m_toastTimer : 2.0f;
        DrawToast(viewportX, viewportY, viewportWidth, viewportHeight, toast, timer);
    }
}

// ================================================================ 主菜单
void SurvivorsDirector::DrawMenuScreen(World& world, float vx, float vy, float vw, float vh) {
    Canvas c = MakeCanvas(vx, vy, vw, vh);
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    DrawCoverImage(draw, c, m_ui.titleBackground);
    draw->AddRectFilled(c.P(0, 0), c.P(c.w, c.h), IM_COL32(16, 11, 23, 70));
    draw->AddRectFilledMultiColor(c.P(0, 0), c.P(c.w, c.h), IM_COL32(16, 11, 23, 245), IM_COL32(18, 13, 24, 160),
                                  IM_COL32(18, 13, 24, 10), IM_COL32(18, 13, 24, 10));

    const float brandX = Clamp(c.w * 0.07f, 40.0f, 110.0f);
    const float contentH = 610.0f;
    const float top = (std::max)(24.0f, (c.h - contentH) * 0.5f);
    float       y = top;

    if (m_ui.crest) DrawImage(draw, c, m_ui.crest, brandX, y, 120.0f, 76.0f);
    y += 88.0f;
    DrawText(draw, m_ui, c, 14.0f, brandX + 2.0f, y, kTag, "地牢生存 · 自动战斗");
    y += 24.0f;
    DrawTextShadow(draw, m_ui, c, 96.0f, brandX, y, kGoldBright, IM_COL32(87, 46, 50, 255),
                   IM_COL32(24, 16, 26, 255), "二毛幸存者");
    y += 122.0f;
    DrawText(draw, m_ui, c, 18.0f, brandX + 2.0f, y, kText, "躲避怪潮，拾取经验，在地牢里活下去。");
    y += 36.0f;

    const float stackW = 305.0f;
    const bool  hasSave = HasSave();
    if (DrawButton(draw, m_ui, c, "##menu_start", "开始游戏", brandX, y, stackW, 52.0f, true, false, 20.0f)) {
        PlaySfx("click", 0.6f);
        m_playerClass = kSurvivorsDefaultClass;
        m_state = State::Prepare;
        m_pauseNotice.clear();
        return;
    }
    y += 61.0f;
    if (DrawButton(draw, m_ui, c, "##menu_continue", "继续游戏", brandX, y, stackW, 52.0f, false, !hasSave,
                   20.0f)) {
        PlaySfx("click", 0.6f);
        if (LoadGame(world)) {
            PlaySfx("click", 0.6f);
        }
    }
    y += 61.0f;
    if (DrawButton(draw, m_ui, c, "##menu_codex", "技能图鉴", brandX, y, stackW, 52.0f, false, false, 20.0f)) {
        PlaySfx("click", 0.6f);
        m_state = State::Codex;
    }
    y += 61.0f;
    if (DrawButton(draw, m_ui, c, "##menu_settings", "设置", brandX, y, stackW, 52.0f, false, false, 20.0f)) {
        PlaySfx("click", 0.6f);
        m_settingsReturn = State::Menu;
        m_showSettings = true;
    }
    y += 61.0f;
    if (DrawButton(draw, m_ui, c, "##menu_quit", "退出程序", brandX, y, stackW, 46.0f, false, false, 18.0f)) {
        PlaySfx("click", 0.6f);
        m_wantsQuit = true;
    }
    y += 58.0f;
    DrawText(draw, m_ui, c, 14.0f, brandX + 2.0f, y,
             hasSave ? kTextDim : kTextMuted,
             hasSave ? "检测到一局未完成的挑战" : "暂无可继续的存档");
}


// ================================================================ 战斗 HUD
namespace {

void DrawSurvivorsMinimap(ImDrawList* draw, const SurvivorsUIAssets& ui, const Canvas& c, World& world,
                          float time) {
    const SurvivorsMinimapSnapshot snapshot = BuildSurvivorsMinimapSnapshot(world);
    const float panelW = 238.0f;
    const float panelH = 158.0f;
    const float panelX = c.w - panelW - 24.0f;
    const float panelY = c.h - panelH - 24.0f;
    DrawPanel(draw, ui, c, panelX, panelY, panelW, panelH);
    DrawText(draw, ui, c, 14.0f, panelX + 14.0f, panelY + 9.0f, kGold, "地图");
    DrawTextRight(draw, ui, c, 11.0f, panelX + panelW - 14.0f, panelY + 12.0f, kTextMuted, "宝箱 / 掉落 / 怪");

    const float mapX = panelX + 12.0f;
    const float mapY = panelY + 32.0f;
    const float mapW = panelW - 24.0f;
    const float mapH = panelH - 44.0f;
    draw->AddRectFilled(c.P(mapX, mapY), c.P(mapX + mapW, mapY + mapH), IM_COL32(15, 18, 26, 230), c.V(4.0f));
    draw->AddRect(c.P(mapX, mapY), c.P(mapX + mapW, mapY + mapH), IM_COL32(74, 70, 58, 255), c.V(4.0f),
                  c.V(1.0f));

    const Vec2 half = snapshot.arenaHalf;
    auto toMap = [&](const Vec2& position) {
        const float u = Clamp01((position.x + half.x) / (half.x * 2.0f));
        const float v = Clamp01((position.y + half.y) / (half.y * 2.0f));
        return c.P(mapX + u * mapW, mapY + (1.0f - v) * mapH);
    };

    for (const SurvivorsMinimapRect& obstacle : snapshot.obstacles) {
        const ImVec2 min = toMap(obstacle.center - obstacle.half);
        const ImVec2 max = toMap(obstacle.center + obstacle.half);
        draw->AddRectFilled(ImVec2((std::min)(min.x, max.x), (std::min)(min.y, max.y)),
                            ImVec2((std::max)(min.x, max.x), (std::max)(min.y, max.y)),
                            IM_COL32(73, 79, 88, 210), c.V(1.0f));
    }

    // 最近宝箱的方向/距离，用于屏幕上方罗盘。
    float nearestDistance = 1e9f;
    Vec2  nearestDirection(1.0f, 0.0f);
    for (const SurvivorsMinimapMarker& marker : snapshot.markers) {
        if (marker.kind == 2) {
            const ImVec2 p = toMap(marker.position);
            draw->AddCircleFilled(p, c.V(1.7f), IM_COL32(235, 92, 82, 170), 8);
            continue;
        }
        if (marker.kind == 1) {
            draw->AddCircleFilled(toMap(marker.position), c.V(2.2f), IM_COL32(95, 230, 140, 210), 8);
            continue;
        }
        const ImVec2 p = toMap(marker.position);
        const float pulse = 0.5f + 0.5f * std::sin(time * 5.0f + marker.position.x);
        draw->AddCircleFilled(p, c.V(5.5f + pulse * 2.0f), IM_COL32(255, 210, 70, 55), 12);
        draw->AddCircleFilled(p, c.V(3.4f), IM_COL32(255, 220, 90, 255), 12);
        draw->AddLine(ImVec2(p.x - 5.0f, p.y), ImVec2(p.x + 5.0f, p.y), IM_COL32(255, 240, 170, 220), 1.0f);
        draw->AddLine(ImVec2(p.x, p.y - 5.0f), ImVec2(p.x, p.y + 5.0f), IM_COL32(255, 240, 170, 220), 1.0f);

        const Vec2 toBuff = marker.position - snapshot.player;
        const float distance = Length(toBuff);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            const float length = Length(toBuff);
            nearestDirection = length > 0.001f ? toBuff / length : Vec2(1.0f, 0.0f);
        }
    }

    // 玩家：金色三角 + 朝向线
    const ImVec2 playerPoint = toMap(snapshot.player);
    draw->AddCircleFilled(playerPoint, c.V(4.0f), IM_COL32(255, 255, 255, 245), 12);
    draw->AddCircle(playerPoint, c.V(6.0f), IM_COL32(120, 220, 255, 230), 12, c.V(1.0f));

    // 顶部罗盘：指出最近抽奖宝箱的方向与距离
    if (nearestDistance < 1e8f) {
        const float cx = c.w * 0.5f;
        const float cy = 92.0f;
        const float sx = nearestDirection.x;
        const float sy = -nearestDirection.y;
        const ImVec2 tip(cx + sx * 13.0f, cy + sy * 13.0f);
        const ImVec2 left(cx - sx * 3.0f - sy * 6.0f, cy - sy * 3.0f + sx * 6.0f);
        const ImVec2 right(cx - sx * 3.0f + sy * 6.0f, cy - sy * 3.0f - sx * 6.0f);
        draw->AddTriangleFilled(tip, left, right, IM_COL32(255, 215, 80, 240));
        char text[64];
        std::snprintf(text, sizeof(text), "最近增益 %.1fm", nearestDistance);
        DrawText(draw, ui, c, 12.0f, cx + 22.0f, cy - 8.0f, kGold, text);
    }
}

} // namespace

void SurvivorsDirector::DrawHudScreen(World& world, float vx, float vy, float vw, float vh, bool interactive) {
    Canvas      c = MakeCanvas(vx, vy, vw, vh);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    auto*       player = PlayerOf(world);

    const float hp = player ? player->hp : 0.0f;
    const float maxHp = player ? player->maxHp : 100.0f;
    const float xp = player ? player->xp : 0.0f;
    const float xpNext = player ? player->xpNext : 12.0f;
    const int   level = player ? player->level : 1;
    const float hpRatio = maxHp > 0.0f ? Clamp01(hp / maxHp) : 0.0f;
    const float xpRatio = xpNext > 0.0f ? Clamp01(xp / xpNext) : 0.0f;

    // 左上双进度条
    DrawPanel(draw, m_ui, c, 24.0f, 24.0f, 310.0f, 122.0f);
    DrawText(draw, m_ui, c, 17.0f, 42.0f, 40.0f, kRed, "生命");
    char value[64];
    std::snprintf(value, sizeof(value), "%.0f / %.0f", hp, maxHp);
    DrawTextRight(draw, m_ui, c, 16.0f, 316.0f, 41.0f, kText, value);
    DrawBar(draw, m_ui, c, 42.0f, 63.0f, 274.0f, 22.0f, hpRatio, m_ui.barHealthFill);

    DrawText(draw, m_ui, c, 17.0f, 42.0f, 91.0f, kMint, "经验");
    std::snprintf(value, sizeof(value), "%.0f / %.0f", xp, xpNext);
    DrawTextRight(draw, m_ui, c, 16.0f, 316.0f, 92.0f, kText, value);
    DrawBar(draw, m_ui, c, 42.0f, 112.0f, 274.0f, 20.0f, xpRatio, m_ui.barXpFill);

    // 顶中：等级 / 计时 / 击杀
    const float timerW = 342.0f;
    const float timerX = c.w * 0.5f - timerW * 0.5f;
    DrawPanel(draw, m_ui, c, timerX, 20.0f, timerW, 56.0f);
    std::snprintf(value, sizeof(value), "等级 %d", level);
    DrawText(draw, m_ui, c, 15.0f, timerX + 20.0f, 38.0f, kTextDim, value);
    const std::string clock = FormatClock(m_time);
    DrawTextCentered(draw, m_ui, c, 30.0f, c.w * 0.5f, 30.0f, kGoldBright, clock.c_str());
    std::snprintf(value, sizeof(value), "击杀 %d", m_kills);
    DrawTextRight(draw, m_ui, c, 15.0f, timerX + timerW - 20.0f, 38.0f, kTextDim, value);

    // 右上暂停
    const float pauseW = 136.0f;
    const float pauseX = c.w - 24.0f - pauseW;
    if (interactive) {
        if (DrawButton(draw, m_ui, c, "##hud_pause", "Esc 暂停", pauseX, 20.0f, pauseW, 44.0f, false, false, 17.0f)) {
            TogglePause();
            return;
        }
    } else {
        DrawNineSlice(draw, c, m_ui.buttonNormal, pauseX, 20.0f, pauseW, 44.0f, 12.0f);
        DrawTextCentered(draw, m_ui, c, 17.0f, pauseX + pauseW * 0.5f, 33.0f, kText, "Esc 暂停");
    }

    // 底部技能槽：只显示本局已拥有的技能（职业基础 1 个 + 最多 4 个掉落）
    int slotSkills[kSurvivorsMaxLoadout] = {-1, -1, -1, -1, -1};
    int slotCount = 0;
    if (player) {
        for (int skill = 0; skill < kSurvivorsSkillCount && slotCount < kSurvivorsMaxLoadout; ++skill) {
            if (player->HasSkill(skill)) slotSkills[slotCount++] = skill;
        }
    }
    const float slotSize = 70.0f;
    const float gap = 5.0f;
    const float totalW = slotSize * static_cast<float>(slotCount) + gap * static_cast<float>((std::max)(0, slotCount - 1));
    const float slotY = c.h - 24.0f - slotSize;
    const float slotX = c.w * 0.5f - totalW * 0.5f;
    int         hoveredSkill = -1;
    for (int slot = 0; slot < slotCount; ++slot) {
        const int   skill = slotSkills[slot];
        const float x = slotX + (slotSize + gap) * static_cast<float>(slot);
        if (interactive) {
            ImGui::SetCursorScreenPos(c.P(x, slotY));
            const bool pressed = ImGui::InvisibleButton((std::string("##skill") + std::to_string(skill)).c_str(),
                                                        c.S(slotSize, slotSize));
            if (ImGui::IsItemHovered()) hoveredSkill = skill;
            if (pressed) m_selectedSkill = (m_selectedSkill == skill) ? -1 : skill;
        }
        DrawNineSlice(draw, c, m_ui.slot, x, slotY, slotSize, slotSize, 12.0f);
        if (m_ui.icon[skill]) DrawImage(draw, c, m_ui.icon[skill], x + 14.0f, slotY + 7.0f, 42.0f, 42.0f);
        const int skillLevel = player ? SkillLevel(*player, skill) : 0;
        char      slotLabel[32];
        std::snprintf(slotLabel, sizeof(slotLabel), "Lv.%d", skillLevel);
        DrawTextCentered(draw, m_ui, c, 11.0f, x + slotSize * 0.5f, slotY + 54.0f, kSlotText, slotLabel);
    }
    if (interactive && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) m_selectedSkill = -1;

    // 技能说明：悬停优先，点击可钉住
    const int shownSkill = (interactive && hoveredSkill >= 0) ? hoveredSkill : (interactive ? m_selectedSkill : -1);
    if (shownSkill >= 0 && shownSkill < kSurvivorsSkillCount && player) {
        const float tipW = 342.0f;
        const float tipH = 106.0f;
        const float tipX = c.w * 0.5f - tipW * 0.5f;
        const float tipY = slotY - tipH - 14.0f;
        DrawPanel(draw, m_ui, c, tipX, tipY, tipW, tipH);
        DrawTextCentered(draw, m_ui, c, 17.0f, c.w * 0.5f, tipY + 16.0f, kText, kSkills[shownSkill].name);
        char levelText[32];
        std::snprintf(levelText, sizeof(levelText), "Lv.%d", SkillLevel(*player, shownSkill));
        DrawTextRight(draw, m_ui, c, 13.0f, tipX + tipW - 22.0f, tipY + 18.0f, kGold, levelText);
        DrawTextCentered(draw, m_ui, c, 14.0f, c.w * 0.5f, tipY + 42.0f, kTextDim, kSkills[shownSkill].desc);
        const std::string stat = SkillStatLine(*player, shownSkill);
        DrawTextCentered(draw, m_ui, c, 12.5f, c.w * 0.5f, tipY + 64.0f, kMint, stat.c_str());
        DrawTextCentered(draw, m_ui, c, 11.5f, c.w * 0.5f, tipY + 86.0f, kTextMuted, "点击技能槽钉住说明");
    }

    DrawSurvivorsMinimap(draw, m_ui, c, world, m_time);
}

// ================================================================ 升级三选一
void SurvivorsDirector::DrawLevelUpScreen(World& world, float vx, float vy, float vw, float vh) {
    Canvas      c = MakeCanvas(vx, vy, vw, vh);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    auto*       player = PlayerOf(world);
    if (!player) return;

    const float panelW = 860.0f;
    const float panelH = 470.0f;
    const float panelX = c.w * 0.5f - panelW * 0.5f;
    const float panelY = c.h * 0.5f - panelH * 0.5f;
    DrawPanel(draw, m_ui, c, panelX, panelY, panelW, panelH);

    char subtitle[96];
    std::snprintf(subtitle, sizeof(subtitle), "等级 %d → %d · 选择后继续战斗", player->level, player->level + 1);
    DrawSectionTitle(draw, m_ui, c, panelX + 30.0f, panelY + 26.0f, panelW - 60.0f, "经验已满", "选择一项强化",
                     subtitle, true);

    const float cardW = (panelW - 60.0f - 28.0f) / 3.0f;
    const float cardH = 286.0f;
    const float cardY = panelY + 140.0f;
    int         picked = -1;
    for (int i = 0; i < 3; ++i) {
        const float cardX = panelX + 30.0f + (cardW + 14.0f) * static_cast<float>(i);
        ImGui::SetCursorScreenPos(c.P(cardX, cardY));
        const bool pressed = ImGui::InvisibleButton((std::string("##levelcard") + std::to_string(i)).c_str(),
                                                     c.S(cardW, cardH));
        const bool hovered = ImGui::IsItemHovered();
        if (pressed) picked = i;

        DrawCard(draw, c, cardX, cardY, cardW, cardH, hovered);
        const LevelCardInfo info = MakeLevelCardInfo(*player, m_levelChoices[i]);
        char tag[64];
        std::snprintf(tag, sizeof(tag), "[%d]  %s", i + 1, info.tag.c_str());
        DrawText(draw, m_ui, c, 13.0f, cardX + 16.0f, cardY + 14.0f, kTag, tag);
        const int cardSkill = m_levelChoices[i].skill;
        if (cardSkill >= 0 && cardSkill < kSurvivorsSkillCount && m_ui.icon[cardSkill]) {
            DrawImage(draw, c, m_ui.icon[cardSkill], cardX + 16.0f, cardY + 42.0f, 64.0f, 64.0f);
        } else if (m_ui.iconHealth) {
            DrawImage(draw, c, m_ui.iconHealth, cardX + 16.0f, cardY + 42.0f, 64.0f, 64.0f);
        }
        DrawText(draw, m_ui, c, 23.0f, cardX + 16.0f, cardY + 116.0f, kText, info.name.c_str());
        DrawText(draw, m_ui, c, 15.0f, cardX + 16.0f, cardY + 150.0f, kGold, info.level.c_str());
        DrawWrapped(draw, m_ui, c, 14.0f, cardX + 16.0f, cardY + 181.0f, cardW - 32.0f, kTextDim,
                    info.desc.c_str(), 1.45f);
        DrawWrapped(draw, m_ui, c, 13.5f, cardX + 16.0f, cardY + 248.0f, cardW - 32.0f, kMint,
                    info.effect.c_str(), 1.35f);
    }

    if (picked < 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_1, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad1, false)) picked = 0;
        else if (ImGui::IsKeyPressed(ImGuiKey_2, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad2, false)) picked = 1;
        else if (ImGui::IsKeyPressed(ImGuiKey_3, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad3, false)) picked = 2;
    }
    if (picked >= 0) ApplyLevelUpChoice(world, picked);
}

// ================================================================ 暂停
void SurvivorsDirector::DrawPauseScreen(World& world, float vx, float vy, float vw, float vh) {
    Canvas      c = MakeCanvas(vx, vy, vw, vh);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    auto*       player = PlayerOf(world);

    const float panelW = 500.0f;
    const float panelH = 520.0f;
    const float panelX = c.w * 0.5f - panelW * 0.5f;
    const float panelY = c.h * 0.5f - panelH * 0.5f;
    DrawPanel(draw, m_ui, c, panelX, panelY, panelW, panelH);
    DrawSectionTitle(draw, m_ui, c, panelX + 30.0f, panelY + 26.0f, panelW - 60.0f, "战斗暂停", "休整片刻", nullptr,
                     true);

    DrawStatColumns(draw, m_ui, c, panelX + 30.0f, panelY + 122.0f, panelW - 60.0f, FormatClock(m_time),
                    std::to_string(m_kills), std::to_string(player ? player->level : 1));

    const float buttonW = panelW - 80.0f;
    const float buttonX = panelX + 40.0f;
    float       buttonY = panelY + 206.0f;
    if (DrawButton(draw, m_ui, c, "##pause_resume", "继续战斗", buttonX, buttonY, buttonW, 46.0f, true, false,
                   19.0f)) {
        m_pauseNotice.clear();
        m_confirmExit = false;
        m_state = State::Playing;
        return;
    }
    buttonY += 56.0f;
    if (DrawButton(draw, m_ui, c, "##pause_settings", "设置", buttonX, buttonY, buttonW, 46.0f, false, false,
                   19.0f)) {
        m_pauseNotice.clear();
        m_confirmExit = false;
        m_settingsReturn = State::Paused;
        m_showSettings = true;
        return;
    }
    buttonY += 56.0f;
    if (DrawButton(draw, m_ui, c, "##pause_save_exit", "保存并返回主菜单", buttonX, buttonY, buttonW, 46.0f,
                   false, false, 19.0f)) {
        if (SaveGame(world)) {
            m_state = State::Menu;
            m_showSettings = false;
            ShowToast("游戏已保存");
            return;
        }
        m_confirmExit = false;
    }
    buttonY += 56.0f;
    if (DrawButton(draw, m_ui, c, "##pause_exit", m_confirmExit ? "确认放弃并返回" : "返回主菜单", buttonX, buttonY,
                   buttonW, 46.0f, false, false, 19.0f)) {
        if (!m_confirmExit) {
            m_confirmExit = true;
            m_pauseNotice = "再次点击“确认放弃并返回”，将放弃本局未保存的进度。";
        } else {
            m_pauseNotice.clear();
            m_confirmExit = false;
            m_state = State::Menu;
            return;
        }
    }

    if (!m_pauseNotice.empty()) {
        DrawWrapped(draw, m_ui, c, 13.0f, panelX + 40.0f, panelY + 444.0f, panelW - 80.0f,
                    m_pauseNotice.find("失败") != std::string::npos ? kRed : kGold, m_pauseNotice.c_str(), 1.4f);
    }
    DrawTextCentered(draw, m_ui, c, 12.0f, c.w * 0.5f, panelY + panelH - 22.0f, kTextMuted,
                     "Esc 继续战斗 · 暂停时玩法与计时冻结");
}

// ================================================================ 增益宝箱抽奖
void SurvivorsDirector::DrawGachaScreen(World& world, float vx, float vy, float vw, float vh) {
    (void)world;
    Canvas      c = MakeCanvas(vx, vy, vw, vh);
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    const float t = m_gachaTime;
    const float burst = kSurvivorsGachaBurst;
    const float reveal = kSurvivorsGachaReveal;
    const int   rarity = Clamp(m_gachaRarity, 0, 2);
    const Vec4  rarityColor = SurvivorsBuffRarityColor(rarity);
    const ImU32 rarityU32 = ToImColor(rarityColor, 1.0f);
    const int   buffIcon = SurvivorsBuffIconForEffect(m_gachaEffect);
    Texture*    iconTexture = buffIcon >= 0 ? m_ui.icon[buffIcon] : m_ui.iconHealth;

    const float cx = c.w * 0.5f;
    const float cy = c.h * 0.50f;
    const float fade = Clamp01(t / 0.28f);

    // 背景压暗 + 上下渐变，把注意力集中到中央。
    draw->AddRectFilled(c.P(0, 0), c.P(c.w, c.h), IM_COL32(7, 5, 12, static_cast<int>(225.0f * fade)));
    draw->AddRectFilledMultiColor(c.P(0, 0), c.P(c.w, c.h * 0.30f), IM_COL32(4, 3, 8, 150),
                                  IM_COL32(4, 3, 8, 150), IM_COL32(4, 3, 8, 0), IM_COL32(4, 3, 8, 0));
    draw->AddRectFilledMultiColor(c.P(0, c.h * 0.70f), c.P(c.w, c.h), IM_COL32(4, 3, 8, 0),
                                  IM_COL32(4, 3, 8, 0), IM_COL32(4, 3, 8, 150), IM_COL32(4, 3, 8, 150));

    // 中央光晕：层层同心圆做出柔光。
    const float glow = 0.35f + 0.65f * Clamp01(t / 0.85f);
    for (int i = 12; i >= 1; --i) {
        const float radius = 58.0f + static_cast<float>(i) * 24.0f;
        const float alpha = glow * (0.020f + 0.018f * (1.0f - static_cast<float>(i) / 12.0f));
        draw->AddCircleFilled(c.P(cx, cy), c.V(radius), ToImColor(rarityColor, alpha), 48);
    }

    // 旋转光束：抖动阶段逐渐变长，光爆后更亮。
    if (t > 0.16f) {
        const float rayGrow = Clamp01((t - 0.16f) / 0.70f);
        const float rayAlpha = (0.10f + 0.12f * rayGrow) * (t > reveal ? 0.65f : 1.0f);
        const float rayRot = t * (t > burst ? 0.85f : 0.55f);
        constexpr int rayCount = 24;
        for (int i = 0; i < rayCount; ++i) {
            const float angle = rayRot + static_cast<float>(i) * kTwoPi / static_cast<float>(rayCount);
            const float length = (150.0f + 150.0f * rayGrow) * (0.75f + 0.5f * Hash01(i + 3));
            const float width = 0.020f + 0.035f * Hash01(i + 17);
            const ImVec2 p0 = c.P(cx + std::cos(angle) * 48.0f, cy + std::sin(angle) * 48.0f);
            const ImVec2 p1 = c.P(cx + std::cos(angle - width) * length, cy + std::sin(angle - width) * length);
            const ImVec2 p2 = c.P(cx + std::cos(angle + width) * length, cy + std::sin(angle + width) * length);
            draw->AddTriangleFilled(p0, p1, p2, ToImColor(rarityColor, rayAlpha));
        }
    }

    // 漂浮粒子：开箱前围绕宝箱，光爆后向外飞散并淡出。
    const float burstProgress = (std::max)(0.0f, t - burst);
    for (int i = 0; i < 38; ++i) {
        const float phase = Hash01(i * 7 + 1) * kTwoPi;
        const float speed = 0.7f + 1.5f * Hash01(i * 5 + 9);
        float radius = 0.0f;
        float alpha = 0.0f;
        if (t < burst) {
            radius = 95.0f + 120.0f * Hash01(i + 23);
            alpha = 0.25f + 0.55f * Hash01(i + 41);
        } else {
            const float spread = Clamp01(burstProgress / 1.1f);
            radius = 110.0f + spread * (240.0f + 180.0f * Hash01(i + 23));
            alpha = (1.0f - spread) * (0.45f + 0.55f * Hash01(i + 41));
        }
        const float angle = phase + t * speed * (t < burst ? 1.1f : 2.0f);
        const float px = cx + std::cos(angle) * radius;
        const float py = cy + std::sin(angle) * radius * 0.72f;
        const float size = 1.0f + 3.2f * Hash01(i + 61);
        const ImU32 color = (i % 3 == 0) ? kGoldBright : rarityU32;
        draw->AddCircleFilled(c.P(px, py), c.V(size), ToImColorFromU32(color, alpha), 8);
        if (i % 5 == 0) {
            draw->AddLine(c.P(px - size * 3.0f, py), c.P(px + size * 3.0f, py), ToImColorFromU32(color, alpha * 0.8f),
                          1.0f);
            draw->AddLine(c.P(px, py - size * 3.0f), c.P(px, py + size * 3.0f), ToImColorFromU32(color, alpha * 0.8f),
                          1.0f);
        }
    }

    // 光爆震屏（只作用于宝箱和爆炸，不晃卡片）。
    float shakeX = 0.0f;
    float shakeY = 0.0f;
    if (t > burst) {
        const float decay = (std::max)(0.0f, 0.30f - (t - burst)) / 0.30f;
        const float amp = 26.0f * decay;
        shakeX = std::sin(t * 137.0f) * amp;
        shakeY = std::cos(t * 113.0f) * amp;
    }
    const float chestX = cx + shakeX;
    const float chestY = cy + shakeY;

    // 宝箱：先关闭 + 抖动，然后在 burst 时刻逐帧打开。
    if (t < burst) {
        const float enter = Clamp01(t / 0.38f);
        const float chestScale = 0.70f + 0.30f * EaseOutBack(enter);
        const float charge = Clamp01((t - 0.34f) / (burst - 0.34f));
        const float tilt = std::sin(t * 31.0f) * 0.05f * charge;
        draw->AddCircle(c.P(chestX, chestY), c.V(105.0f + 22.0f * charge),
                        ToImColor(rarityColor, 0.20f + 0.18f * charge), 48, c.V(2.0f));
        draw->AddCircle(c.P(chestX, chestY), c.V(142.0f + 30.0f * charge),
                        ToImColor(rarityColor, 0.08f + 0.10f * charge), 48, c.V(1.2f));
        DrawSheetFrameRotated(draw, c, m_ui.chestTiles, 12, 11, 7 * 12 + 7, chestX, chestY, 190.0f * chestScale,
                              190.0f * chestScale, IM_COL32_WHITE, tilt);
    } else {
        const float openTime = t - burst;

        // 光爆闪光放在宝箱下面，用多层低透明同心圆而不是一块实心圆，避免盖住宝箱和爆炸。
        if (openTime < 0.30f) {
            const float flash = 1.0f - openTime / 0.30f;
            for (int i = 6; i >= 1; --i) {
                const float radius = (90.0f + static_cast<float>(i) * 38.0f) * (0.75f + 0.25f * (1.0f - flash));
                draw->AddCircleFilled(c.P(cx, cy), c.V(radius),
                                      IM_COL32(255, 238, 205, static_cast<int>(16.0f * flash)), 48);
            }
            draw->AddCircle(c.P(cx, cy), c.V(70.0f + 160.0f * (1.0f - flash)),
                            IM_COL32(255, 246, 220, static_cast<int>(110.0f * flash)), 48, c.V(3.0f));
        }

        // 金色冲击波：两圈错开的扩散光环。
        if (openTime < 0.55f) {
            for (int ring = 0; ring < 2; ++ring) {
                const float delay = static_cast<float>(ring) * 0.10f;
                if (openTime < delay) continue;
                const float ringT = Clamp01((openTime - delay) / 0.48f);
                const float radius = 96.0f + 235.0f * EaseOutCubic(ringT);
                const int   alpha = static_cast<int>(210.0f * (1.0f - ringT));
                draw->AddCircle(c.P(chestX, chestY), c.V(radius), ToImColor(rarityColor, alpha / 255.0f), 64,
                                c.V(4.0f * (1.0f - ringT) + 1.0f));
                draw->AddCircle(c.P(chestX, chestY), c.V(radius * 0.91f), IM_COL32(255, 245, 220, alpha / 3), 64,
                                c.V(1.5f));
            }
        }

        // 小幅火焰残影垫在宝箱后面，避免大块烟雾糊住画面。
        const float explosionT = Clamp01(openTime / 0.50f);
        const int   explosionFrame = 8 + static_cast<int>(explosionT * 31.0f);
        DrawExplosionFrame(draw, c, m_ui.fxExplosion, explosionFrame, chestX, chestY, 250.0f + 70.0f * explosionT,
                           IM_COL32(255, 196, 120, static_cast<int>(190.0f * (1.0f - 0.55f * explosionT))));

        const int   frame = openTime < 0.07f ? 1 : (openTime < 0.14f ? 2 : 3);
        const float openAlpha = 1.0f - 0.72f * Clamp01((openTime - 0.30f) / 0.90f);
        DrawSheetFrameRotated(draw, c, m_ui.chestSheet, 2, 2, frame, chestX, chestY, 205.0f, 205.0f,
                              ToImColor(Vec4(1, 1, 1, 1), openAlpha), 0.0f);
    }

    // 顶部标题：开箱期间提示，翻牌后让位给卡面。
    if (t < reveal) {
        DrawTextCentered(draw, m_ui, c, 30.0f, cx, cy - 245.0f, kGoldBright, "增益宝箱开启");
        char subtitle[64];
        std::snprintf(subtitle, sizeof(subtitle), "%s增益 · 正在抽取", SurvivorsBuffRarityName(rarity));
        DrawTextCentered(draw, m_ui, c, 15.0f, cx, cy - 198.0f, ToImColor(rarityColor, 1.0f), subtitle);
    }

    // 翻牌：奖励卡从 0.7 倍弹到 1 倍，带稀有度光边与图标。
    if (t >= reveal) {
        const float p = Clamp01((t - reveal) / 0.34f);
        float       scale = EaseOutBack(p);
        scale = (std::max)(0.05f, scale);

        constexpr float baseW = 600.0f;
        constexpr float baseH = 344.0f;
        const float     baseX = cx - baseW * 0.5f;
        const float     baseY = cy - baseH * 0.5f;
        auto PX = [&](float x) { return cx + (x - cx) * scale; };
        auto PY = [&](float y) { return cy + (y - cy) * scale; };
        auto TS = [&](float v) { return v * scale; };

        const ImVec2 cardMin = c.P(PX(baseX), PY(baseY));
        const ImVec2 cardMax = c.P(PX(baseX + baseW), PY(baseY + baseH));

        draw->AddRectFilled(c.P(PX(baseX) - 8.0f, PY(baseY) - 8.0f),
                            c.P(PX(baseX + baseW) + 8.0f, PY(baseY + baseH) + 8.0f), ToImColor(rarityColor, 0.18f),
                            c.V(14.0f));
        draw->AddRectFilled(cardMin, cardMax, IM_COL32(27, 21, 33, 248), c.V(10.0f * scale));
        draw->AddRectFilledMultiColor(cardMin, cardMax, ToImColor(rarityColor, 0.20f), ToImColor(rarityColor, 0.10f),
                                      IM_COL32(10, 8, 15, 0), IM_COL32(10, 8, 15, 0));
        draw->AddRect(cardMin, cardMax, rarityU32, c.V(10.0f * scale), 0, c.V(2.2f * scale));

        // 四角装饰线
        const float corner = TS(22.0f);
        const float inset = TS(10.0f);
        const ImU32 cornerColor = ToImColor(rarityColor, 0.85f);
        draw->AddLine(c.P(PX(baseX) + inset, PY(baseY) + inset), c.P(PX(baseX) + inset + corner, PY(baseY) + inset),
                      cornerColor, c.V(2.0f));
        draw->AddLine(c.P(PX(baseX) + inset, PY(baseY) + inset), c.P(PX(baseX) + inset, PY(baseY) + inset + corner),
                      cornerColor, c.V(2.0f));
        draw->AddLine(c.P(PX(baseX + baseW) - inset, PY(baseY) + inset),
                      c.P(PX(baseX + baseW) - inset - corner, PY(baseY) + inset), cornerColor, c.V(2.0f));
        draw->AddLine(c.P(PX(baseX + baseW) - inset, PY(baseY) + inset),
                      c.P(PX(baseX + baseW) - inset, PY(baseY) + inset + corner), cornerColor, c.V(2.0f));
        draw->AddLine(c.P(PX(baseX) + inset, PY(baseY + baseH) - inset),
                      c.P(PX(baseX) + inset + corner, PY(baseY + baseH) - inset), cornerColor, c.V(2.0f));
        draw->AddLine(c.P(PX(baseX) + inset, PY(baseY + baseH) - inset),
                      c.P(PX(baseX) + inset, PY(baseY + baseH) - inset - corner), cornerColor, c.V(2.0f));
        draw->AddLine(c.P(PX(baseX + baseW) - inset, PY(baseY + baseH) - inset),
                      c.P(PX(baseX + baseW) - inset - corner, PY(baseY + baseH) - inset), cornerColor, c.V(2.0f));
        draw->AddLine(c.P(PX(baseX + baseW) - inset, PY(baseY + baseH) - inset),
                      c.P(PX(baseX + baseW) - inset, PY(baseY + baseH) - inset - corner), cornerColor, c.V(2.0f));

        char tag[64];
        std::snprintf(tag, sizeof(tag), "%s增益", SurvivorsBuffRarityName(rarity));
        DrawTextCentered(draw, m_ui, c, TS(15.0f), cx, PY(baseY + 16.0f), rarityU32, tag);

        // 图标框
        const float iconBox = 118.0f;
        const float iconX = baseX + 30.0f;
        const float iconY = baseY + 58.0f;
        const ImVec2 iconMin = c.P(PX(iconX), PY(iconY));
        const ImVec2 iconMax = c.P(PX(iconX + iconBox), PY(iconY + iconBox));
        draw->AddRectFilled(iconMin, iconMax, ToImColor(rarityColor, 0.14f), c.V(8.0f * scale));
        draw->AddRect(iconMin, iconMax, ToImColor(rarityColor, 0.85f), c.V(8.0f * scale), 0, c.V(1.5f));
        if (iconTexture) {
            DrawImage(draw, c, iconTexture, PX(iconX + 11.0f), PY(iconY + 11.0f), TS(iconBox - 22.0f),
                      TS(iconBox - 22.0f));
        }

        // 文案
        DrawText(draw, m_ui, c, TS(32.0f), PX(baseX + 176.0f), PY(baseY + 56.0f), kText, "宝箱增益");
        DrawWrapped(draw, m_ui, c, TS(15.0f), PX(baseX + 176.0f), PY(baseY + 102.0f), TS(baseW - 208.0f), kTextDim,
                    "来自地图增益宝箱的随机效果，已在本局生效。", 1.4f);
        draw->AddLine(c.P(PX(baseX + 176.0f), PY(baseY + 156.0f)),
                      c.P(PX(baseX + baseW - 30.0f), PY(baseY + 156.0f)), kRule, c.V(1.2f));
        DrawText(draw, m_ui, c, TS(13.0f), PX(baseX + 176.0f), PY(baseY + 172.0f), kTextMuted, "获得效果");
        DrawText(draw, m_ui, c, TS(21.0f), PX(baseX + 176.0f), PY(baseY + 194.0f), kGoldBright,
                 m_gachaEffect.c_str());

        // 稀有度三星：当前品质点亮。
        for (int i = 0; i < 3; ++i) {
            const ImVec2 center = c.P(PX(baseX + baseW - 42.0f - static_cast<float>(i) * 22.0f), PY(baseY + 26.0f));
            const float  r = c.V(5.0f * scale);
            const ImU32  color = i < rarity + 1 ? rarityU32 : IM_COL32(80, 70, 88, 150);
            draw->AddQuadFilled(ImVec2(center.x, center.y - r), ImVec2(center.x + r, center.y),
                                ImVec2(center.x, center.y + r), ImVec2(center.x - r, center.y), color);
        }

        // 收取提示放在卡片底部，避免和身后的宝箱/爆炸叠在一起。
        draw->AddLine(c.P(PX(baseX + 30.0f), PY(baseY + baseH - 58.0f)),
                      c.P(PX(baseX + baseW - 30.0f), PY(baseY + baseH - 58.0f)), kRule, c.V(1.2f));
        const float hintAlpha = 0.55f + 0.45f * (0.5f + 0.5f * std::sin((t - reveal) * 5.0f));
        DrawTextCentered(draw, m_ui, c, TS(15.0f), cx, PY(baseY + baseH - 38.0f), ToImColor(rarityColor, hintAlpha),
                         "点击 / 空格 收取 · 等待自动关闭");
    }

    // 点击或空格跳过（只允许在翻牌后）。
    if (!m_gachaPreview && t >= reveal &&
        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Space, false) ||
         ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
        m_gachaSkipRequested = true;
    }
}

// ================================================================ 设置
void SurvivorsDirector::DrawSettingsScreen(float vx, float vy, float vw, float vh) {
    Canvas      c = MakeCanvas(vx, vy, vw, vh);
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    const float panelW = 540.0f;
    const float panelH = 590.0f;
    const float panelX = c.w * 0.5f - panelW * 0.5f;
    const float panelY = c.h * 0.5f - panelH * 0.5f;
    DrawPanel(draw, m_ui, c, panelX, panelY, panelW, panelH);
    DrawSectionTitle(draw, m_ui, c, panelX + 30.0f, panelY + 26.0f, panelW - 60.0f, "偏好设置", "设置",
                     "设置会立即生效，并保存到存档目录。", false);

    SurvivorsSettingsState settings = GetSettings();
    const float rowX = panelX + 30.0f;
    const float rowW = panelW - 60.0f;
    float       rowY = panelY + 132.0f;
    const float rowH = 60.0f;

    auto rowLabel = [&](const char* label) {
        DrawText(draw, m_ui, c, 16.0f, rowX, rowY + 17.0f, kText, label);
    };
    auto rowRule = [&]() {
        DrawRule(draw, c, rowX, rowY + rowH - 1.0f, rowW);
    };

    // 背景音乐
    rowLabel("背景音乐");
    if (DrawButton(draw, m_ui, c, "##set_music", settings.musicEnabled ? "开" : "关", rowX + rowW - 86.0f,
                   rowY + 11.0f, 86.0f, 34.0f, settings.musicEnabled, false, 15.0f)) {
        settings.musicEnabled = !settings.musicEnabled;
        ApplySettings(settings, true);
        PlaySfx("click", 0.6f);
    }
    rowRule();
    rowY += rowH;

    // 音乐音量
    rowLabel("音乐音量");
    {
        char label[32];
        std::snprintf(label, sizeof(label), "%.0f%%", settings.musicVolume * 100.0f);
        DrawTextRight(draw, m_ui, c, 13.0f, rowX + 150.0f, rowY + 19.0f, kTextDim, label);
        bool released = false;
        if (DrawSlider(draw, m_ui, c, "##set_music_vol", rowX + rowW - 240.0f, rowY + 12.0f, 240.0f, 34.0f,
                       settings.musicVolume, &released)) {
            ApplySettings(settings, false);
        }
        if (released) ApplySettings(settings, true);
    }
    rowRule();
    rowY += rowH;

    // 游戏音效
    rowLabel("游戏音效");
    if (DrawButton(draw, m_ui, c, "##set_sfx", settings.sfxEnabled ? "开" : "关", rowX + rowW - 86.0f,
                   rowY + 11.0f, 86.0f, 34.0f, settings.sfxEnabled, false, 15.0f)) {
        settings.sfxEnabled = !settings.sfxEnabled;
        ApplySettings(settings, true);
        PlaySfx("click", 0.6f);
    }
    rowRule();
    rowY += rowH;

    // 音效音量
    rowLabel("音效音量");
    {
        char label[32];
        std::snprintf(label, sizeof(label), "%.0f%%", settings.sfxVolume * 100.0f);
        DrawTextRight(draw, m_ui, c, 13.0f, rowX + 150.0f, rowY + 19.0f, kTextDim, label);
        bool released = false;
        if (DrawSlider(draw, m_ui, c, "##set_sfx_vol", rowX + rowW - 240.0f, rowY + 12.0f, 240.0f, 34.0f,
                       settings.sfxVolume, &released)) {
            ApplySettings(settings, false);
        }
        if (released) ApplySettings(settings, true);
    }
    rowRule();
    rowY += rowH;

    // 主音量
    rowLabel("主音量");
    {
        char label[32];
        std::snprintf(label, sizeof(label), "%.0f%%", settings.masterVolume * 100.0f);
        DrawTextRight(draw, m_ui, c, 13.0f, rowX + 150.0f, rowY + 19.0f, kTextDim, label);
        bool released = false;
        if (DrawSlider(draw, m_ui, c, "##set_master_vol", rowX + rowW - 240.0f, rowY + 12.0f, 240.0f, 34.0f,
                       settings.masterVolume, &released)) {
            ApplySettings(settings, false);
        }
        if (released) ApplySettings(settings, true);
    }
    rowRule();

    const float doneW = 190.0f;
    if (DrawButton(draw, m_ui, c, "##settings_done", "完成", c.w * 0.5f - doneW * 0.5f, panelY + panelH - 82.0f,
                   doneW, 48.0f, true, false, 19.0f)) {
        PlaySfx("click", 0.6f);
        m_showSettings = false;
    }
    DrawTextCentered(draw, m_ui, c, 12.0f, c.w * 0.5f, panelY + panelH - 24.0f, kTextMuted,
                     "Esc 返回上一页");
}

// ================================================================ 结算
void SurvivorsDirector::DrawResultScreen(World& world, float vx, float vy, float vw, float vh) {
    Canvas      c = MakeCanvas(vx, vy, vw, vh);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    DrawDim(draw, c, 188);
    auto* player = PlayerOf(world);

    const float panelW = 840.0f;
    const float panelH = 560.0f;
    const float panelX = c.w * 0.5f - panelW * 0.5f;
    const float panelY = c.h * 0.5f - panelH * 0.5f;
    DrawPanel(draw, m_ui, c, panelX, panelY, panelW, panelH);
    DrawSectionTitle(draw, m_ui, c, panelX + 30.0f, panelY + 26.0f, panelW - 60.0f, "挑战结束", "倒在怪潮之中",
                     "灰石地牢 · 本局战绩", true);

    DrawStatColumns(draw, m_ui, c, panelX + 30.0f, panelY + 126.0f, panelW - 60.0f, FormatClock(m_time),
                    std::to_string(m_kills), std::to_string(player ? player->level : 1));
    DrawRule(draw, c, panelX + 30.0f, panelY + 218.0f, panelW - 60.0f);
    DrawTextCentered(draw, m_ui, c, 18.0f, c.w * 0.5f, panelY + 236.0f, kText, "本局技能");

    int  resultSkills[kSurvivorsMaxLoadout] = {-1, -1, -1, -1, -1};
    int  resultCount = 0;
    if (player) {
        for (int skill = 0; skill < kSurvivorsSkillCount && resultCount < kSurvivorsMaxLoadout; ++skill) {
            if (player->HasSkill(skill)) resultSkills[resultCount++] = skill;
        }
    }
    const float itemW = 100.0f;
    const float rowW = itemW * static_cast<float>((std::max)(1, resultCount));
    const float rowX = c.w * 0.5f - rowW * 0.5f;
    for (int slot = 0; slot < resultCount; ++slot) {
        const int   skill = resultSkills[slot];
        const float itemX = rowX + itemW * static_cast<float>(slot);
        if (m_ui.icon[skill]) DrawImage(draw, c, m_ui.icon[skill], itemX + 26.0f, panelY + 272.0f, 48.0f, 48.0f);
        char levelText[32];
        std::snprintf(levelText, sizeof(levelText), "Lv.%d", SkillLevel(*player, skill));
        DrawTextCentered(draw, m_ui, c, 13.0f, itemX + itemW * 0.5f, panelY + 326.0f, kTextDim, levelText);
    }

    const float buttonW = 248.0f;
    const float buttonY = panelY + panelH - 82.0f;
    if (DrawButton(draw, m_ui, c, "##result_menu", "回到主菜单", c.w * 0.5f - buttonW - 8.0f, buttonY, buttonW,
                   48.0f, false, false, 19.0f)) {
        PlaySfx("click", 0.6f);
        m_state = State::Menu;
    }
    if (DrawButton(draw, m_ui, c, "##result_replay", "再来一局", c.w * 0.5f + 8.0f, buttonY, buttonW, 48.0f, true,
                   false, 19.0f)) {
        PlaySfx("click", 0.6f);
        Reset(world);
    }
}

// ================================================================ 技能图鉴
void SurvivorsDirector::DrawCodexScreen(float vx, float vy, float vw, float vh) {
    Canvas      c = MakeCanvas(vx, vy, vw, vh);
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    const float panelW = 840.0f;
    const float panelH = 760.0f;
    const float panelX = c.w * 0.5f - panelW * 0.5f;
    const float panelY = c.h * 0.5f - panelH * 0.5f;
    DrawPanel(draw, m_ui, c, panelX, panelY, panelW, panelH);
    DrawSectionTitle(draw, m_ui, c, panelX + 30.0f, panelY + 26.0f, panelW - 60.0f, "地牢手册", "技能图鉴",
                     "职业给 1 个基础技能，击杀最多再掉落 4 个（共 5 个）。", true);

    const float rowX = panelX + 30.0f;
    const float rowW = panelW - 60.0f;
    float       rowY = panelY + 126.0f;
    for (int i = 0; i < kSurvivorsSkillCount; ++i) {
        draw->AddRectFilled(c.P(rowX, rowY), c.P(rowX + rowW, rowY + 44.0f), IM_COL32(37, 30, 43, 255), c.V(3.0f));
        draw->AddRect(c.P(rowX, rowY), c.P(rowX + rowW, rowY + 44.0f), IM_COL32(72, 53, 64, 255), c.V(3.0f),
                      c.V(1.0f));
        if (m_ui.icon[i]) DrawImage(draw, c, m_ui.icon[i], rowX + 10.0f, rowY + 4.0f, 34.0f, 34.0f);
        DrawText(draw, m_ui, c, 15.0f, rowX + 54.0f, rowY + 12.0f, kText, kSkills[i].name);
        DrawText(draw, m_ui, c, 13.0f, rowX + 150.0f, rowY + 14.0f, kTextDim, kSkills[i].desc);
        rowY += 50.0f;
    }

    const float buttonW = 240.0f;
    if (DrawButton(draw, m_ui, c, "##codex_back", "返回主菜单", c.w * 0.5f - buttonW * 0.5f,
                   panelY + panelH - 70.0f, buttonW, 46.0f, true, false, 19.0f)) {
        PlaySfx("click", 0.6f);
        m_state = State::Menu;
    }
}

// ================================================================ 出战准备
void SurvivorsDirector::DrawPrepareScreen(World& world, float vx, float vy, float vw, float vh) {
    Canvas      c = MakeCanvas(vx, vy, vw, vh);
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    const float panelW = 840.0f;
    const float panelH = 660.0f;
    const float panelX = c.w * 0.5f - panelW * 0.5f;
    const float panelY = c.h * 0.5f - panelH * 0.5f;
    DrawPanel(draw, m_ui, c, panelX, panelY, panelW, panelH);
    DrawSectionTitle(draw, m_ui, c, panelX + 30.0f, panelY + 26.0f, panelW - 60.0f, "出战准备", "踏入地牢",
                     "职业决定初始技能；技能最高 20 级，击杀最多再掉落 4 个", true);

    const float cardW = (panelW - 60.0f - 28.0f) / 3.0f;
    const float cardH = 264.0f;
    const float cardY = panelY + 146.0f;
    for (int i = 0; i < 3; ++i) {
        const float cardX = panelX + 30.0f + (cardW + 14.0f) * static_cast<float>(i);
        DrawCard(draw, c, cardX, cardY, cardW, cardH, false);
        if (i == 0) {
            if (m_ui.crest) DrawImage(draw, c, m_ui.crest, cardX + (cardW - 120.0f) * 0.5f, cardY + 30.0f, 120.0f,
                                      76.0f);
            DrawTextCentered(draw, m_ui, c, 22.0f, cardX + cardW * 0.5f, cardY + 126.0f, kText, "二毛");
            char loadoutText[64];
            std::snprintf(loadoutText, sizeof(loadoutText), "初始技能：%s",
                          kSkills[kSurvivorsClasses[m_playerClass].baseSkill].name);
            DrawTextCentered(draw, m_ui, c, 14.0f, cardX + cardW * 0.5f, cardY + 158.0f, kTextDim, loadoutText);
        } else if (i == 1) {
            const float mapX = cardX + 12.0f;
            const float mapY = cardY + 12.0f;
            const float mapW = cardW - 24.0f;
            const float mapH = 132.0f;
            if (m_ui.floor) {
                DrawImage(draw, c, m_ui.floor, mapX, mapY, mapW, mapH, IM_COL32(205, 195, 215, 255));
            } else {
                draw->AddRectFilled(c.P(mapX, mapY), c.P(mapX + mapW, mapY + mapH), IM_COL32(30, 27, 38, 255),
                                    c.V(2.0f));
            }
            draw->AddRectFilled(c.P(mapX, mapY), c.P(mapX + mapW, mapY + mapH), IM_COL32(16, 11, 20, 120));
            for (int line = 1; line < 4; ++line) {
                const float ly = mapY + mapH * static_cast<float>(line) / 4.0f;
                draw->AddLine(c.P(mapX + 6.0f, ly), c.P(mapX + mapW - 6.0f, ly), IM_COL32(228, 171, 106, 32),
                              c.V(1.0f));
            }
            // 用房间/走廊轮廓把实时地面贴图读成一张地牢小地图
            const ImU32 roomLine = IM_COL32(228, 171, 106, 120);
            const ImU32 roomFill = IM_COL32(26, 19, 30, 120);
            const float roomAx = mapX + mapW * 0.10f;
            const float roomAy = mapY + mapH * 0.16f;
            const float roomBx = mapX + mapW * 0.44f;
            const float roomBy = mapY + mapH * 0.58f;
            const float roomCx = mapX + mapW * 0.58f;
            const float roomCy = mapY + mapH * 0.30f;
            const float roomDx = mapX + mapW * 0.90f;
            const float roomDy = mapY + mapH * 0.78f;
            draw->AddRectFilled(c.P(roomAx, roomAy), c.P(roomBx, roomBy), roomFill, c.V(2.0f));
            draw->AddRect(c.P(roomAx, roomAy), c.P(roomBx, roomBy), roomLine, c.V(2.0f), c.V(1.0f));
            draw->AddRectFilled(c.P(roomCx, roomCy), c.P(roomDx, roomDy), roomFill, c.V(2.0f));
            draw->AddRect(c.P(roomCx, roomCy), c.P(roomDx, roomDy), roomLine, c.V(2.0f), c.V(1.0f));
            draw->AddLine(c.P(roomBx, (roomAy + roomBy) * 0.5f), c.P(roomCx, (roomCy + roomDy) * 0.5f), roomLine,
                          c.V(2.0f));
            const float markerX = mapX + mapW * 0.62f;
            const float markerY = mapY + mapH * 0.42f;
            draw->AddQuadFilled(c.P(markerX, markerY - 6.0f), c.P(markerX + 6.0f, markerY),
                                c.P(markerX, markerY + 6.0f), c.P(markerX - 6.0f, markerY), kGoldBright);
            DrawTextCentered(draw, m_ui, c, 22.0f, cardX + cardW * 0.5f, cardY + 158.0f, kText, "灰石地牢");
            DrawTextCentered(draw, m_ui, c, 14.0f, cardX + cardW * 0.5f, cardY + 190.0f, kTextDim,
                             "90×54 · 石柱、木箱与火把");
        } else {
            DrawText(draw, m_ui, c, 22.0f, cardX + 16.0f, cardY + 22.0f, kText, "生存目标");
            DrawWrapped(draw, m_ui, c, 14.0f, cardX + 16.0f, cardY + 62.0f, cardW - 32.0f, kText,
                        "击杀敌人，拾取经验，升级并选择强化。", 1.5f);
            DrawWrapped(draw, m_ui, c, 13.0f, cardX + 16.0f, cardY + 152.0f, cardW - 32.0f, kTextDim,
                        "WASD 移动 · Esc 暂停 · 自动攻击最近的敌人", 1.5f);
        }
    }

    // 职业选择：决定进入战斗时唯一的初始基础技能；其余技能靠击杀掉落。
    {
        DrawText(draw, m_ui, c, 18.0f, panelX + 30.0f, panelY + 430.0f, kText, "选择职业");
        DrawTextRight(draw, m_ui, c, 13.0f, panelX + panelW - 30.0f, panelY + 433.0f, kTextMuted,
                      "基础技能 1 个 + 掉落最多 4 个 = 5 个");

        const float optionW = 246.0f;
        const float optionH = 100.0f;
        const float optionGap = 16.0f;
        const float rowW = optionW * static_cast<float>(kSurvivorsClassCount) +
                           optionGap * static_cast<float>(kSurvivorsClassCount - 1);
        const float rowX = c.w * 0.5f - rowW * 0.5f;
        const float rowY = panelY + 460.0f;
        for (int cls = 0; cls < kSurvivorsClassCount; ++cls) {
            const bool  selected = (m_playerClass == cls);
            const float x = rowX + (optionW + optionGap) * static_cast<float>(cls);
            ImGui::SetCursorScreenPos(c.P(x, rowY));
            const bool pressed = ImGui::InvisibleButton((std::string("##class") + std::to_string(cls)).c_str(),
                                                        c.S(optionW, optionH));
            const bool hovered = ImGui::IsItemHovered();
            if (pressed && !selected) {
                PlaySfx("click", 0.55f);
                m_playerClass = cls;
            }
            draw->AddRectFilled(c.P(x, rowY), c.P(x + optionW, rowY + optionH),
                                selected ? kCardHover : kCard, c.V(4.0f));
            draw->AddRect(c.P(x, rowY), c.P(x + optionW, rowY + optionH),
                          selected ? kGoldBright : (hovered ? kGold : kCardBorder), c.V(1.0f),
                          ImDrawFlags_None, selected ? c.V(2.5f) : c.V(1.0f));
            const int baseSkill = kSurvivorsClasses[cls].baseSkill;
            DrawNineSlice(draw, c, m_ui.slot, x + 12.0f, rowY + 18.0f, 62.0f, 62.0f, 10.0f);
            if (m_ui.icon[baseSkill]) {
                DrawImage(draw, c, m_ui.icon[baseSkill], x + 21.0f, rowY + 27.0f, 44.0f, 44.0f);
            }
            DrawText(draw, m_ui, c, 19.0f, x + 86.0f, rowY + 22.0f, kText, kSurvivorsClasses[cls].name);
            DrawText(draw, m_ui, c, 14.0f, x + 86.0f, rowY + 54.0f, kTextDim,
                     kSurvivorsClasses[cls].desc);
            if (selected) {
                DrawTextRight(draw, m_ui, c, 12.0f, x + optionW - 12.0f, rowY + 76.0f, kGold, "已选择");
            }
        }
    }

    const float backW = 220.0f;
    const float startW = 240.0f;
    const float gap = 16.0f;
    const float total = backW + gap + startW;
    const float buttonY = panelY + panelH - 80.0f;
    const float startX = c.w * 0.5f - total * 0.5f;
    if (DrawButton(draw, m_ui, c, "##prepare_back", "返回", startX, buttonY, backW, 48.0f, false, false, 19.0f)) {
        PlaySfx("click", 0.6f);
        m_state = State::Menu;
    }
    if (DrawButton(draw, m_ui, c, "##prepare_start", "进入战斗", startX + backW + gap, buttonY, startW, 48.0f, true,
                   false, 19.0f)) {
        PlaySfx("click", 0.6f);
        Reset(world);
    }
}

// ================================================================ Toast
void SurvivorsDirector::DrawToast(float vx, float vy, float vw, float vh, const std::string& text, float timer) {
    if (timer <= 0.0f || text.empty()) return;
    Canvas      c = MakeCanvas(vx, vy, vw, vh);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const float alpha = Clamp01(timer / 0.35f);
    const ImVec2 measured = MeasureText(m_ui, c.V(15.0f), text.c_str());
    const float  textW = measured.x / c.scale;
    const float  w = textW + 40.0f;
    const float  h = 38.0f;
    const float  x = c.w * 0.5f - w * 0.5f;
    const float  y = 96.0f;
    draw->AddRectFilled(c.P(x, y), c.P(x + w, y + h), IM_COL32(6, 8, 12, static_cast<int>(214 * alpha)),
                        c.V(5.0f));
    draw->AddRect(c.P(x, y), c.P(x + w, y + h), IM_COL32(240, 205, 120, static_cast<int>(170 * alpha)),
                  c.V(5.0f), 1.0f);
    DrawTextCentered(draw, m_ui, c, 15.0f, c.w * 0.5f, y + (h - 15.0f) * 0.5f - 1.0f,
                     IM_COL32(255, 226, 150, static_cast<int>(255 * alpha)), text.c_str());
}

} // namespace erm
