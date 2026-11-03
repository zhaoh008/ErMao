// ErMao Engine - 引擎主程序（编辑器 / Play 模式 / 离屏视口）
#pragma once

#include "asset/AssetManager.h"
#include "core/Json.h"
#include "core/Types.h"
#include "editor/EditorUI.h"
#include "editor/ImGuiLayer.h"
#include "platform/Audio.h"
#include "game/Survivors.h"
#include "game/Survivors3D.h"
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

    // 切换内置关卡（编辑器「关卡」菜单 / 资源管理器「场景」双击）
    void LoadLevel(const std::string& kind);
    // 只恢复 2D/3D/玩法模式，不重建世界（读档后调用）
    void ApplyLevelMode(const std::string& kind);
    // 切换关卡后同步编辑器相机：2D 关卡固定正交正视图，3D 关卡恢复透视
    void SetupEditorViewForLevel();

    // ---- AI 优先：无头脚本接口 ----
    // 输入 {"commands":[{cmd:...}, ...]}，返回逐条结果（JSON）
    JsonValue ExecuteAgentCommand(const JsonValue& command);
    bool      RunAgentScript(const std::string& path, const std::string& outputPath);
    bool      RenderAgentScreenshot(const std::string& path);
    JsonValue ValidateWorld();
    void RunUndoSelfTest();
    void UpdateGameplay(float dt);
    void RenderViewportScene(const Camera& camera, u32 width, u32 height);
    bool CaptureScreenshot(const std::string& path);

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
    SurvivorsDirector m_survivors; // 2D 幸存者（survivors 关卡）
    bool        m_survivorsLevel = false;
    Survivors3DDirector m_survivors3d; // 3D 幸存者（survivors3d 关卡）
    bool        m_survivors3dLevel = false;
    bool        m_is2D = false;
    bool        m_waveLevel = false;
    bool        m_demoMode = false;
    bool        m_playSessionStarted = false; // 本次 Play 是否已自动开局（Run=直接开打）

    bool  m_running = true;
    bool  m_vsync = true;
    bool  m_showEditor = true;
    bool  m_startPlay = false;
    float m_accumulator = 0.0f;
    float m_time = 0.0f;
    float m_fpsTimer = 0.0f;
    int   m_fpsFrames = 0;

    // 发布版 / 开发版：--dev 打开控制台日志，--log 只写日志文件，--nolog 两者都关
    bool m_devMode = false;
    bool m_devConsole = false;
    bool m_logFile = false;
    bool m_logExplicit = false;

    // 自动化
    std::string m_screenshotPath;
    int         m_autoFrames = 0;
    int         m_frameIndex = 0;
    bool        m_testSerialize = false;
    bool        m_testUndo = false;
    bool        m_smoke = false;
    std::string m_importPath;
    std::string m_packageTarget; // windows/linux
    std::string m_packageGame;   // windows/linux（游戏包）
    bool        m_audioTest = false;
    bool        m_vulkanTest = false;
    std::string m_scriptPath;
    std::string m_jsonOut;
    bool        m_validateOnly = false;
    bool        m_headlessFailed = false; // 无头任务（脚本/校验/打包）失败 → 非 0 退出码
    bool        m_noShadows = false;
    bool        m_noGpuCull = false;
    int         m_debugView = -1;       // -1 = 不改；0/1/2/3 = 调试视图
    float       m_overrideRoughness = -1.0f;
    float       m_overrideMetallic = -1.0f;
    std::string m_levelName = "survivors";
    std::string m_scenePath = "assets/scenes/survivors.ermao.json";
    std::string m_editorView;        // --editor-view perspective|front|top（自动截图用）
    std::string m_editorSelect;      // --editor-select <名字>（自动截图用）
    bool        m_editorSettings = false; // --editor-settings（自动截图用）
    std::string m_uiPreview;              // --ui-preview <page>（2D UI 截图/走查）
    bool        m_uiPreviewApplied = false;
    int         m_windowWidth = 1600;
    int         m_windowHeight = 900;
};

} // namespace erm
