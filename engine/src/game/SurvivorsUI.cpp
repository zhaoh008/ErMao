// ErMao Engine - 2D 幸存者 UI（按 ermao-ui-v2 原型重做）
//
// 设计基准：1600×900 逻辑画布；窗口高度决定整体缩放，宽度不足时逻辑画布变窄。
// 切图面板/按钮/进度条/技能槽全部走九宫格；背景使用 live 战场或 title 主视觉。
#include "game/Survivors.h"

#include "asset/AssetManager.h"
#include "core/FileSystem.h"
#include "core/Log.h"
#include "platform/Audio.h"
#include "platform/Input.h"
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

// ---------------------------------------------------------------- 调色板（xianxia-design：暖宣纸 + 青玉 + 朱砂）
const ImU32 kText = IM_COL32(44, 58, 50, 255);        // 墨绿正文
const ImU32 kTextDim = IM_COL32(82, 97, 79, 255);
const ImU32 kTextMuted = IM_COL32(120, 131, 107, 255);
const ImU32 kGold = IM_COL32(158, 57, 49, 255);       // 朱砂强调
const ImU32 kGoldBright = IM_COL32(176, 70, 60, 255);
const ImU32 kTag = IM_COL32(46, 107, 87, 255);        // 青玉
const ImU32 kMint = IM_COL32(47, 122, 107, 255);
const ImU32 kRed = IM_COL32(178, 59, 59, 255);
const ImU32 kCard = IM_COL32(244, 236, 217, 255);
const ImU32 kCardHover = IM_COL32(232, 238, 218, 255);
const ImU32 kCardBorder = IM_COL32(178, 169, 141, 255);
const ImU32 kCardBorderHover = IM_COL32(46, 107, 87, 255);
const ImU32 kRule = IM_COL32(188, 178, 151, 255);
const ImU32 kPanelInner = IM_COL32(250, 244, 230, 255);
const ImU32 kSlotText = IM_COL32(55, 74, 62, 255);

// 仙侠宣纸主题的公共色
const ImU32 kPaper = IM_COL32(246, 238, 220, 255);
const ImU32 kPaperDeep = IM_COL32(239, 229, 206, 255);
const ImU32 kPaperBorder = IM_COL32(118, 126, 103, 255);
const ImU32 kPaperBorderSoft = IM_COL32(207, 197, 172, 255);
const ImU32 kInkGreen = IM_COL32(31, 92, 74, 255);
const ImU32 kInkGreenDark = IM_COL32(24, 66, 53, 255);
const ImU32 kTrack = IM_COL32(218, 208, 185, 255);

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
    // 手机横屏（vh<520）对文字额外放大；布局尺寸不变，只放大字号。
    float  textScale = 1.0f;
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
    if (vh > vw) {
        // 手机竖屏：使用 1600 设计高度；宽度按实际比例（约 736），再把字号放大。
        c.scale = Clamp(vh / 1600.0f, 0.40f, 1.25f);
        c.textScale = 1.30f;
    } else {
        // 手机横屏高度通常 360~430 CSS px：允许缩到 0.40，保证 900 设计高度能完整放下。
        c.scale = Clamp(vh / 900.0f, 0.40f, 1.25f);
        // 按 900 设计高度缩到 0.43 后，15px 字只有 6px；手机横屏把字号再放大 30%。
        c.textScale = (vh < 520.0f) ? 1.30f : 1.0f;
    }
#else
    c.scale = Clamp(vh / 900.0f, 0.70f, 1.25f);
#endif
    c.w = vw / c.scale;
    c.h = vh / c.scale;
    return c;
}

float FitPanelWidth(const Canvas& c, float desired) { return (std::min)(desired, c.w - 40.0f); }

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

ImVec2 MeasureText(const SurvivorsUIAssets& ui, const Canvas& c, float size, const char* text) {
    ImFont* font = PickFont(ui, size);
    if (!font || !text) return ImVec2(0, 0);
    return font->CalcTextSizeA(c.V(size * c.textScale), FLT_MAX, 0.0f, text);
}

void DrawText(ImDrawList* draw, const SurvivorsUIAssets& ui, const Canvas& c, float size, float x, float y,
              ImU32 color, const char* text) {
    if (!text || !*text) return;
    ImFont* font = PickFont(ui, size);
    if (!font) return;
    draw->AddText(font, c.V(size * c.textScale), c.P(x, y), color, text);
}

void DrawTextRight(ImDrawList* draw, const SurvivorsUIAssets& ui, const Canvas& c, float size, float rightX,
                   float y, ImU32 color, const char* text) {
    const ImVec2 measured = MeasureText(ui, c, size, text);
    DrawText(draw, ui, c, size, rightX - measured.x / c.scale, y, color, text);
}

void DrawTextCentered(ImDrawList* draw, const SurvivorsUIAssets& ui, const Canvas& c, float size, float centerX,
                      float y, ImU32 color, const char* text) {
    const ImVec2 measured = MeasureText(ui, c, size, text);
    DrawText(draw, ui, c, size, centerX - measured.x / c.scale * 0.5f, y, color, text);
}

void DrawTextShadow(ImDrawList* draw, const SurvivorsUIAssets& ui, const Canvas& c, float size, float x, float y,
                    ImU32 color, ImU32 shadow1, ImU32 shadow2, const char* text) {
    DrawText(draw, ui, c, size, x, y + 4.0f, shadow1, text);
    (void)shadow2;
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
    const float scaledSize = size * c.textScale;
    const float maxPx = c.V(wrapWidth);
    float       cursorY = y;
    std::string line;
    std::string candidate;
    auto emit = [&]() {
        if (line.empty()) return;
        draw->AddText(font, c.V(scaledSize), c.P(x, cursorY), color, line.c_str());
        cursorY += scaledSize * lineGap;
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
        const ImVec2 measured = font->CalcTextSizeA(c.V(scaledSize), FLT_MAX, 0.0f, candidate.c_str());
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
    (void)ui;
    draw->AddRectFilled(c.P(x + 6.0f, y + 8.0f), c.P(x + w + 6.0f, y + h + 8.0f), IM_COL32(40, 38, 30, 70),
                        c.V(9.0f));
    draw->AddRectFilled(c.P(x, y), c.P(x + w, y + h), kPaper, c.V(7.0f));
    draw->AddRect(c.P(x, y), c.P(x + w, y + h), kPaperBorder, c.V(7.0f), ImDrawFlags_None, c.V(2.0f));
    draw->AddRect(c.P(x + 5.0f, y + 5.0f), c.P(x + w - 5.0f, y + h - 5.0f), kPaperBorderSoft, c.V(5.0f),
                  ImDrawFlags_None, c.V(1.0f));
}

void DrawCard(ImDrawList* draw, const SurvivorsUIAssets& ui, const Canvas& c, float x, float y, float w, float h,
              bool hovered) {
    (void)ui;
    draw->AddRectFilled(c.P(x, y), c.P(x + w, y + h), hovered ? kCardHover : kCard, c.V(5.0f));
    draw->AddRect(c.P(x, y), c.P(x + w, y + h), hovered ? kCardBorderHover : kCardBorder, c.V(5.0f),
                  ImDrawFlags_None, c.V(hovered ? 2.0f : 1.0f));
}

void DrawSlot(ImDrawList* draw, const Canvas& c, float x, float y, float w, float h, ImU32 tint = IM_COL32_WHITE) {
    draw->AddRectFilled(c.P(x, y), c.P(x + w, y + h), kPaperDeep, c.V(5.0f));
    draw->AddRect(c.P(x, y), c.P(x + w, y + h), kCardBorder, c.V(5.0f), ImDrawFlags_None, c.V(1.0f));
    if ((tint & IM_COL32_A_MASK) < IM_COL32_A_MASK) {
        draw->AddRectFilled(c.P(x, y), c.P(x + w, y + h), tint, c.V(5.0f));
    }
}

void DrawRule(ImDrawList* draw, const Canvas& c, float x, float y, float w) {
    draw->AddLine(c.P(x, y), c.P(x + w, y), kRule, (std::max)(1.0f, c.V(1.0f)));
}

void DrawBar(ImDrawList* draw, const SurvivorsUIAssets& ui, const Canvas& c, float x, float y, float w, float h,
             float ratio, Texture* fillTexture) {
    (void)ui;
    draw->AddRectFilled(c.P(x, y), c.P(x + w, y + h), kTrack, c.V(h * 0.5f));
    draw->AddRect(c.P(x, y), c.P(x + w, y + h), kPaperBorderSoft, c.V(h * 0.5f), ImDrawFlags_None, c.V(1.0f));
    ratio = Clamp01(ratio);
    if (ratio <= 0.0f || !fillTexture) return;
    const float insetX = 4.0f;
    const float insetY = 3.0f;
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
    (void)ui;
    ImGui::SetCursorScreenPos(c.P(x, y));
    const bool pressed = ImGui::InvisibleButton(id, c.S(w, h));
    const bool hovered = ImGui::IsItemHovered() && !disabled;
    const bool active = ImGui::IsItemActive() && !disabled;

    const ImU32 fill = disabled ? IM_COL32(220, 212, 195, 255)
                                : (primary ? (active ? kInkGreenDark : (hovered ? IM_COL32(52, 132, 108, 255) : kInkGreen))
                                           : (active ? IM_COL32(226, 231, 210, 255)
                                                     : (hovered ? IM_COL32(238, 242, 224, 255) : kPaper)));
    const ImU32 border = disabled ? IM_COL32(184, 176, 157, 255) : (primary ? kInkGreenDark : kPaperBorder);
    const ImU32 color = disabled ? IM_COL32(140, 134, 118, 255) : (primary ? kPaper : kInkGreenDark);

    draw->AddRectFilled(c.P(x, y), c.P(x + w, y + h), fill, c.V(7.0f));
    draw->AddRect(c.P(x, y), c.P(x + w, y + h), border, c.V(7.0f), ImDrawFlags_None, c.V(1.6f));
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
    if (GameObject* object = SurvivorsFindLocalPlayer(world)) {
        if (auto* player = object->GetComponent<SurvivorsPlayerComponent>()) return player;
    }
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
    SetSamplerLinear(draw);
    draw->AddImage(TexId(texture), c.P(0, 0), c.P(c.w, c.h), ImVec2(u0, v0), ImVec2(u1, v1));
    SetSamplerNearest(draw);
}

void DrawDim(ImDrawList* draw, const Canvas& c, int alpha) {
    draw->AddRectFilled(c.P(0, 0), c.P(c.w, c.h), IM_COL32(18, 26, 21, alpha));
}

bool DrawSlider(ImDrawList* draw, const SurvivorsUIAssets& ui, const Canvas& c, const char* id, float x, float y,
                float w, float h, float& value, bool* released) {
    ImGui::SetCursorScreenPos(c.P(x, y));
    ImGui::InvisibleButton(id, c.S(w, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const float trackY = y + h * 0.5f;
    const float trackH = 8.0f;
    draw->AddRectFilled(c.P(x, trackY - trackH * 0.5f), c.P(x + w, trackY + trackH * 0.5f), kTrack, c.V(4.0f));
    draw->AddRect(c.P(x, trackY - trackH * 0.5f), c.P(x + w, trackY + trackH * 0.5f), kPaperBorderSoft, c.V(4.0f),
                  c.V(1.0f));
    const float knobX = x + w * Clamp01(value);
    draw->AddRectFilled(c.P(x, trackY - trackH * 0.5f), c.P(knobX, trackY + trackH * 0.5f), kInkGreen, c.V(4.0f));
    draw->AddCircleFilled(c.P(knobX, trackY), c.V(active ? 9.0f : 7.0f),
                          active ? kInkGreenDark : (hovered ? kInkGreen : kTag), 18);
    draw->AddCircle(c.P(knobX, trackY), c.V(active ? 9.0f : 7.0f), kPaper, 18, c.V(1.0f));

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
    SetSamplerLinear(draw);
    draw->AddImageQuad(TexId(texture), p0, p1, p2, p3, ImVec2(u0, v0), ImVec2(u1, v0), ImVec2(u1, v1),
                       ImVec2(u0, v1), tint);
    SetSamplerNearest(draw);
}

} // namespace

// ================================================================ 资源加载
bool SurvivorsUIAssets::Load(AssetManager& assets, ImFont* bodyFont, ImFont* largeFont, ImFont* displayFont) {
    body = bodyFont;
    large = largeFont;
    display = displayFont;

    // 水墨国风切图：全部来自 engine/assets/inkwash（素材来源见 LICENSE-INKWASH.txt）
    panel = assets.GetTextureAsset("inkwash/panel.png");
    slot = assets.GetTextureAsset("inkwash/slot.png");
    card = panel;
    buttonNormal = assets.GetTextureAsset("inkwash/btn_normal.png");
    buttonHover = assets.GetTextureAsset("inkwash/btn_hover.png");
    buttonPressed = assets.GetTextureAsset("inkwash/btn_pressed.png");
    buttonDisabled = assets.GetTextureAsset("inkwash/btn_disabled.png");
    buttonPrimary = assets.GetTextureAsset("inkwash/btn_primary.png");
    barTrack = assets.GetTextureAsset("inkwash/bar_track.png");
    barHealthFill = assets.GetTextureAsset("inkwash/bar_health.png");
    barXpFill = assets.GetTextureAsset("inkwash/bar_xp.png");
    crest = assets.GetTextureAsset("inkwash/crest.png");
    iconHealth = assets.GetTextureAsset("inkwash/health.png");
    iconXp = assets.GetTextureAsset("inkwash/xp.png");
    titleBackground = assets.GetTextureAsset("inkwash/menu.jpg");

    static const char* kIconPaths[10] = {
        "inkwash/icon_0.png", "inkwash/icon_1.png", "inkwash/icon_2.png", "inkwash/icon_3.png",
        "inkwash/icon_4.png", "inkwash/icon_5.png", "inkwash/icon_6.png", "inkwash/icon_7.png",
        "inkwash/icon_8.png", "inkwash/icon_9.png"};
    for (int i = 0; i < 10; ++i) icon[i] = assets.GetTextureAsset(kIconPaths[i]);

    floor = assets.GetTextureAsset("inkwash/ground.png");
    chestClosed = assets.GetTextureAsset("inkwash/chest.png");
    chestOpen = assets.GetTextureAsset("inkwash/chest_open.png");

    static const char* kHeroPaths[3] = {"inkwash/hero_0.png", "inkwash/hero_1.png", "inkwash/hero_2.png"};
    for (int i = 0; i < 3; ++i) hero[i] = assets.GetTextureAsset(kHeroPaths[i]);

    propTree = assets.GetTextureAsset("inkwash/prop_tree.png");
    propCenser = assets.GetTextureAsset("inkwash/prop_censer.png");
    propRock = assets.GetTextureAsset("inkwash/prop_rock.png");
    propLantern = assets.GetTextureAsset("inkwash/prop_lantern.png");
    propJar = assets.GetTextureAsset("inkwash/prop_jar.png");
    propUrn = assets.GetTextureAsset("inkwash/prop_urn.png");

    fxBlade = assets.GetTextureAsset("inkwash/fx_blade.png");
    fxSword = assets.GetTextureAsset("inkwash/fx_sword.png");
    fxLightning = assets.GetTextureAsset("inkwash/fx_lightning.png");
    fxLightningRing = assets.GetTextureAsset("inkwash/fx_lightning_ring.png");
    fxFrost = assets.GetTextureAsset("inkwash/fx_frost.png");
    fxFrostRing = assets.GetTextureAsset("inkwash/fx_frost_ring.png");
    fxPoison = assets.GetTextureAsset("inkwash/fx_poison.png");
    fxPoisonGround = assets.GetTextureAsset("inkwash/fx_poison_ground.png");
    fxFireball = assets.GetTextureAsset("inkwash/fx_fireball.png");
    fxExplosion = assets.GetTextureAsset("inkwash/fx_explosion.png");
    fxMeteor = assets.GetTextureAsset("inkwash/fx_meteor.png");
    fxSingularity = assets.GetTextureAsset("inkwash/fx_singularity.png");
    fxSingularityRing = assets.GetTextureAsset("inkwash/fx_singularity_ring.png");
    fxPrism = assets.GetTextureAsset("inkwash/fx_prism.png");
    fxPlague = assets.GetTextureAsset("inkwash/fx_plague.png");
    fxInk = assets.GetTextureAsset("inkwash/fx_ink.png");
    fxSlash = assets.GetTextureAsset("inkwash/fx_slash.png");

    const bool ok = panel && slot && buttonNormal && buttonPrimary && barTrack && barHealthFill && barXpFill &&
                    titleBackground && body;
    if (!ok) LogWarn("[2D] UI 部分切图/字体缺失，界面会退回纯色绘制");
    return ok;
}

// ================================================================ 导演：主绘制入口
void SurvivorsDirector::DrawUI(World& world, float viewportX, float viewportY, float viewportWidth,
                               float viewportHeight, const Vec4& safeInsets) {
    m_safeInsets = safeInsets;
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
            case State::StageClear: {
                DrawHudScreen(world, c.origin.x, c.origin.y, viewportWidth, viewportHeight, false);
                DrawDim(draw, c, 180);
                DrawStageClearScreen(world, c.origin.x, c.origin.y, viewportWidth, viewportHeight);
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
    if (!toast.empty() && m_state != State::LevelUp && m_state != State::Gacha && m_state != State::StageClear &&
        m_state != State::GameOver) {
        const float timer = m_toastTimer > 0.0f ? m_toastTimer : 2.0f;
        DrawToast(viewportX, viewportY, viewportWidth, viewportHeight, toast, timer);
    }
}

bool SurvivorsDirector::HitPauseButton(float screenX, float screenY) const {
    const Vec4& rect = m_pauseHit;
    return rect.z > 0.0f && rect.w > 0.0f && screenX >= rect.x && screenX <= rect.x + rect.z &&
           screenY >= rect.y && screenY <= rect.y + rect.w;
}

// ================================================================ 主菜单
void SurvivorsDirector::DrawMenuScreen(World& world, float vx, float vy, float vw, float vh) {
    Canvas      c = MakeCanvas(vx, vy, vw, vh);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const bool  portrait = c.h > c.w * 1.15f;

    DrawCoverImage(draw, c, m_ui.titleBackground);
    if (portrait) {
        // 手机竖屏：暖宣纸压亮背景，标题和按钮用墨绿/朱砂，下方留亮给职业立绘。
        draw->AddRectFilledMultiColor(c.P(0, 0), c.P(c.w, c.h), IM_COL32(248, 240, 222, 238),
                                      IM_COL32(248, 240, 222, 220), IM_COL32(248, 240, 222, 118),
                                      IM_COL32(248, 240, 222, 118));
    } else {
        draw->AddRectFilled(c.P(0, 0), c.P(c.w, c.h), IM_COL32(248, 240, 222, 150));
        draw->AddRectFilledMultiColor(c.P(0, 0), c.P(c.w, c.h), IM_COL32(248, 240, 222, 232),
                                      IM_COL32(248, 240, 222, 196), IM_COL32(248, 240, 222, 46),
                                      IM_COL32(248, 240, 222, 46));
    }

    const float brandX = portrait ? Clamp(c.w * 0.08f, 28.0f, 80.0f) : Clamp(c.w * 0.07f, 40.0f, 110.0f);
    const float contentH = 610.0f;
    const float top = portrait ? (std::max)(112.0f, c.h * 0.075f) : (std::max)(24.0f, (c.h - contentH) * 0.5f);
    const float stackW = portrait ? c.w - brandX * 2.0f : 305.0f;
    const float buttonH = portrait ? 64.0f : 52.0f;
    const float buttonGap = portrait ? 76.0f : 61.0f;
    const float buttonFont = portrait ? 22.0f : 20.0f;
    float       y = top;

    if (m_ui.crest) DrawImage(draw, c, m_ui.crest, brandX, y, 84.0f, 84.0f);
    y += 88.0f;
    DrawText(draw, m_ui, c, portrait ? 16.0f : 14.0f, brandX + 2.0f, y, kTag, "水墨江湖 · 自动战斗");
    y += 26.0f;
    DrawTextShadow(draw, m_ui, c, 96.0f, brandX, y, IM_COL32(156, 57, 49, 255),
                   IM_COL32(255, 248, 232, 210), IM_COL32(255, 248, 232, 150), "二毛幸存者");
    y += 122.0f;
    DrawText(draw, m_ui, c, portrait ? 20.0f : 18.0f, brandX + 2.0f, y, kText,
             "踏入水墨江湖，拾取武学，在怪潮中活下去。");
    y += portrait ? 44.0f : 36.0f;

    const bool hasSave = HasSave();
    if (DrawButton(draw, m_ui, c, "##menu_start", "开始游戏", brandX, y, stackW, buttonH, true, false, buttonFont)) {
        PlaySfx("click", 0.6f);
        ShowClassSelect(world);
        return;
    }
    y += buttonGap;
    if (DrawButton(draw, m_ui, c, "##menu_continue", "继续游戏", brandX, y, stackW, buttonH, false, !hasSave,
                   buttonFont)) {
        PlaySfx("click", 0.6f);
        if (LoadGame(world)) {
            PlaySfx("click", 0.6f);
        }
    }
    y += buttonGap;
    if (DrawButton(draw, m_ui, c, "##menu_codex", "技能图鉴", brandX, y, stackW, buttonH, false, false, buttonFont)) {
        PlaySfx("click", 0.6f);
        m_state = State::Codex;
    }
    y += buttonGap;
    if (DrawButton(draw, m_ui, c, "##menu_settings", "设置", brandX, y, stackW, buttonH, false, false, buttonFont)) {
        PlaySfx("click", 0.6f);
        m_settingsReturn = State::Menu;
        m_showSettings = true;
    }
    y += buttonGap;
    if (DrawButton(draw, m_ui, c, "##menu_quit", "退出程序", brandX, y, stackW, buttonH - 6.0f, false, false,
                   buttonFont - 2.0f)) {
        PlaySfx("click", 0.6f);
        m_wantsQuit = true;
    }
    y += buttonH - 6.0f + (portrait ? 8.0f : 6.0f);
    DrawText(draw, m_ui, c, 14.0f, brandX + 2.0f, y,
             hasSave ? kTextDim : kTextMuted,
             hasSave ? "检测到一局未完成的挑战" : "暂无可继续的存档");

    // 手机竖屏：底部放当前职业立绘，让水墨人物填满下半屏。
    if (portrait) {
        if (Texture* heroTexture = m_ui.hero[Clamp(m_playerClass, 0, 2)]) {
            const float heroH = (std::min)(500.0f, c.h * 0.34f);
            const float heroW = heroTexture->Height() > 0
                                    ? heroH * static_cast<float>(heroTexture->Width()) / heroTexture->Height()
                                    : heroH;
            DrawImage(draw, c, heroTexture, c.w * 0.5f - heroW * 0.5f, c.h - heroH - 24.0f, heroW, heroH);
        }
    }
}

// ================================================================ 战斗 HUD
namespace {

// ---------------------------------------------------------------- 手机 HUD 布局（mobile-hud 原型）
// 原型尺寸以 CSS 像素给出；这里按当前画布缩放换算成设计单位，保证不同屏宽下比例一致。
struct HudRect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};

struct MobileHudLayout {
    bool  portrait = false;
    float px = 1.0f;   // 1 CSS 像素对应的设计单位
    float font = 1.0f; // DrawText 字号：CSS 像素 * font
    float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
    HudRect status, pause, map, dock;
    float slot = 0.0f, gap = 0.0f;
};

MobileHudLayout BuildMobileHud(const Canvas& c, const Vec4& safe) {
    MobileHudLayout hud;
    hud.portrait = c.h > c.w;
    hud.px = 1.0f / c.scale;
    hud.font = 1.0f / (c.scale * c.textScale);
    // safe 为屏幕四边安全区内边距（CSS 像素），再留 12px 呼吸位。
    hud.left = (safe.x + 12.0f) * hud.px;
    hud.right = c.w - (safe.z + 12.0f) * hud.px;
    hud.bottom = c.h - (safe.w + 12.0f) * hud.px;
    // 竖屏额外保留顶部空间：部分宿主启动时不报告安全区/胶囊位置。
    const float clearance = hud.portrait ? 60.0f : 0.0f;
    hud.top = ((std::max)(safe.y, clearance) + 14.0f) * hud.px;

    const float available = (std::max)(0.0f, hud.right - hud.left);
    const float row = (std::min)(460.0f * hud.px, available);
    const float rowX = hud.portrait ? std::round((hud.left + hud.right - row) * 0.5f) : hud.left;
    const float statusW = (std::max)(0.0f, row - 49.0f * hud.px);
    hud.status = {rowX, hud.top, statusW, 66.0f * hud.px};
    hud.pause = {rowX + statusW + 5.0f * hud.px, hud.top, 44.0f * hud.px, 44.0f * hud.px};

    const float mapSize = (hud.portrait ? 78.0f : 104.0f) * hud.px;
    const bool  mapBeside = !hud.portrait && available >= row + mapSize + 12.0f * hud.px;
    hud.map = {hud.right - mapSize, mapBeside ? hud.top : hud.top + 74.0f * hud.px, mapSize, mapSize};

    // 本局最多 5 个技能，固定 5 个槽位（空格子提示容量）。
    hud.gap = 5.0f * hud.px;
    const float slotLimit = (available - hud.gap * 4.0f) / static_cast<float>(kSurvivorsMaxLoadout);
    hud.slot = (std::max)(24.0f * hud.px, (std::min)(39.0f * hud.px, slotLimit));
    const float dockW = hud.slot * static_cast<float>(kSurvivorsMaxLoadout) +
                        hud.gap * static_cast<float>(kSurvivorsMaxLoadout - 1);
    hud.dock = {hud.left + (available - dockW) * 0.5f, hud.bottom - hud.slot, dockW, hud.slot};
    return hud;
}

// 紧凑雷达：以玩家为中心显示 22 个世界单位内的敌人/宝箱/掉落，范围外的最近宝箱用边缘箭头指向。
void DrawMobileRadar(ImDrawList* draw, const SurvivorsUIAssets& ui, const Canvas& c, World& world,
                     const MobileHudLayout& hud) {
    const SurvivorsMinimapSnapshot snapshot = BuildSurvivorsMinimapSnapshot(world);
    const float                 px = hud.px;
    const HudRect&              rect = hud.map;
    DrawPanel(draw, ui, c, rect.x, rect.y, rect.w, rect.h);
    DrawText(draw, ui, c, 10.0f * hud.font, rect.x + 7.0f * px, rect.y + 4.0f * px, kTextMuted, "雷达");

    const float inset = 6.0f * px;
    const float innerX = rect.x + inset;
    const float innerY = rect.y + 15.0f * px;
    const float innerW = rect.w - inset * 2.0f;
    const float innerH = rect.h - 21.0f * px;
    draw->AddRectFilled(c.P(innerX, innerY), c.P(innerX + innerW, innerY + innerH), IM_COL32(238, 231, 212, 245),
                        c.V(2.0f * px));
    // 三层同心方框，沿用原型雷达的刻度感。
    for (int i = 0; i < 3; ++i) {
        const float step = static_cast<float>(i) * 6.0f * px;
        draw->AddRect(c.P(innerX + step, innerY + step), c.P(innerX + innerW - step, innerY + innerH - step),
                      IM_COL32(177, 168, 140, 255), 0.0f, 0, c.V(px));
    }

    const float cx = innerX + innerW * 0.5f;
    const float cy = innerY + innerH * 0.5f;
    const float radius = (std::max)(0.0f, (std::min)(innerW, innerH) * 0.5f - 2.0f * px);
    constexpr float kRadarRange = 22.0f;
    auto plot = [&](const Vec2& worldPos, float size, ImU32 color) {
        const float dx = (worldPos.x - snapshot.player.x) / kRadarRange;
        const float dy = (worldPos.y - snapshot.player.y) / kRadarRange;
        if (std::fabs(dx) > 1.0f || std::fabs(dy) > 1.0f) return;
        draw->AddCircleFilled(c.P(cx + dx * radius, cy - dy * radius), c.V(size * px), color, 8);
    };

    float nearestChestDistance = 1e9f;
    Vec2  nearestChestDir(1.0f, 0.0f);
    for (const SurvivorsMinimapMarker& marker : snapshot.markers) {
        if (marker.kind == 2) {
            plot(marker.position, 1.6f, IM_COL32(178, 59, 59, 220));
        } else if (marker.kind == 1) {
            plot(marker.position, 1.4f, IM_COL32(46, 122, 87, 235));
        } else {
            plot(marker.position, 2.0f, IM_COL32(190, 120, 52, 255));
            const Vec2  offset = marker.position - snapshot.player;
            const float distance = Length(offset);
            if (distance < nearestChestDistance) {
                nearestChestDistance = distance;
                nearestChestDir = distance > 0.001f ? offset / distance : Vec2(1.0f, 0.0f);
            }
        }
    }
    // 玩家：原型中的青绿小方块。
    draw->AddRectFilled(c.P(cx - 2.5f * px, cy - 2.5f * px), c.P(cx + 2.5f * px, cy + 2.5f * px),
                        kInkGreen, c.V(px));
    // 雷达范围外的最近宝箱：在边缘画一个金色箭头。
    if (nearestChestDistance > kRadarRange && nearestChestDistance < 1e8f) {
        const float sx = nearestChestDir.x;
        const float sy = -nearestChestDir.y;
        const float ax = cx + sx * radius * 0.86f;
        const float ay = cy + sy * radius * 0.86f;
        const ImVec2 tip = c.P(ax + sx * 5.0f * px, ay + sy * 5.0f * px);
        const ImVec2 left = c.P(ax - sy * 3.5f * px, ay + sx * 3.5f * px);
        const ImVec2 right = c.P(ax + sy * 3.5f * px, ay - sx * 3.5f * px);
        draw->AddTriangleFilled(tip, left, right, IM_COL32(190, 120, 52, 240));
    }
}

void DrawSurvivorsMinimap(ImDrawList* draw, const SurvivorsUIAssets& ui, const Canvas& c, World& world,
                          float time) {
    const SurvivorsMinimapSnapshot snapshot = BuildSurvivorsMinimapSnapshot(world);
    const bool mobile = c.textScale > 1.0f;
    // 小地图固定在右上角（避开手机端 vConsole/调试按钮），竖版并扩大。
    const float panelW = mobile ? 250.0f : 260.0f;
    const float panelH = mobile ? 280.0f : 300.0f;
    const float panelX = c.w - panelW - (mobile ? 16.0f : 24.0f);
    const float panelY = mobile ? 88.0f : 72.0f;
    DrawPanel(draw, ui, c, panelX, panelY, panelW, panelH);
    DrawText(draw, ui, c, 14.0f, panelX + 14.0f, panelY + 9.0f, kTag, "地图");

    const float headerH = 32.0f;
    const float legendH = 46.0f;
    const float mapX = panelX + 12.0f;
    const float mapY = panelY + headerH;
    const float mapW = panelW - 24.0f;
    const float mapH = panelH - headerH - 12.0f - legendH;
    draw->AddRectFilled(c.P(mapX, mapY), c.P(mapX + mapW, mapY + mapH), IM_COL32(238, 231, 212, 245), c.V(4.0f));
    draw->AddRect(c.P(mapX, mapY), c.P(mapX + mapW, mapY + mapH), kCardBorder, c.V(4.0f),
                  c.V(1.0f));

    const Vec2 half = snapshot.arenaHalf;
    // 保持地图真实宽高比，竖版面板内上下居中，避免被拉伸。
    const float arenaW = half.x * 2.0f;
    const float arenaH = half.y * 2.0f;
    float viewW = mapW;
    float viewH = mapW * (arenaH / arenaW);
    if (viewH > mapH) {
        viewH = mapH;
        viewW = mapH * (arenaW / arenaH);
    }
    const float viewX = mapX + (mapW - viewW) * 0.5f;
    const float viewY = mapY + (mapH - viewH) * 0.5f;
    float mapWorldLeft = -half.x;
    float mapWorldBottom = -half.y;
    float mapWorldW = arenaW;
    float mapWorldH = arenaH;
    if (mobile) {
        // 手机竖版：按面板比例放大到接近整高，宽度裁到玩家附近，地图内容更大。
        const float aspect = mapW / mapH;
        float vH = arenaH * 0.95f;
        float vW = vH * aspect;
        if (vW > arenaW) { vW = arenaW; vH = vW / aspect; }
        const float maxX = (std::max)(0.0f, half.x - vW * 0.5f);
        const float maxY = (std::max)(0.0f, half.y - vH * 0.5f);
        const float cx = Clamp(snapshot.player.x, -maxX, maxX);
        const float cy = Clamp(snapshot.player.y, -maxY, maxY);
        mapWorldLeft = cx - vW * 0.5f;
        mapWorldBottom = cy - vH * 0.5f;
        mapWorldW = vW;
        mapWorldH = vH;
    }
    const float mapScreenX = mobile ? mapX : viewX;
    const float mapScreenY = mobile ? mapY : viewY;
    const float mapScreenW = mobile ? mapW : viewW;
    const float mapScreenH = mobile ? mapH : viewH;
    auto toMap = [&](const Vec2& position) {
        const float u = Clamp01((position.x - mapWorldLeft) / mapWorldW);
        const float v = Clamp01((position.y - mapWorldBottom) / mapWorldH);
        return c.P(mapScreenX + u * mapScreenW, mapScreenY + (1.0f - v) * mapScreenH);
    };

    // 底部图例：怪 / 宝箱 / 掉落，竖版面板留白用来放说明。
    {
        const float legendY = panelY + panelH - 34.0f;
        const float dotY = legendY + 7.0f;
        auto legend = [&](float x, ImU32 color, const char* label) {
            draw->AddCircleFilled(c.P(x, dotY), c.V(3.0f), color, 8);
            DrawText(draw, ui, c, 11.0f, x + 8.0f, legendY, kTextMuted, label);
        };
        legend(panelX + 16.0f, IM_COL32(178, 59, 59, 220), "怪");
        legend(panelX + 72.0f, IM_COL32(190, 120, 52, 255), "宝箱");
        legend(panelX + 140.0f, IM_COL32(46, 122, 87, 220), "掉落");
    }

    for (const SurvivorsMinimapRect& obstacle : snapshot.obstacles) {
        const ImVec2 min = toMap(obstacle.center - obstacle.half);
        const ImVec2 max = toMap(obstacle.center + obstacle.half);
        draw->AddRectFilled(ImVec2((std::min)(min.x, max.x), (std::min)(min.y, max.y)),
                            ImVec2((std::max)(min.x, max.x), (std::max)(min.y, max.y)),
                            IM_COL32(142, 151, 128, 200), c.V(1.0f));
    }

    // 最近宝箱的方向/距离，用于屏幕上方罗盘。
    float nearestDistance = 1e9f;
    Vec2  nearestDirection(1.0f, 0.0f);
    for (const SurvivorsMinimapMarker& marker : snapshot.markers) {
        if (marker.kind == 2) {
            const ImVec2 p = toMap(marker.position);
            draw->AddCircleFilled(p, c.V(1.7f), IM_COL32(178, 59, 59, 190), 8);
            continue;
        }
        if (marker.kind == 1) {
            draw->AddCircleFilled(toMap(marker.position), c.V(2.2f), IM_COL32(46, 122, 87, 220), 8);
            continue;
        }
        const ImVec2 p = toMap(marker.position);
        const float pulse = 0.5f + 0.5f * std::sin(time * 5.0f + marker.position.x);
        draw->AddCircleFilled(p, c.V(5.5f + pulse * 2.0f), IM_COL32(190, 120, 52, 55), 12);
        draw->AddCircleFilled(p, c.V(3.4f), IM_COL32(190, 120, 52, 255), 12);
        draw->AddLine(ImVec2(p.x - 5.0f, p.y), ImVec2(p.x + 5.0f, p.y), IM_COL32(238, 231, 212, 240), 1.0f);
        draw->AddLine(ImVec2(p.x, p.y - 5.0f), ImVec2(p.x, p.y + 5.0f), IM_COL32(238, 231, 212, 240), 1.0f);

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
    draw->AddCircleFilled(playerPoint, c.V(4.0f), kInkGreen, 12);
    draw->AddCircle(playerPoint, c.V(6.0f), kPaper, 12, c.V(1.5f));

    // 顶部罗盘：指出最近抽奖宝箱的方向与距离
    if (nearestDistance < 1e8f) {
        const float cx = c.w * 0.5f;
        const float cy = 92.0f;
        const float sx = nearestDirection.x;
        const float sy = -nearestDirection.y;
        const ImVec2 tip(cx + sx * 13.0f, cy + sy * 13.0f);
        const ImVec2 left(cx - sx * 3.0f - sy * 6.0f, cy - sy * 3.0f + sx * 6.0f);
        const ImVec2 right(cx - sx * 3.0f + sy * 6.0f, cy - sy * 3.0f - sx * 6.0f);
        draw->AddTriangleFilled(tip, left, right, IM_COL32(190, 120, 52, 240));
        char text[64];
        std::snprintf(text, sizeof(text), "最近增益 %.1fm", nearestDistance);
        DrawText(draw, ui, c, 12.0f, cx + 22.0f, cy - 8.0f, kTag, text);
    }
}

} // namespace

// ================================================================ 教学提示
namespace {
const char* TutorialStepTitle(int step) {
    switch (step) {
        case 0: return "第一步 · 移动角色";
        case 1: return "第二步 · 拾取灵气";
        case 2: return "第三步 · 习得新法术";
        case 3: return "第四步 · 强化本命法术";
        default: return "训练完成";
    }
}

const char* TutorialStepHint(int step) {
    switch (step) {
        case 0: return "WASD / 拖动屏幕，移动一小段距离";
        case 1: return "走到灵气光点旁，拾取经验";
        case 2: return "走到绿色光点旁，拾取新法术";
        case 3: return "走到强化光点旁，提升本命法术";
        default: return "怪潮来了：满屏法术清场";
    }
}

const char* TutorialCheckLabel(int step) {
    switch (step) {
        case 0: return "移动";
        case 1: return "拾取灵气";
        case 2: return "新法术";
        case 3: return "强化";
        default: return "实战";
    }
}

void DrawCheckMark(ImDrawList* draw, const Canvas& c, float x, float y, float size, ImU32 color) {
    const float w = c.V((std::max)(1.5f, size * 0.20f));
    draw->AddLine(c.P(x - size * 0.42f, y), c.P(x, y + size * 0.40f), color, w);
    draw->AddLine(c.P(x, y + size * 0.40f), c.P(x + size * 0.52f, y - size * 0.42f), color, w);
}
} // namespace

void SurvivorsDirector::DrawTutorialHint(World& world, float vx, float vy, float vw, float vh, bool mobile) {
    Canvas      c = MakeCanvas(vx, vy, vw, vh);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const int   cls = Clamp(m_playerClass, 0, kSurvivorsClassCount - 1);

    const bool gate = (m_tutorialGate || m_tutorialFlash > 0.0f) &&
                      m_tutorialStep < kSurvivorsTutorialStepCount;

    if (!gate) {
        m_tutorialSkipHit = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
        // 实战阶段：小面板展示成长方向，不遮挡操作。
        float panelW = mobile ? (std::min)(c.w - 24.0f, 680.0f) : 500.0f;
        float panelX = mobile ? c.w * 0.5f - panelW * 0.5f : 24.0f;
        float panelH = mobile ? 188.0f : 196.0f;
        float panelY = 158.0f;
        if (mobile) {
            const float px = 1.0f / c.scale;
            const float bottom = c.h - (m_safeInsets.w + 12.0f) * px;
            panelY = bottom - 78.0f - 14.0f - panelH;
        }
        DrawPanel(draw, m_ui, c, panelX, panelY, panelW, panelH);
        DrawText(draw, m_ui, c, mobile ? 20.0f : 18.0f, panelX + 16.0f, panelY + 12.0f, kTag,
                 "训练完成 · 怪潮来了");
        DrawWrapped(draw, m_ui, c, mobile ? 17.0f : 15.0f, panelX + 16.0f, panelY + 50.0f, panelW - 32.0f,
                    kText, "满屏小怪 + 满屏法术，这就是本作的爽点。", 1.40f);
        DrawWrapped(draw, m_ui, c, mobile ? 15.0f : 13.0f, panelX + 16.0f, panelY + 92.0f, panelW - 32.0f,
                    kMint, kSurvivorsClasses[cls].growth, 1.35f);
        DrawWrapped(draw, m_ui, c, mobile ? 14.0f : 12.5f, panelX + 16.0f, panelY + 142.0f, panelW - 32.0f,
                    kTextDim, "雷霆：Lv.3 双雷 · Lv.7 四雷 · Lv.17 九雷；范围随等级扩大", 1.35f);
        return;
    }

    // 分步操作：压暗战场，中央宣纸法册；未完成操作不恢复刷怪与计时。
    draw->AddRectFilled(c.P(0, 0), c.P(c.w, c.h), IM_COL32(32, 42, 34, 66));
    const float panelW = mobile ? (std::min)(c.w - 24.0f, 720.0f) : 680.0f;
    const float panelX = mobile ? c.w * 0.5f - panelW * 0.5f : 24.0f;
    const float panelH = mobile ? 392.0f : 318.0f;
    float       panelY = 158.0f;
    if (mobile) {
        const float px = 1.0f / c.scale;
        const float bottom = c.h - (m_safeInsets.w + 12.0f) * px;
        panelY = bottom - 78.0f - 14.0f - panelH;
    }
    DrawPanel(draw, m_ui, c, panelX, panelY, panelW, panelH);

    char stepLabel[64];
    std::snprintf(stepLabel, sizeof(stepLabel), "%d / %d", (std::min)(m_tutorialStep + 1, kSurvivorsTutorialStepCount),
                  kSurvivorsTutorialStepCount);
    DrawText(draw, m_ui, c, mobile ? 16.0f : 15.0f, panelX + 24.0f, panelY + 16.0f, kTag, "初入江湖 · 教学");
    DrawTextRight(draw, m_ui, c, mobile ? 16.0f : 15.0f, panelX + panelW - 24.0f, panelY + 16.0f, kTextMuted,
                  stepLabel);

    if (m_tutorialFlash > 0.0f) {
        DrawTextCentered(draw, m_ui, c, mobile ? 34.0f : 32.0f, panelX + panelW * 0.5f, panelY + 46.0f, kInkGreen,
                         "步骤完成");
        const float doneW = MeasureText(m_ui, c, mobile ? 34.0f : 32.0f, "步骤完成").x / c.scale;
        DrawCheckMark(draw, c, panelX + panelW * 0.5f - doneW * 0.5f - (mobile ? 26.0f : 22.0f),
                      panelY + 62.0f, mobile ? 16.0f : 14.0f, kInkGreen);
        DrawTextCentered(draw, m_ui, c, mobile ? 18.0f : 16.0f, panelX + panelW * 0.5f, panelY + 110.0f, kTextDim,
                         "很好，继续下一步");
    } else {
        DrawTextCentered(draw, m_ui, c, mobile ? 31.0f : 29.0f, panelX + panelW * 0.5f, panelY + 44.0f, kInkGreenDark,
                         TutorialStepTitle(m_tutorialStep));
        DrawTextCentered(draw, m_ui, c, mobile ? 19.0f : 17.0f, panelX + panelW * 0.5f, panelY + 88.0f, kText,
                         TutorialStepHint(m_tutorialStep));
    }

    // 四步清单：已完成打勾，当前步高亮。
    const float listY = panelY + (mobile ? 160.0f : 138.0f);
    const float rowH = mobile ? 44.0f : 34.0f;
    for (int step = 0; step < kSurvivorsTutorialStepCount; ++step) {
        const float rowY = listY + rowH * static_cast<float>(step);
        const bool  done = step < m_tutorialStep;
        const bool  current = step == m_tutorialStep && m_tutorialFlash <= 0.0f;
        if (current) {
            draw->AddRectFilled(c.P(panelX + 20.0f, rowY - 6.0f), c.P(panelX + panelW - 20.0f, rowY + rowH - 10.0f),
                                IM_COL32(222, 234, 212, 255), c.V(4.0f));
        }
        const float circleX = panelX + 38.0f;
        const float circleY = rowY + rowH * 0.35f;
        if (done) {
            draw->AddCircleFilled(c.P(circleX, circleY), c.V(12.0f), kInkGreen, 16);
            DrawCheckMark(draw, c, circleX, circleY, 12.0f, kPaper);
        } else {
            draw->AddCircleFilled(c.P(circleX, circleY), c.V(12.0f), current ? kTag : kPaperDeep, 16);
            draw->AddCircle(c.P(circleX, circleY), c.V(12.0f), current ? kInkGreen : kPaperBorder, 16, c.V(1.0f));
            char number[8];
            std::snprintf(number, sizeof(number), "%d", step + 1);
            DrawTextCentered(draw, m_ui, c, 14.0f, circleX, circleY - 8.0f, current ? kPaper : kTextMuted, number);
        }
        DrawText(draw, m_ui, c, mobile ? 19.0f : 17.0f, panelX + 62.0f, rowY, current ? kInkGreenDark : kText,
                 TutorialCheckLabel(step));
        if (done) {
            DrawTextRight(draw, m_ui, c, mobile ? 15.0f : 14.0f, panelX + panelW - 28.0f, rowY + 2.0f, kInkGreen,
                          "完成");
        }
    }

    const float skipW = mobile ? 132.0f : 118.0f;
    const float skipH = mobile ? 40.0f : 36.0f;
    const float skipX = panelX + panelW - skipW - 20.0f;
    const float skipY = panelY + panelH - skipH - 12.0f;
    DrawText(draw, m_ui, c, mobile ? 15.0f : 13.5f, panelX + 24.0f, skipY + 8.0f, kTextMuted,
             "完成当前操作后自动继续");
    if (DrawButton(draw, m_ui, c, "##tutorial_skip", "跳过教学", skipX, skipY, skipW, skipH, false, false,
                   mobile ? 15.0f : 14.0f)) {
        SkipTutorial(world);
        return;
    }
    m_tutorialSkipHit = Vec4(c.P(skipX, skipY).x, c.P(skipX, skipY).y, c.V(skipW), c.V(skipH));
}

void SurvivorsDirector::DrawHudScreen(World& world, float vx, float vy, float vw, float vh, bool interactive) {
    Canvas      c = MakeCanvas(vx, vy, vw, vh);

    // 手机竖屏/横屏走 mobile-hud 原型的紧凑布局（顶栏合并状态、雷达缩小、5 槽固定底栏）。
    if (c.textScale > 1.0f) {
        DrawMobileHudScreen(world, vx, vy, vw, vh, interactive);
        return;
    }

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

    // 等级 / 计时 / 击杀；竖屏放到生命/经验面板下方，避免和左上角重叠。
    const bool  portrait = c.h > c.w;
    const float timerW = 342.0f;
    const float timerX = portrait ? 24.0f : c.w * 0.5f - timerW * 0.5f;
    const float timerY = portrait ? 154.0f : 20.0f;
    DrawPanel(draw, m_ui, c, timerX, timerY, timerW, 56.0f);
    std::snprintf(value, sizeof(value), "等级 %d", level);
    DrawText(draw, m_ui, c, 15.0f, timerX + 20.0f, timerY + 18.0f, kTextDim, value);
    const std::string clock = FormatClock(m_time);
    DrawTextCentered(draw, m_ui, c, 30.0f, timerX + timerW * 0.5f, timerY + 10.0f, kGoldBright, clock.c_str());
    std::snprintf(value, sizeof(value), "击杀 %d", m_kills);
    DrawTextRight(draw, m_ui, c, 15.0f, timerX + timerW - 20.0f, timerY + 18.0f, kTextDim, value);

    // 20 层关卡：层数 + 本层通关条件/进度（联机副本的层数由主机决定，客户端不显示本地位面）
    if (!m_networkReplica) {
        const float stageW = 430.0f;
        const float stageX = c.w * 0.5f - stageW * 0.5f;
        const float stageY = timerY + 64.0f;
        DrawPanel(draw, m_ui, c, stageX, stageY, stageW, 42.0f);
        char floorText[48];
        std::snprintf(floorText, sizeof(floorText), "第 %d / %d 层", m_floor, kSurvivorsFloorCount);
        DrawText(draw, m_ui, c, 15.0f, stageX + 16.0f, stageY + 12.0f, kTag, floorText);
        DrawTextRight(draw, m_ui, c, 14.0f, stageX + stageW - 16.0f, stageY + 13.0f, kText,
                      StageGoalProgressText(world).c_str());
    }

    // 右上暂停
    const float pauseW = 136.0f;
    const float pauseX = c.w - 24.0f - pauseW;
    m_pauseHit = Vec4(c.P(pauseX, 20.0f).x, c.P(pauseX, 20.0f).y, c.V(pauseW), c.V(44.0f));
    if (interactive) {
        if (DrawButton(draw, m_ui, c, "##hud_pause", "Esc 暂停", pauseX, 20.0f, pauseW, 44.0f, false, false, 17.0f)) {
            TogglePause();
            return;
        }
    } else {
        DrawSlot(draw, c, pauseX, 20.0f, pauseW, 44.0f);
        DrawTextCentered(draw, m_ui, c, 17.0f, pauseX + pauseW * 0.5f, 33.0f, kInkGreenDark, "Esc 暂停");
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
        DrawSlot(draw, c, x, slotY, slotSize, slotSize);
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
    if (m_tutorialPending && !m_networkReplica) DrawTutorialHint(world, vx, vy, vw, vh, false);
}

// mobile-hud 原型的战斗 HUD：一块顶栏（等级/计时/击杀 + 生命/经验）、44px 暂停键、
// 右上雷达、底部 5 个固定技能槽；摇杆不再绘制，整块屏幕都可以按下拖动。
void SurvivorsDirector::DrawMobileHudScreen(World& world, float vx, float vy, float vw, float vh,
                                             bool interactive) {
    Canvas      c = MakeCanvas(vx, vy, vw, vh);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    auto*       player = PlayerOf(world);
    const MobileHudLayout hud = BuildMobileHud(c, m_safeInsets);
    const float           px = hud.px;

    const float hp = player ? player->hp : 0.0f;
    const float maxHp = player ? player->maxHp : 100.0f;
    const float xp = player ? player->xp : 0.0f;
    const float xpNext = player ? player->xpNext : 12.0f;
    const int   level = player ? player->level : 1;
    const float hpRatio = maxHp > 0.0f ? Clamp01(hp / maxHp) : 0.0f;
    const float xpRatio = xpNext > 0.0f ? Clamp01(xp / xpNext) : 0.0f;

    // ------------------------------------------------------------ 顶栏状态
    const HudRect& s = hud.status;
    DrawPanel(draw, m_ui, c, s.x, s.y, s.w, s.h);
    char value[64];
    std::snprintf(value, sizeof(value), "LV %d", level);
    DrawText(draw, m_ui, c, 12.0f * hud.font, s.x + 10.0f * px, s.y + 9.0f * px, kGold, value);
    DrawTextCentered(draw, m_ui, c, 22.0f * hud.font, s.x + s.w * 0.5f, s.y + 4.0f * px, kGoldBright,
                     FormatClock(m_time).c_str());
    std::snprintf(value, sizeof(value), "击杀 %d", m_kills);
    DrawTextRight(draw, m_ui, c, 11.5f * hud.font, s.x + s.w - 10.0f * px, s.y + 10.0f * px, kTextDim, value);
    draw->AddLine(c.P(s.x + 10.0f * px, s.y + 30.0f * px), c.P(s.x + s.w - 10.0f * px, s.y + 30.0f * px),
                  IM_COL32(130, 100, 60, 120), c.V(px));

    const float barX = s.x + 26.0f * px;
    const float barW = (std::max)(0.0f, s.w - 94.0f * px);
    auto compactBar = [&](float y, float ratio, Texture* fillTexture, ImU32 color) {
        draw->AddRectFilled(c.P(barX, y), c.P(barX + barW, y + 8.0f * px), kTrack, c.V(px));
        draw->AddLine(c.P(barX, y + 7.5f * px), c.P(barX + barW, y + 7.5f * px), kPaperBorderSoft,
                      c.V(px));
        const float clamped = Clamp01(ratio);
        const float fill = barW * clamped;
        if (fill <= 0.0f) return;
        if (fillTexture) {
            SetSamplerLinear(draw);
            draw->AddImage(TexId(fillTexture), c.P(barX, y), c.P(barX + fill, y + 7.0f * px),
                           ImVec2(0.0f, 0.0f), ImVec2(clamped, 1.0f), IM_COL32_WHITE);
            SetSamplerLinear(draw);
        } else {
            draw->AddRectFilled(c.P(barX, y), c.P(barX + fill, y + 7.0f * px), color);
        }
    };
    if (m_ui.iconHealth) {
        DrawImage(draw, c, m_ui.iconHealth, s.x + 9.0f * px, s.y + 34.0f * px, 12.0f * px, 12.0f * px);
    }
    compactBar(s.y + 36.0f * px, hpRatio, m_ui.barHealthFill, kRed);
    std::snprintf(value, sizeof(value), "%.0f/%.0f", hp, maxHp);
    DrawTextRight(draw, m_ui, c, 10.0f * hud.font, s.x + s.w - 10.0f * px, s.y + 35.0f * px, kText, value);
    if (m_ui.iconXp) {
        DrawImage(draw, c, m_ui.iconXp, s.x + 9.0f * px, s.y + 48.0f * px, 12.0f * px, 12.0f * px);
    }
    compactBar(s.y + 50.0f * px, xpRatio, m_ui.barXpFill, kMint);
    std::snprintf(value, sizeof(value), "%.0f/%.0f", xp, xpNext);
    DrawTextRight(draw, m_ui, c, 10.0f * hud.font, s.x + s.w - 10.0f * px, s.y + 49.0f * px, kMint, value);

    // ------------------------------------------------------------ 20 层：层数 + 本层条件
    if (!m_networkReplica) {
        const float pillY = s.y + s.h + 5.0f * px;
        const float pillH = 23.0f * px;
        const float pillW = hud.portrait ? (std::max)(100.0f * px, s.w - hud.map.w - 8.0f * px) : s.w;
        DrawPanel(draw, m_ui, c, s.x, pillY, pillW, pillH);
        char floorText[48];
        std::snprintf(floorText, sizeof(floorText), "第 %d/%d 层", m_floor, kSurvivorsFloorCount);
        DrawText(draw, m_ui, c, 12.5f * hud.font, s.x + 8.0f * px, pillY + 3.0f * px, kTag, floorText);
        DrawTextRight(draw, m_ui, c, 12.5f * hud.font, s.x + pillW - 8.0f * px, pillY + 3.0f * px, kText,
                      StageGoalProgressText(world).c_str());
    }

    // ------------------------------------------------------------ 暂停键
    const HudRect& p = hud.pause;
    m_pauseHit = Vec4(c.P(p.x, p.y).x, c.P(p.x, p.y).y, c.V(p.w), c.V(p.h));
    if (interactive) {
        ImGui::SetCursorScreenPos(c.P(p.x, p.y));
        if (ImGui::InvisibleButton("##hud_pause", c.S(p.w, p.h))) {
            TogglePause();
            return;
        }
    }
    DrawPanel(draw, m_ui, c, p.x, p.y, p.w, p.h);
    draw->AddRectFilled(c.P(p.x + 16.0f * px, p.y + 14.0f * px), c.P(p.x + 20.0f * px, p.y + 30.0f * px),
                        kInkGreenDark);
    draw->AddRectFilled(c.P(p.x + 24.0f * px, p.y + 14.0f * px), c.P(p.x + 28.0f * px, p.y + 30.0f * px),
                        kInkGreenDark);

    // ------------------------------------------------------------ 技能槽（固定 5 个）
    int slotSkills[kSurvivorsMaxLoadout];
    for (int& skill : slotSkills) skill = -1;
    int slotCount = 0;
    if (player) {
        for (int skill = 0; skill < kSurvivorsSkillCount && slotCount < kSurvivorsMaxLoadout; ++skill) {
            if (player->HasSkill(skill)) slotSkills[slotCount++] = skill;
        }
    }
    int hoveredSkill = -1;
    for (int slot = 0; slot < kSurvivorsMaxLoadout; ++slot) {
        const int   skill = slotSkills[slot];
        const float x = hud.dock.x + (hud.slot + hud.gap) * static_cast<float>(slot);
        const float y = hud.dock.y;
        const float size = hud.slot;
        if (skill < 0) {
            DrawSlot(draw, c, x, y, size, size, IM_COL32(255, 255, 255, 0));
            DrawSlot(draw, c, x, y, size, size, IM_COL32(255, 255, 255, 0));
            DrawTextCentered(draw, m_ui, c, 14.0f * hud.font, x + size * 0.5f, y + size * 0.5f - 7.0f * px,
                             kTextMuted, "·");
            continue;
        }
        if (interactive) {
            ImGui::SetCursorScreenPos(c.P(x, y));
            if (ImGui::InvisibleButton((std::string("##skill") + std::to_string(skill)).c_str(),
                                       c.S(size, size))) {
                m_selectedSkill = (m_selectedSkill == skill) ? -1 : skill;
            }
            if (ImGui::IsItemHovered()) hoveredSkill = skill;
        }
        DrawSlot(draw, c, x, y, size, size);
        const float iconSize = size - 8.0f * px;
        if (m_ui.icon[skill]) {
            DrawImage(draw, c, m_ui.icon[skill], x + 4.0f * px, y + 3.0f * px, iconSize, iconSize);
        }
        const int  skillLevel = player ? SkillLevel(*player, skill) : 0;
        char       levelText[16];
        std::snprintf(levelText, sizeof(levelText), "%d", skillLevel);
        const float badgeW = 13.0f * px;
        const float badgeH = 10.0f * px;
        draw->AddRectFilled(c.P(x + size - badgeW - 2.0f * px, y + size - badgeH - 2.0f * px),
                            c.P(x + size - 2.0f * px, y + size - 2.0f * px), kPaperDeep,
                            c.V(2.0f * px));
        DrawTextCentered(draw, m_ui, c, 9.0f * hud.font, x + size - badgeW * 0.5f - 2.0f * px,
                         y + size - badgeH - 1.0f * px, kInkGreenDark, levelText);
    }
    if (interactive && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
        m_selectedSkill = -1;
    }

    // 技能说明：悬停优先，点击可钉住（桌面/平板鼠标仍可用；触屏按下屏幕会直接移动）。
    const int shownSkill = (interactive && hoveredSkill >= 0) ? hoveredSkill : (interactive ? m_selectedSkill : -1);
    if (shownSkill >= 0 && shownSkill < kSurvivorsSkillCount && player) {
        const float tipW = (std::min)(342.0f, c.w - 48.0f);
        const float tipH = 106.0f;
        const float tipX = c.w * 0.5f - tipW * 0.5f;
        const float tipY = hud.dock.y - tipH - 14.0f;
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

    DrawMobileRadar(draw, m_ui, c, world, hud);
    if (m_tutorialPending && !m_networkReplica) DrawTutorialHint(world, vx, vy, vw, vh, true);
}

// ================================================================ 升级三选一
void SurvivorsDirector::DrawLevelUpScreen(World& world, float vx, float vy, float vw, float vh) {
    Canvas      c = MakeCanvas(vx, vy, vw, vh);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    auto*       player = PlayerOf(world);
    if (!player) return;

    const float panelW = FitPanelWidth(c, 860.0f);
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

        DrawCard(draw, m_ui, c, cardX, cardY, cardW, cardH, hovered);
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
    char pauseHint[96];
    std::snprintf(pauseHint, sizeof(pauseHint), "Esc 继续战斗 · %s 测试版 · 暂停时玩法与计时冻结",
                  kSurvivorsVersion);
    DrawTextCentered(draw, m_ui, c, 12.0f, c.w * 0.5f, panelY + panelH - 22.0f, kTextMuted, pauseHint);
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
        DrawSheetFrameRotated(draw, c, m_ui.chestClosed, 1, 1, 0, chestX, chestY, 210.0f * chestScale,
                              184.0f * chestScale, IM_COL32_WHITE, tilt);
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
        const int   explosionFrame = Clamp(static_cast<int>(explosionT * 15.0f), 0, 15);
        DrawSheetFrameRotated(draw, c, m_ui.fxExplosion, 4, 4, explosionFrame, chestX, chestY,
                              250.0f + 70.0f * explosionT, 250.0f + 70.0f * explosionT,
                              IM_COL32(255, 196, 120, static_cast<int>(190.0f * (1.0f - 0.55f * explosionT))), 0.0f);

        const float openAlpha = 1.0f - 0.72f * Clamp01((openTime - 0.30f) / 0.90f);
        DrawSheetFrameRotated(draw, c, m_ui.chestOpen, 1, 1, 0, chestX, chestY, 180.0f, 206.0f,
                              ToImColor(Vec4(1, 1, 1, 1), openAlpha), 0.0f);
    }

    // 顶部标题：开箱期间提示，翻牌后让位给卡面。
    if (t < reveal) {
        DrawTextCentered(draw, m_ui, c, 30.0f, cx, cy - 245.0f, kPaper, "增益宝箱开启");
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
        draw->AddRectFilled(cardMin, cardMax, IM_COL32(246, 238, 220, 250), c.V(10.0f * scale));
        draw->AddRectFilledMultiColor(cardMin, cardMax, ToImColor(rarityColor, 0.16f), ToImColor(rarityColor, 0.06f),
                                      IM_COL32(246, 238, 220, 0), IM_COL32(246, 238, 220, 0));
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
            const ImU32  color = i < rarity + 1 ? rarityU32 : IM_COL32(203, 194, 170, 220);
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

// ================================================================ 层间结算
void SurvivorsDirector::DrawStageClearScreen(World& world, float vx, float vy, float vw, float vh) {
    Canvas      c = MakeCanvas(vx, vy, vw, vh);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const bool  portrait = c.h > c.w * 1.15f;

    const float panelW = FitPanelWidth(c, portrait ? 760.0f : 960.0f);
    const float panelH = portrait ? 820.0f : 760.0f;
    const float panelX = c.w * 0.5f - panelW * 0.5f;
    const float panelY = c.h * 0.5f - panelH * 0.5f;
    DrawPanel(draw, m_ui, c, panelX, panelY, panelW, panelH);

    char tag[64];
    std::snprintf(tag, sizeof(tag), "第 %d / %d 层", m_floor, kSurvivorsFloorCount);
    DrawSectionTitle(draw, m_ui, c, panelX + 30.0f, panelY + 24.0f, panelW - 60.0f, "层数突破", "通关成功",
                     tag, true);

    char goalLine[160];
    if (m_stageGoal.type == static_cast<int>(SurvivorsGoalType::Tutorial)) {
        std::snprintf(goalLine, sizeof(goalLine), "本层条件：完成教学");
    } else {
        std::snprintf(goalLine, sizeof(goalLine), "本层条件：%s", StageGoalProgressText(world).c_str());
    }
    DrawTextCentered(draw, m_ui, c, 15.0f, c.w * 0.5f, panelY + 112.0f, kText, goalLine);

    char stats[128];
    std::snprintf(stats, sizeof(stats), "本局总击杀 %d · 总用时 %s", m_kills, FormatClock(m_time).c_str());
    DrawTextCentered(draw, m_ui, c, 13.0f, c.w * 0.5f, panelY + 138.0f, kTextDim, stats);

    const int nextFloor = (std::min)(m_floor + 1, kSurvivorsFloorCount);
    char      nextTitle[96];
    std::snprintf(nextTitle, sizeof(nextTitle), "下一层 · 第 %d 层", nextFloor);
    DrawTextCentered(draw, m_ui, c, 16.0f, c.w * 0.5f, panelY + 162.0f, kInkGreenDark, nextTitle);
    char nextGoal[160];
    switch (static_cast<SurvivorsGoalType>(m_nextStageGoal.type)) {
        case SurvivorsGoalType::SurviveTime:
            std::snprintf(nextGoal, sizeof(nextGoal), "随机条件：存活 %d 秒", static_cast<int>(m_nextStageGoal.target));
            break;
        case SurvivorsGoalType::KillCount:
            std::snprintf(nextGoal, sizeof(nextGoal), "随机条件：击杀 %d 只怪物",
                          static_cast<int>(m_nextStageGoal.target));
            break;
        case SurvivorsGoalType::EliteHunt:
            std::snprintf(nextGoal, sizeof(nextGoal), "随机条件：击杀 %d 名精英",
                          static_cast<int>(m_nextStageGoal.target));
            break;
        case SurvivorsGoalType::GemCollect:
            std::snprintf(nextGoal, sizeof(nextGoal), "随机条件：拾取 %d 枚经验",
                          static_cast<int>(m_nextStageGoal.target));
            break;
        case SurvivorsGoalType::SkillLevelSum:
            std::snprintf(nextGoal, sizeof(nextGoal), "随机条件：武学总等级达到 %d",
                          static_cast<int>(m_nextStageGoal.target));
            break;
        default: std::snprintf(nextGoal, sizeof(nextGoal), "随机条件：通关"); break;
    }
    DrawTextCentered(draw, m_ui, c, 14.0f, c.w * 0.5f, panelY + 186.0f, kGoldBright, nextGoal);

    DrawRule(draw, c, panelX + 30.0f, panelY + 214.0f, panelW - 60.0f);
    DrawTextCentered(draw, m_ui, c, 20.0f, c.w * 0.5f, panelY + 230.0f, kInkGreenDark,
                     "翻牌奖励 · 任选一张");
    DrawTextCentered(draw, m_ui, c, 13.0f, c.w * 0.5f, panelY + 260.0f, kTextMuted,
                     "每次通关都有一次强化；点击背面卡牌翻开");

    auto* player = PlayerOf(world);
    const float gap = portrait ? 10.0f : 16.0f;
    const float cardW = (panelW - 60.0f - gap * 2.0f) / 3.0f;
    const float cardH = portrait ? 214.0f : 236.0f;
    const float cardY = panelY + 286.0f;
    const float cardX0 = panelX + 30.0f;
    if (m_rewardChosen == 0 || m_rewardChosen == 1 || m_rewardChosen == 2) {
        m_rewardFlipTime = (std::min)(0.45f, m_rewardFlipTime + ImGui::GetIO().DeltaTime);
    }

    for (int i = 0; i < 3; ++i) {
        const float x = cardX0 + (cardW + gap) * static_cast<float>(i);
        const SurvivorsStageReward& reward = m_stageRewards[i];
        const bool chosen = (m_rewardChosen == i);
        const bool locked = (m_rewardChosen >= 0 && !chosen);
        const float alpha = locked ? 0.42f : 1.0f;
        const float angle = chosen ? (m_rewardFlipTime / 0.45f) * 3.14159265f : 0.0f;
        const bool  front = chosen && angle >= 1.5707963f;
        const float scaleX = chosen ? (std::max)(0.06f, std::abs(std::cos(angle))) : 1.0f;
        const float cx = x + cardW * 0.5f;
        const float drawW = cardW * scaleX;
        const float dx = cx - drawW * 0.5f;

        if (m_rewardChosen < 0) {
            ImGui::SetCursorScreenPos(c.P(x, cardY));
            if (ImGui::InvisibleButton((std::string("##stage_reward") + std::to_string(i)).c_str(),
                                       c.S(cardW, cardH))) {
                ApplyStageReward(world, i);
            }
            if (ImGui::IsItemHovered()) {
                draw->AddRect(c.P(x - 3.0f, cardY - 3.0f), c.P(x + cardW + 3.0f, cardY + cardH + 3.0f), kGoldBright,
                              c.V(9.0f), 0, c.V(2.0f));
            }
        }

        if (!front) {
            const ImU32 border = chosen ? kGoldBright : kPaperBorder;
            draw->AddRectFilled(c.P(dx + 2.0f, cardY + 4.0f), c.P(dx + drawW + 2.0f, cardY + cardH + 4.0f),
                                IM_COL32(20, 28, 22, 60), c.V(9.0f));
            draw->AddRectFilled(c.P(dx, cardY), c.P(dx + drawW, cardY + cardH), kInkGreen, c.V(9.0f));
            draw->AddRect(c.P(dx, cardY), c.P(dx + drawW, cardY + cardH), border, c.V(9.0f), 0, c.V(2.0f));
            if (drawW > cardW * 0.35f) {
                const float sealR = (portrait ? 30.0f : 38.0f) * scaleX;
                draw->AddCircleFilled(c.P(cx, cardY + cardH * 0.42f), c.V(sealR), IM_COL32(156, 57, 49, alpha));
                DrawTextCentered(draw, m_ui, c, portrait ? 30.0f : 36.0f, cx, cardY + cardH * 0.42f - 18.0f,
                                 IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)), "翻");
                DrawTextCentered(draw, m_ui, c, 14.0f, cx, cardY + cardH - 52.0f, IM_COL32(230, 224, 208, static_cast<int>(255 * alpha)),
                                 "点击翻开");
            }
        } else if (player) {
            LevelCardInfo info;
            if (reward.unlock) {
                info.tag = "新武学";
                info.name = kSkills[Clamp(reward.choice.skill, 0, 9)].name;
                info.level = "解锁 · Lv.1";
                info.desc = kSkills[Clamp(reward.choice.skill, 0, 9)].desc;
                info.effect = "永久加入本局武学";
            } else {
                info = MakeLevelCardInfo(*player, reward.choice);
            }
            const ImU32 accent = reward.unlock ? kGoldBright : (reward.choice.skill == 100 ? IM_COL32(156, 57, 49, 255) : kInkGreen);
            draw->AddRectFilled(c.P(dx, cardY), c.P(dx + drawW, cardY + cardH), kPaper, c.V(9.0f));
            draw->AddRect(c.P(dx, cardY), c.P(dx + drawW, cardY + cardH), accent, c.V(9.0f), 0, c.V(2.0f));
            DrawTextCentered(draw, m_ui, c, portrait ? 12.0f : 13.0f, cx, cardY + 12.0f, accent, info.tag.c_str());
            Texture* icon = nullptr;
            if (reward.choice.skill >= 0 && reward.choice.skill < 10) icon = m_ui.icon[reward.choice.skill];
            if (!icon) icon = m_ui.icon[0];
            if (icon) {
                const float iconSize = portrait ? 38.0f : 46.0f;
                DrawImage(draw, c, icon, cx - iconSize * 0.5f, cardY + 34.0f, iconSize, iconSize);
            }
            DrawTextCentered(draw, m_ui, c, portrait ? 18.0f : 21.0f, cx, cardY + 86.0f, kInkGreenDark,
                             info.name.c_str());
            DrawTextCentered(draw, m_ui, c, portrait ? 12.0f : 13.0f, cx, cardY + 114.0f, kTag,
                             info.level.c_str());
            DrawWrapped(draw, m_ui, c, portrait ? 11.5f : 12.5f, dx + 12.0f, cardY + 138.0f, drawW - 24.0f,
                        kTextDim, info.desc.c_str(), 1.35f);
            DrawTextCentered(draw, m_ui, c, portrait ? 11.5f : 12.5f, cx, cardY + cardH - 28.0f, kGoldBright,
                             info.effect.c_str());
            if (chosen) {
                DrawTextCentered(draw, m_ui, c, 12.0f, cx, cardY + cardH - 12.0f, kTextMuted, "已获得");
            }
        }
    }

    if (m_rewardChosen < 0) {
        DrawTextCentered(draw, m_ui, c, 13.0f, c.w * 0.5f, cardY + cardH + 14.0f, kTextMuted,
                         "翻牌后即可进入下一层");
    }

    const float buttonW = 240.0f;
    const float buttonY = panelY + panelH - 78.0f;
    if (DrawButton(draw, m_ui, c, "##stage_menu", "回到主菜单", c.w * 0.5f - buttonW - 8.0f, buttonY, buttonW,
                   46.0f, false, false, 18.0f)) {
        PlaySfx("click", 0.6f);
        ReturnToMenu();
    }
    char nextLabel[48];
    std::snprintf(nextLabel, sizeof(nextLabel), "进入第 %d 层", nextFloor);
    if (DrawButton(draw, m_ui, c, "##stage_next", nextLabel, c.w * 0.5f + 8.0f, buttonY, buttonW, 46.0f, true,
                   m_rewardChosen < 0, 18.0f)) {
        PlaySfx("click", 0.6f);
        AdvanceStage(world);
    }
}

// ================================================================ 结算
void SurvivorsDirector::DrawResultScreen(World& world, float vx, float vy, float vw, float vh) {
    Canvas      c = MakeCanvas(vx, vy, vw, vh);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    DrawDim(draw, c, 188);
    auto* player = PlayerOf(world);

    const float panelW = FitPanelWidth(c, 840.0f);
    const float panelH = 560.0f;
    const float panelX = c.w * 0.5f - panelW * 0.5f;
    const float panelY = c.h * 0.5f - panelH * 0.5f;
    DrawPanel(draw, m_ui, c, panelX, panelY, panelW, panelH);
    char stageTag[64];
    std::snprintf(stageTag, sizeof(stageTag), "到达第 %d / %d 层", m_floor, kSurvivorsFloorCount);
    if (m_victory) {
        DrawSectionTitle(draw, m_ui, c, panelX + 30.0f, panelY + 26.0f, panelW - 60.0f, "挑战成功", "通关二十层",
                         "水墨江湖 · 天下无双", true);
    } else {
        DrawSectionTitle(draw, m_ui, c, panelX + 30.0f, panelY + 26.0f, panelW - 60.0f, "挑战结束",
                         "倒在怪潮之中", "水墨江湖 · 本局战绩", true);
    }
    if (!m_victory) {
        DrawTextCentered(draw, m_ui, c, 13.5f, c.w * 0.5f, panelY + 102.0f, kTextMuted, stageTag);
    }

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

    const float panelW = FitPanelWidth(c, 840.0f);
    const float panelH = 760.0f;
    const float panelX = c.w * 0.5f - panelW * 0.5f;
    const float panelY = c.h * 0.5f - panelH * 0.5f;
    DrawPanel(draw, m_ui, c, panelX, panelY, panelW, panelH);
    DrawSectionTitle(draw, m_ui, c, panelX + 30.0f, panelY + 26.0f, panelW - 60.0f, "江湖武学", "武学图鉴",
                     "职业给 1 个基础武学，击杀最多再掉落 4 个（共 5 个）。", true);

    const float rowX = panelX + 30.0f;
    const float rowW = panelW - 60.0f;
    float       rowY = panelY + 126.0f;
    for (int i = 0; i < kSurvivorsSkillCount; ++i) {
        draw->AddRectFilled(c.P(rowX, rowY), c.P(rowX + rowW, rowY + 44.0f), kPaperDeep, c.V(4.0f));
        draw->AddRect(c.P(rowX, rowY), c.P(rowX + rowW, rowY + 44.0f), kCardBorder, c.V(4.0f),
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
    const bool  portrait = c.h > c.w * 1.15f;

    const float panelW = FitPanelWidth(c, portrait ? 760.0f : 900.0f);
    const float panelH = portrait ? 1160.0f : 720.0f;
    const float panelX = c.w * 0.5f - panelW * 0.5f;
    const float panelY = c.h * 0.5f - panelH * 0.5f;
    DrawPanel(draw, m_ui, c, panelX, panelY, panelW, panelH);
    const bool tutorialDone = TutorialCompleted();
    if (tutorialDone) m_startFloor = Clamp(m_startFloor, 1, MaxUnlockedFloor());
    DrawSectionTitle(draw, m_ui, c, panelX + 30.0f, panelY + 26.0f, panelW - 60.0f, "出战准备", "初入江湖",
                     tutorialDone ? "选择职业与起始层；通关 20 层" : "先选职业，再进入第 1 层教学关；通关 20 层",
                     true);

    auto selectClass = [&](int cls) {
        if (m_playerClass == cls) return;
        m_playerClass = cls;
        if (GameObject* player = SurvivorsFindLocalPlayer(world)) {
            if (auto* component = player->GetComponent<SurvivorsPlayerComponent>()) component->playerClass = cls;
            SurvivorsApplyPlayerVisual(*player, cls);
        }
        PlaySfx("click", 0.55f);
    };

    auto drawCard = [&](int cls, float x, float y, float w, float h, bool rowLayout) {
        const bool selected = (m_playerClass == cls);
        m_prepareClassHit[cls] = Vec4(c.P(x, y).x, c.P(x, y).y, c.V(w), c.V(h));
        ImGui::SetCursorScreenPos(c.P(x, y));
        const bool pressed = ImGui::InvisibleButton((std::string("##class") + std::to_string(cls)).c_str(),
                                                     c.S(w, h));
        const bool hovered = ImGui::IsItemHovered();
        if (pressed) selectClass(cls);

        DrawCard(draw, m_ui, c, x, y, w, h, selected);
        if (selected) {
            draw->AddRect(c.P(x, y), c.P(x + w, y + h), kGoldBright, c.V(6.0f), ImDrawFlags_None, c.V(2.5f));
        }

        const Texture* heroTexture = m_ui.hero[Clamp(cls, 0, kSurvivorsClassCount - 1)];
        const float    heroH = rowLayout ? 150.0f : 178.0f;
        const float    heroW = heroTexture && heroTexture->Height() > 0
                                   ? heroH * static_cast<float>(heroTexture->Width()) / static_cast<float>(heroTexture->Height())
                                   : heroH;
        if (rowLayout) {
            if (heroTexture) {
                DrawImage(draw, c, m_ui.hero[cls], x + 18.0f, y + (h - heroH) * 0.5f, heroW, heroH);
            }
            const float textX = x + 18.0f + 112.0f;
            const float textW = w - (textX - x) - 18.0f;
            DrawText(draw, m_ui, c, 28.0f, textX, y + 14.0f, kText, kSurvivorsClasses[cls].name);
            DrawText(draw, m_ui, c, 17.0f, textX, y + 54.0f, kTag, kSurvivorsClasses[cls].desc);
            DrawWrapped(draw, m_ui, c, 15.5f, textX, y + 84.0f, textW, kTextDim,
                        kSurvivorsClasses[cls].style, 1.38f);
            DrawWrapped(draw, m_ui, c, 15.0f, textX, y + 142.0f, textW, kMint,
                        kSurvivorsClasses[cls].growth, 1.38f);
            if (selected) {
                DrawTextRight(draw, m_ui, c, 13.0f, x + w - 16.0f, y + 16.0f, kGold, "已选择");
            } else if (hovered) {
                DrawTextRight(draw, m_ui, c, 13.0f, x + w - 16.0f, y + 16.0f, kTextMuted, "点击选择");
            }
        } else {
            if (heroTexture) {
                DrawImage(draw, c, m_ui.hero[cls], x + (w - heroW) * 0.5f, y + 12.0f, heroW, heroH);
            }
            DrawTextCentered(draw, m_ui, c, 23.0f, x + w * 0.5f, y + 200.0f, kText,
                             kSurvivorsClasses[cls].name);
            DrawTextCentered(draw, m_ui, c, 14.0f, x + w * 0.5f, y + 234.0f, kTag,
                             kSurvivorsClasses[cls].desc);
            DrawWrapped(draw, m_ui, c, 13.0f, x + 16.0f, y + 262.0f, w - 32.0f, kTextDim,
                        kSurvivorsClasses[cls].style, 1.40f);
            DrawWrapped(draw, m_ui, c, 12.0f, x + 16.0f, y + 314.0f, w - 32.0f, kMint,
                        kSurvivorsClasses[cls].growth, 1.40f);
            if (selected) {
                DrawTextRight(draw, m_ui, c, 13.0f, x + w - 14.0f, y + 12.0f, kGold, "已选择");
            } else if (hovered) {
                DrawTextRight(draw, m_ui, c, 13.0f, x + w - 14.0f, y + 12.0f, kTextMuted, "点击选择");
            }
        }
    };

    if (portrait) {
        const float rowX = panelX + 24.0f;
        const float rowW = panelW - 48.0f;
        const float rowH = 236.0f;
        const float rowGap = 12.0f;
        float       rowY = panelY + 146.0f;
        for (int cls = 0; cls < kSurvivorsClassCount; ++cls) {
            drawCard(cls, rowX, rowY, rowW, rowH, true);
            rowY += rowH + rowGap;
        }
    } else {
        const float gap = 18.0f;
        const float cardW = (panelW - 60.0f - gap * 2.0f) / 3.0f;
        const float cardH = 352.0f;
        const float cardY = panelY + 146.0f;
        for (int cls = 0; cls < kSurvivorsClassCount; ++cls) {
            drawCard(cls, panelX + 30.0f + (cardW + gap) * static_cast<float>(cls), cardY, cardW, cardH, false);
        }
    }

    const float infoY = portrait ? panelY + 900.0f : panelY + 506.0f;
    if (tutorialDone) {
        const int maxUnlocked = MaxUnlockedFloor();
        const int cleared = MaxFloorCleared();
        if (portrait) {
            DrawText(draw, m_ui, c, 20.0f, panelX + 30.0f, infoY, kText, "历史最高");
            char clearedText[64];
            std::snprintf(clearedText, sizeof(clearedText), "第 %d 层 / %d", cleared, kSurvivorsFloorCount);
            DrawTextRight(draw, m_ui, c, 20.0f, panelX + panelW - 30.0f, infoY, kTag, clearedText);
            char floorText[48];
            std::snprintf(floorText, sizeof(floorText), "起始层：第 %d 层", m_startFloor);
            DrawTextCentered(draw, m_ui, c, 22.0f, c.w * 0.5f, infoY + 40.0f, kInkGreenDark, floorText);
            m_prepareFloorPrevHit = Vec4(c.P(panelX + 30.0f, infoY + 34.0f).x,
                                         c.P(panelX + 30.0f, infoY + 34.0f).y, c.V(92.0f), c.V(38.0f));
            m_prepareFloorNextHit = Vec4(c.P(panelX + panelW - 122.0f, infoY + 34.0f).x,
                                         c.P(panelX + panelW - 122.0f, infoY + 34.0f).y, c.V(92.0f), c.V(38.0f));
            if (DrawButton(draw, m_ui, c, "##floor_prev", "上一", panelX + 30.0f, infoY + 34.0f, 92.0f, 38.0f,
                           false, m_startFloor <= 1, 16.0f)) {
                SetStartFloor(m_startFloor - 1);
                PlaySfx("click", 0.55f);
            }
            if (DrawButton(draw, m_ui, c, "##floor_next", "下一", panelX + panelW - 122.0f, infoY + 34.0f, 92.0f,
                           38.0f, false, m_startFloor >= maxUnlocked, 16.0f)) {
                SetStartFloor(m_startFloor + 1);
                PlaySfx("click", 0.55f);
            }
            if (m_playerClass < 0) {
                DrawTextCentered(draw, m_ui, c, 18.0f, c.w * 0.5f, infoY + 92.0f, kGold,
                                 "请选择一个职业后开始");
            }
        } else {
            DrawText(draw, m_ui, c, 17.0f, panelX + 30.0f, infoY, kText, "历史最高");
            char clearedText[64];
            std::snprintf(clearedText, sizeof(clearedText), "第 %d 层 / %d", cleared, kSurvivorsFloorCount);
            DrawTextRight(draw, m_ui, c, 17.0f, panelX + panelW - 30.0f, infoY, kTag, clearedText);
            char floorText[48];
            std::snprintf(floorText, sizeof(floorText), "起始层：第 %d 层", m_startFloor);
            DrawTextCentered(draw, m_ui, c, 20.0f, c.w * 0.5f, infoY + 34.0f, kInkGreenDark, floorText);
            m_prepareFloorPrevHit = Vec4(c.P(panelX + 30.0f, infoY + 30.0f).x,
                                         c.P(panelX + 30.0f, infoY + 30.0f).y, c.V(92.0f), c.V(34.0f));
            m_prepareFloorNextHit = Vec4(c.P(panelX + panelW - 122.0f, infoY + 30.0f).x,
                                         c.P(panelX + panelW - 122.0f, infoY + 30.0f).y, c.V(92.0f), c.V(34.0f));
            if (DrawButton(draw, m_ui, c, "##floor_prev", "上一", panelX + 30.0f, infoY + 30.0f, 92.0f, 34.0f,
                           false, m_startFloor <= 1, 15.0f)) {
                SetStartFloor(m_startFloor - 1);
                PlaySfx("click", 0.55f);
            }
            if (DrawButton(draw, m_ui, c, "##floor_next", "下一", panelX + panelW - 122.0f, infoY + 30.0f, 92.0f,
                           34.0f, false, m_startFloor >= maxUnlocked, 15.0f)) {
                SetStartFloor(m_startFloor + 1);
                PlaySfx("click", 0.55f);
            }
            if (m_playerClass < 0) {
                DrawTextCentered(draw, m_ui, c, 15.0f, c.w * 0.5f, infoY + 76.0f, kGold,
                                 "请选择一个职业后开始");
            }
        }
    } else if (portrait) {
        DrawText(draw, m_ui, c, 20.0f, panelX + 30.0f, infoY, kText, "第 1 层 · 教学关");
        DrawWrapped(draw, m_ui, c, 16.0f, panelX + 30.0f, infoY + 34.0f, panelW - 60.0f, kTextDim,
                    "首次进入先学移动、拾取与武学成长；完成后可从已通关层开始。", 1.38f);
        if (m_playerClass < 0) {
            DrawTextCentered(draw, m_ui, c, 18.0f, c.w * 0.5f, infoY + 90.0f, kGold,
                             "请选择一个职业后开始教学");
        }
    } else {
        DrawText(draw, m_ui, c, 17.0f, panelX + 30.0f, infoY, kText, "第 1 层 · 教学关");
        DrawWrapped(draw, m_ui, c, 13.0f, panelX + 190.0f, infoY + 2.0f, panelW - 220.0f, kTextDim,
                    "首次进入先学移动、拾取与武学成长；完成后可从已通关层开始。", 1.40f);
        if (m_playerClass < 0) {
            DrawTextCentered(draw, m_ui, c, 15.0f, c.w * 0.5f, infoY + 44.0f, kGold,
                             "请选择一个职业后开始教学");
        }
    }

    const float backW = 220.0f;
    const float startW = 240.0f;
    const float gap = 16.0f;
    const float total = backW + gap + startW;
    const float buttonY = panelY + panelH - 82.0f;
    const float startX = c.w * 0.5f - total * 0.5f;
    m_prepareBackHit = Vec4(c.P(startX, buttonY).x, c.P(startX, buttonY).y, c.V(backW), c.V(48.0f));
    m_prepareStartHit = Vec4(c.P(startX + backW + gap, buttonY).x, c.P(startX + backW + gap, buttonY).y,
                             c.V(startW), c.V(48.0f));
    if (DrawButton(draw, m_ui, c, "##prepare_back", "返回", startX, buttonY, backW, 48.0f, false, false, 19.0f)) {
        PlaySfx("click", 0.6f);
        m_state = State::Menu;
    }
    char startLabel[32];
    if (tutorialDone) std::snprintf(startLabel, sizeof(startLabel), "进入第 %d 层", m_startFloor);
    else std::snprintf(startLabel, sizeof(startLabel), "开始教学");
    const bool startDisabled = m_playerClass < 0;
    bool       startPressed = DrawButton(draw, m_ui, c, "##prepare_start", startLabel, startX + backW + gap,
                                         buttonY, startW, 48.0f, true, startDisabled, 19.0f);
    // 兜底命中：少数情况下 ImGui 窗口裁剪/活动项会让底部按钮收不到点击，直接按屏幕矩形再判一次。
    if (!startPressed && !startDisabled && Input::IsMousePressed(0)) {
        const Vec2   mouse = Input::MousePosition();
        const ImVec2 min = c.P(startX + backW + gap, buttonY);
        const ImVec2 max = c.P(startX + backW + gap + startW, buttonY + 48.0f);
        startPressed = mouse.x >= min.x && mouse.x <= max.x && mouse.y >= min.y && mouse.y <= max.y;
    }
    if (startPressed) {
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
    const ImVec2 measured = MeasureText(m_ui, c, 15.0f, text.c_str());
    const float  textW = measured.x / c.scale;
    const float  w = textW + 40.0f;
    const float  h = 38.0f;
    const float  x = c.w * 0.5f - w * 0.5f;
    float        y = 150.0f; // 桌面：让开顶部的层数/通关条件条
    if (c.textScale > 1.0f) {
        // 手机 HUD 顶栏更高：Toast 放到层数条下方，避开雷达与暂停键。
        const float px = 1.0f / c.scale;
        const float top = ((std::max)(m_safeInsets.y, c.h > c.w ? 60.0f : 0.0f) + 14.0f) * px;
        y = top + 104.0f * px;
    }
    draw->AddRectFilled(c.P(x, y), c.P(x + w, y + h), IM_COL32(6, 8, 12, static_cast<int>(214 * alpha)),
                        c.V(5.0f));
    draw->AddRect(c.P(x, y), c.P(x + w, y + h), IM_COL32(240, 205, 120, static_cast<int>(170 * alpha)),
                  c.V(5.0f), 1.0f);
    DrawTextCentered(draw, m_ui, c, 15.0f, c.w * 0.5f, y + (h - 15.0f) * 0.5f - 1.0f,
                     IM_COL32(255, 226, 150, static_cast<int>(255 * alpha)), text.c_str());
}

} // namespace erm
