// ErMao Engine - 2D 幸存者 UI 资源与设置快照
//
// 这里只声明资源句柄，不把 Dear ImGui 的头文件带进游戏层头文件；
// 具体绘制在 SurvivorsUI.cpp 中完成。
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

    Texture* panel = nullptr;
    Texture* slot = nullptr;
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
    Texture* icon[6]{}; // 箭矢 / 雷霆 / 回旋刃 / 霜环 / 毒雾 / 火球
    Texture* titleBackground = nullptr;
    Texture* floor = nullptr;

    ImFont* body = nullptr;
    ImFont* large = nullptr;
    ImFont* display = nullptr;
};

} // namespace erm
