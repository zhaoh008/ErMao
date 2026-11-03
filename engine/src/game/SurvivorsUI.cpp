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

const SkillMeta kSkills[6] = {
    {"箭矢", "自动攻击最近的敌人"},
    {"雷霆", "落雷打击敌人"},
    {"回旋刃", "飞刃环绕角色"},
    {"霜环", "扩散冰环并减速"},
    {"毒雾", "持续伤害范围内敌人"},
    {"火球", "火球命中后爆炸"},
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
    c.scale = Clamp(vh / 900.0f, 0.70f, 1.25f);
    c.w = vw / c.scale;
    c.h = vh / c.scale;
    return c;
}

ImTextureID TexId(Texture* texture) {
    return static_cast<ImTextureID>(reinterpret_cast<intptr_t>(texture ? texture->SRV() : nullptr));
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

int SkillLevel(const SurvivorsPlayerComponent& player, int skill) {
    switch (skill) {
        case 0:
            return 1 + player.arrowSpeedLevel + player.arrowDamageLevel + player.arrowRangeLevel +
                   (player.arrowSpread ? 1 : 0);
        case 1: return player.lightningLevel;
        case 2: return player.bladeLevel;
        case 3: return player.frostLevel;
        case 4: return player.poisonLevel;
        case 5: return player.fireballLevel;
        default: return 1;
    }
}

int SkillSubLevel(const SurvivorsPlayerComponent& player, int skill, int sub) {
    if (skill == 0) {
        switch (sub) {
            case 0: return player.arrowSpread ? 1 : 0;
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
                          player.arrowSpread ? "八向 0.65x" : "单发锁定", player.ArrowDamage(),
                          player.ArrowSpeedBonus() * 100.0f, player.ArrowRange());
            break;
        case 1:
            std::snprintf(buffer, sizeof(buffer), "伤害 %.0f · 目标 %d · 连锁 %d · CD %.1fs", player.LightningDamage(),
                          player.LightningTargets(), player.LightningChains(), player.LightningCooldown());
            break;
        case 2:
            std::snprintf(buffer, sizeof(buffer), "伤害 %.0f · 刃数 %d · 半径 %.1f", player.BladeDamage(),
                          player.BladeCount(), player.BladeRadius());
            break;
        case 3:
            std::snprintf(buffer, sizeof(buffer), "伤害 %.0f · 半径 %.1f · 减速 %.0f%%", player.FrostDamage(),
                          player.FrostRadius(), player.FrostSlow() * 100.0f);
            break;
        case 4:
            std::snprintf(buffer, sizeof(buffer), "DPS %.0f · 半径 %.1f", player.PoisonDps(), player.PoisonRadius());
            break;
        case 5:
            std::snprintf(buffer, sizeof(buffer), "伤害 %.0f · CD %.1fs", player.FireballDamage(),
                          player.FireballCooldown());
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
    if (choice.skill == 0) {
        const int current = SkillSubLevel(player, 0, choice.sub);
        if (choice.sub == 0) {
            info.tag = "箭矢 · 新技能";
            info.name = "八向散射";
            info.level = "解锁";
            info.desc = "每次射击向八个方向各射出一箭，单箭伤害 0.65x。";
            info.effect = "单发 → 八向";
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
            std::snprintf(buffer, sizeof(buffer), "伤害 %.0f → %.0f · 半径 %.1f → %.1f", player.FrostDamage(),
                          after.FrostDamage(), player.FrostRadius(), after.FrostRadius());
            break;
        case 4:
            std::snprintf(buffer, sizeof(buffer), "DPS %.0f → %.0f · 半径 %.1f → %.1f", player.PoisonDps(),
                          after.PoisonDps(), player.PoisonRadius(), after.PoisonRadius());
            break;
        case 5:
            std::snprintf(buffer, sizeof(buffer), "伤害 %.0f → %.0f · CD %.1fs → %.1fs", player.FireballDamage(),
                          after.FireballDamage(), player.FireballCooldown(), after.FireballCooldown());
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

    static const char* kIconPaths[6] = {"sprites/ui/icon_arrow.png",     "sprites/ui/icon_lightning.png",
                                        "sprites/ui/icon_blade.png",     "sprites/ui/icon_frost.png",
                                        "sprites/ui/icon_poison.png",    "sprites/ui/icon_fireball.png"};
    for (int i = 0; i < 6; ++i) icon[i] = assets.GetTextureAsset(kIconPaths[i]);

    // 关卡里程序化生成的地面，用于出战准备的地图卡片
    floor = assets.GetTexture("floor_big");

    const bool ok = panel && slot && buttonNormal && buttonPrimary && barTrack && barHealthFill && barXpFill &&
                    titleBackground && body;
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
    if (!toast.empty() && m_state != State::LevelUp) {
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

    // 底部六槽
    const float slotSize = 70.0f;
    const float gap = 5.0f;
    const float totalW = slotSize * 6.0f + gap * 5.0f;
    const float slotY = c.h - 24.0f - slotSize;
    const float slotX = c.w * 0.5f - totalW * 0.5f;
    int         hoveredSkill = -1;
    for (int i = 0; i < 6; ++i) {
        const float x = slotX + (slotSize + gap) * static_cast<float>(i);
        if (interactive) {
            ImGui::SetCursorScreenPos(c.P(x, slotY));
            const bool pressed = ImGui::InvisibleButton((std::string("##skill") + std::to_string(i)).c_str(),
                                                        c.S(slotSize, slotSize));
            if (ImGui::IsItemHovered()) hoveredSkill = i;
            if (pressed) m_selectedSkill = (m_selectedSkill == i) ? -1 : i;
        }
        DrawNineSlice(draw, c, m_ui.slot, x, slotY, slotSize, slotSize, 12.0f);
        if (m_ui.icon[i]) DrawImage(draw, c, m_ui.icon[i], x + 14.0f, slotY + 7.0f, 42.0f, 42.0f);
        const int skillLevel = player ? SkillLevel(*player, i) : 0;
        char slotLabel[32];
        if (skillLevel > 0) {
            std::snprintf(slotLabel, sizeof(slotLabel), "Lv.%d", skillLevel);
        } else {
            std::snprintf(slotLabel, sizeof(slotLabel), "未解锁");
        }
        DrawTextCentered(draw, m_ui, c, 11.0f, x + slotSize * 0.5f, slotY + 54.0f,
                         skillLevel > 0 ? kSlotText : kTextMuted, slotLabel);
    }

    // 技能说明：悬停优先，点击可钉住
    const int shownSkill = (interactive && hoveredSkill >= 0) ? hoveredSkill : (interactive ? m_selectedSkill : -1);
    if (shownSkill >= 0 && shownSkill < 6 && player) {
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
        if (m_ui.icon[m_levelChoices[i].skill]) {
            DrawImage(draw, c, m_ui.icon[m_levelChoices[i].skill], cardX + 16.0f, cardY + 42.0f, 64.0f, 64.0f);
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

    const float itemW = 100.0f;
    const float rowW = itemW * 6.0f;
    const float rowX = c.w * 0.5f - rowW * 0.5f;
    for (int i = 0; i < 6; ++i) {
        const float itemX = rowX + itemW * static_cast<float>(i);
        if (m_ui.icon[i]) DrawImage(draw, c, m_ui.icon[i], itemX + 26.0f, panelY + 272.0f, 48.0f, 48.0f);
        char levelText[32];
        std::snprintf(levelText, sizeof(levelText), "Lv.%d", player ? SkillLevel(*player, i) : 1);
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
    const float panelH = 580.0f;
    const float panelX = c.w * 0.5f - panelW * 0.5f;
    const float panelY = c.h * 0.5f - panelH * 0.5f;
    DrawPanel(draw, m_ui, c, panelX, panelY, panelW, panelH);
    DrawSectionTitle(draw, m_ui, c, panelX + 30.0f, panelY + 26.0f, panelW - 60.0f, "地牢手册", "技能图鉴",
                     "复用游戏内的技能名称与说明；没有额外解锁系统。", true);

    const float rowX = panelX + 30.0f;
    const float rowW = panelW - 60.0f;
    float       rowY = panelY + 126.0f;
    for (int i = 0; i < 6; ++i) {
        draw->AddRectFilled(c.P(rowX, rowY), c.P(rowX + rowW, rowY + 48.0f), IM_COL32(37, 30, 43, 255), c.V(3.0f));
        draw->AddRect(c.P(rowX, rowY), c.P(rowX + rowW, rowY + 48.0f), IM_COL32(72, 53, 64, 255), c.V(3.0f),
                      c.V(1.0f));
        if (m_ui.icon[i]) DrawImage(draw, c, m_ui.icon[i], rowX + 12.0f, rowY + 6.0f, 36.0f, 36.0f);
        DrawText(draw, m_ui, c, 16.0f, rowX + 62.0f, rowY + 13.0f, kText, kSkills[i].name);
        DrawText(draw, m_ui, c, 14.0f, rowX + 150.0f, rowY + 15.0f, kTextDim, kSkills[i].desc);
        rowY += 54.0f;
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
    const float panelH = 560.0f;
    const float panelX = c.w * 0.5f - panelW * 0.5f;
    const float panelY = c.h * 0.5f - panelH * 0.5f;
    DrawPanel(draw, m_ui, c, panelX, panelY, panelW, panelH);
    DrawSectionTitle(draw, m_ui, c, panelX + 30.0f, panelY + 26.0f, panelW - 60.0f, "出战准备", "踏入地牢",
                     "当前演示使用默认角色和地图", true);

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
            DrawTextCentered(draw, m_ui, c, 14.0f, cardX + cardW * 0.5f, cardY + 158.0f, kTextDim,
                             "起始技能 · 箭矢");
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
