// ErMao Engine - 引擎主程序（编辑器 / Play 模式 / 离屏视口）
#pragma once

#include "asset/AssetManager.h"
#include "core/Types.h"
#include "editor/EditorUI.h"
#include "editor/ImGuiLayer.h"
#include "game/WaveGame.h"
#include "platform/Window.h"
#include "renderer/Camera.h"
#include "renderer/RenderScene.h"
#include "renderer/Renderer.h"
#include "rhi/Device.h"
#include "scene/World.h"

namespace erm {

class Engine {
public:
    int  Run(int argc, char** argv);

private:
    bool Init();
    void Shutdown();
    void MainLoop();
    void UpdateGameplay(float dt);
    void RenderViewportScene(const Camera& camera, u32 width, u32 height);
    void CaptureScreenshot(const std::string& path);

    Window       m_window;
    Device       m_device;
    Renderer     m_renderer;
    AssetManager m_assets;
    World        m_world;
    ImGuiLayer   m_imgui;
    Editor       m_editor;

    Camera      m_editorCamera;
    RenderScene m_scene; // 复用，避免每帧堆分配（P2-10）
    WaveGame    m_waveGame; // 刷怪 Demo（arena 关卡时才启用）
    bool        m_waveLevel = false;
    bool        m_demoMode = false;

    bool  m_running = true;
    bool  m_vsync = true;
    bool  m_showEditor = true;
    bool  m_startPlay = false;
    float m_accumulator = 0.0f;
    float m_time = 0.0f;
    float m_fpsTimer = 0.0f;
    int   m_fpsFrames = 0;

    // 自动化
    std::string m_screenshotPath;
    int         m_autoFrames = 0;
    int         m_frameIndex = 0;
    bool        m_testSerialize = false;
    bool        m_smoke = false;
    std::string m_importPath;
    std::string m_packageTarget; // windows/linux
    bool        m_noShadows = false;
    int         m_debugView = -1;       // -1 = 不改；0/1/2/3 = 调试视图
    float       m_overrideRoughness = -1.0f;
    float       m_overrideMetallic = -1.0f;
    std::string m_levelName = "arena";
    std::string m_scenePath = "assets/scenes/thirdperson.ermao.json";
};

} // namespace erm
