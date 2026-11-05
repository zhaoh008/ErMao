// ErMao Engine - 2D 幸存者 UI 资源与设置快照
//
// 这里只声明资源句柄，不把 Dear ImGui 的头文件带进游戏层头文件；
// 具体绘制在 SurvivorsUI.cpp 中完成；技能特效贴图也在这里统一加载。
#pragma once

struct ImFont;

namespace erm {

class AssetManager;
class Texture;

// 设置面板与持久化之间传递的纯数据快照
struct SurvivorsSettingsState {
    bool  musicEnabled = true;
    bool  sfxEnabled = true;
    float masterVolume = 0.8f;
    float musicVolume = 0.45f;
    float sfxVolume = 0.9f;
};

// 2D 幸存者界面使用的切图与字体
struct SurvivorsUIAssets {
    bool Load(AssetManager& assets, ImFont* body, ImFont* large, ImFont* display);

    bool Ready() const { return panel != nullptr && slot != nullptr && body != nullptr; }

    // 水墨国风 UI 切图（engine/assets/inkwash，素材来源见 LICENSE-INKWASH.txt）
    Texture* panel = nullptr;
    Texture* slot = nullptr;
    Texture* card = nullptr;
    Texture* buttonNormal = nullptr;
    Texture* buttonHover = nullptr;
    Texture* buttonPressed = nullptr;
    Texture* buttonDisabled = nullptr;
    Texture* buttonPrimary = nullptr;
    Texture* barTrack = nullptr;
    Texture* barHealthFill = nullptr;
    Texture* barXpFill = nullptr;
    Texture* crest = nullptr;
    Texture* iconHealth = nullptr;
    Texture* iconXp = nullptr;
    Texture* icon[10]{}; // 箭矢 / 雷霆 / 回旋刃 / 霜环 / 毒雾 / 火球 / 陨石 / 黑洞 / 棱镜 / 毒爆
    Texture* titleBackground = nullptr;
    Texture* floor = nullptr;
    Texture* chestClosed = nullptr;
    Texture* chestOpen = nullptr;
    Texture* hero[3]{};     // 游侠 / 剑士 / 法师立绘
    Texture* propTree = nullptr;
    Texture* propCenser = nullptr;
    Texture* propRock = nullptr;
    Texture* propLantern = nullptr;
    Texture* propJar = nullptr;
    Texture* propUrn = nullptr;

    // 水墨技能特效贴图（序列帧网格在 Survivors.cpp 绘制时指定）
    Texture* fxBlade = nullptr;           // 月牙剑气 2×2
    Texture* fxSword = nullptr;           // 飞剑
    Texture* fxLightning = nullptr;       // 闪电 1×4
    Texture* fxLightningRing = nullptr;   // 雷环
    Texture* fxFrost = nullptr;           // 雪花 2×2
    Texture* fxFrostRing = nullptr;       // 墨环（染冰蓝）
    Texture* fxPoison = nullptr;          // 毒烟 4×4
    Texture* fxPoisonGround = nullptr;    // 毒地
    Texture* fxFireball = nullptr;        // 火球 4×4
    Texture* fxExplosion = nullptr;       // 爆炸 4×4
    Texture* fxMeteor = nullptr;          // 陨石 2×2
    Texture* fxSingularity = nullptr;     // 黑洞
    Texture* fxSingularityRing = nullptr; // 吸积环
    Texture* fxPrism = nullptr;           // 棱镜光束
    Texture* fxPlague = nullptr;          // 毒爆 2×2
    Texture* fxInk = nullptr;             // 墨点
    Texture* fxSlash = nullptr;           // 金刃 2×4

    ImFont* body = nullptr;
    ImFont* large = nullptr;
    ImFont* display = nullptr;
};

} // namespace erm
