#pragma once

#include "asset/AssetManager.h"
#include "core/Math.h"
#include "renderer/Camera.h"
#include "renderer/RenderScene.h"
#include "scene/World.h"
#include "WebRenderer.h"

struct SDL_Window;
union SDL_Event;
struct ImFont;

namespace erm {

class SurvivorsDirector;

// 浏览器/微信小游戏端的 2D 幸存者宿主。
// 复用 engine/scene、asset、game 层的 C++ 逻辑；只替换平台层与渲染后端。
class WebApp {
public:
    WebApp();
    ~WebApp();

    bool Init();
    void Frame();
    void Shutdown();

private:
    void ProcessEvent(SDL_Event& event);
    void Update(float dt);
    void Render();
    void ApplyStyle();
    bool LoadFonts();
    void UpdateTouchInput();
    void ReleaseTouchKeys();
    void Quit();

    SDL_Window* m_window = nullptr;
    void*       m_glContext = nullptr;

    AssetManager      m_assets;
    World             m_world;
    SurvivorsDirector* m_survivors = nullptr; // 头文件保持轻量，实现里创建
    WebRenderer       m_renderer;
    RenderScene       m_scene;
    Camera            m_camera;

    ImFont* m_bodyFont = nullptr;
    ImFont* m_largeFont = nullptr;
    ImFont* m_displayFont = nullptr;

    bool  m_running = true;
    bool  m_mobile = false;
    bool  m_initialised = false;
    float m_time = 0.0f;
    float m_accumulator = 0.0f;
    int   m_prewarmFrames = 0;
    int   m_mouseX = 0;
    int   m_mouseY = 0;
    int   m_mouseDeltaX = 0;
    int   m_mouseDeltaY = 0;
    float m_wheel = 0.0f;

    // 触摸摇杆：手指按下后的起点/当前位置（窗口坐标）；摇杆本身不绘制。
    bool  m_touchActive = false;
    int   m_touchFinger = -1;
    Vec2  m_touchOrigin{};
    Vec2  m_touchPosition{};
    int   m_touchKeys = 0;

    // 系统安全区内边距（左/上/右/下，CSS 像素），供手机 HUD 布局使用。
    Vec4 m_safeInsets{0.0f, 0.0f, 0.0f, 0.0f};
};

} // namespace erm
