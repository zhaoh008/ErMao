// ErMao Engine - 编辑器（Dear ImGui 版）
//
// 面板：视口 / 世界大纲 / 检视器 / 资源浏览器 / 控制台 / 统计 / 设置
// 交互：Dock 布局、拾取、Play-In-Editor（快照回滚）、资源导入、面板开关
//
// 设计原则（与用户要求一致）：UI 只提供"看得见、改得动"的基本能力，
// 真正的关卡/玩法仍然是代码优先（见 game/Levels.cpp）。
#pragma once

#include "core/Json.h"
#include "core/Math.h"
#include "core/Types.h"
#include "editor/ImGuiLayer.h"
#include "renderer/Camera.h"

#include <functional>

struct ImVec2;

namespace erm {

class World;
class Renderer;
class AssetManager;
class GameObject;
struct FieldDesc;

class Editor {
public:
    using ViewportRenderer = std::function<void(const Camera& camera, u32 width, u32 height)>;

    void Init(ImGuiLayer* imguiLayer);

    // 资源管理器「场景」里的内置关卡（level://xxx）交给 Engine 重建
    using LevelLoader = std::function<void(const std::string&)>;
    void SetLevelLoader(LevelLoader loader) { m_levelLoader = std::move(loader); }
    // 载入普通场景后，把场景里的 levelKind 通知 Engine 恢复 2D/3D 模式
    using SceneLoadedCallback = std::function<void(const std::string&)>;
    void SetSceneLoadedCallback(SceneLoadedCallback callback) { m_sceneLoaded = std::move(callback); }
    void LoadSceneAsset(World& world, const std::string& name, const std::string& sourcePath);
    void SetScenePath(const std::string& path) { m_scenePath = path; }
    void ShowSettings(bool show) {
        m_showSettings = show;
        if (show) m_focusSettings = true;
    }
    void Shutdown();
    // 忽略 imgui.ini，本次启动强制铺一次默认 Dock 布局
    void ForceDefaultLayout() { m_forceDefaultLayout = true; }
    void Draw(World& world, Renderer& renderer, AssetManager& assets, Camera& editorCamera,
              const ViewportRenderer& renderViewport, float dt);

    bool IsPlaying() const { return m_playing; }
    bool IsPaused() const { return m_paused; }
    bool ViewportHovered() const { return m_viewportHovered; }
    float ViewportMinX() const { return m_viewportMinX; }
    float ViewportMinY() const { return m_viewportMinY; }
    u32   ViewportWidthPx() const { return m_viewportWidth; }
    u32   ViewportHeightPx() const { return m_viewportHeight; }
    bool WantsExit() const { return m_wantsExit; }
    void ClearExitRequest() { m_wantsExit = false; }

    // ---- 编辑器视角：透视 / 正视图 / 俯视图 ----
    enum class ViewPreset { Perspective, Front, Top };
    void      SetViewPreset(Camera& editorCamera, ViewPreset preset);
    ViewPreset CurrentView() const { return m_viewPreset; }
    // 2D 关卡固定为正交正视图（编辑 2D 精灵时 gizmo / 拾取与画面严格对齐）
    void Enter2DEditorView(Camera& editorCamera);
    void Exit2DEditorView(Camera& editorCamera);
    bool Is2DEditorView() const { return m_is2DLevel; }

    GameObject* Selected() const { return m_selected; }
    void        Select(GameObject* object);

    void Play();
    void EnterPlayMode(World& world);
    void ExitPlayMode(World& world);
    void Pause();
    void Stop();

    void SetStatus(const std::string& text, bool error = false);

private:
    void DrawMenuBar(World& world, Renderer& renderer, AssetManager& assets, Camera& editorCamera);
    void DrawToolbar(World& world, Renderer& renderer, Camera& editorCamera);
    void DrawDockspace(World& world, Renderer& renderer, AssetManager& assets, Camera& editorCamera);
    void DrawViewport(World& world, Renderer& renderer, Camera& editorCamera, const ViewportRenderer& renderViewport);
    void DrawOutliner(World& world);
    void DrawInspector(World& world, AssetManager& assets);
    void DrawAssets(World& world, AssetManager& assets, Renderer& renderer, Camera& editorCamera);
    void DrawConsole();
    void DrawStats(World& world, Renderer& renderer);
    void DrawSettings(World& world, Renderer& renderer, Camera& editorCamera);
    void DrawStatusBar();

    void DrawOutlinerNode(World& world, GameObject* object);
    void DrawTransform(GameObject* object);
    void DrawComponents(World& world, GameObject* object, AssetManager& assets);
    bool DrawField(const FieldDesc& field, void* object, AssetManager& assets);

    void HandleShortcuts(World& world, Camera& editorCamera, Renderer& renderer);
    void DrawGizmo(World& world, Camera& editorCamera, const ImVec2& viewportMin, const ImVec2& viewportSize);
    void FocusSelected(World& world, Camera& editorCamera);
    void PickObject(World& world, Camera& editorCamera, const ImVec2& viewportPos, const ImVec2& viewportSize);
    void ImportModel(World& world, AssetManager& assets, Camera& editorCamera);
    void ImportTexture(AssetManager& assets);
    void SaveScene(World& world, bool saveAs);
    void LoadScene(World& world);
    void NewScene(World& world);
    void SpawnMeshObject(World& world, const std::string& meshName);
    void DeleteSelected(World& world);
    void DuplicateSelected(World& world);

    ImGuiLayer* m_imgui = nullptr;

    GameObject* m_selected = nullptr;
    u64         m_selectedId = 0;

    bool m_playing = false;
    bool m_paused = false;
    bool m_wantsExit = false;
    bool m_viewportHovered = false;
    bool m_layoutInitialized = false;
    bool m_forceDefaultLayout = false;
    bool m_showDemo = false;

    // 面板开关
    bool m_showViewport = true;
    bool m_showOutliner = true;
    bool m_showInspector = true;
    bool m_showAssets = true;
    bool m_showConsole = true;
    bool m_showStats = true;
    bool m_showSettings = false;
    bool m_focusSettings = false;

    std::string m_scenePath = "assets/scenes/survivors.ermao.json";
    std::string m_snapshot;
    std::string m_statusMessage = "就绪";
    bool        m_statusIsError = false;
    double      m_statusTime = 0.0;

    // 资源浏览器状态
    std::string m_assetFilter;
    char        m_assetFilterBuffer[128] = {};
    int         m_selectedAsset = -1;
    int         m_assetCategory = 0; // 0=全部 1=模型 2=贴图 3=场景

    // 控制台状态
    bool m_consoleInfo = true;
    bool m_consoleWarn = true;
    bool m_consoleError = true;
    bool m_consoleAutoScroll = true;
    char m_consoleFilter[128] = {};

    // 统计
    float m_frameTimes[120] = {};
    int   m_frameTimeOffset = 0;
    float m_benchSerial = 0.0f;
    float m_benchParallel = 0.0f;

    // 位移 gizmo
    // 0=位移 1=旋转 2=缩放（W/E/R 切换）
    int    m_gizmoMode = 0;
    LevelLoader          m_levelLoader;
    SceneLoadedCallback  m_sceneLoaded;
    bool   m_gizmoEnabled = true;
    int    m_gizmoAxis = -1;      // 0/1/2 = X/Y/Z，-1 未拖拽
    float  m_gizmoStartMouseX = 0.0f;
    float  m_gizmoStartMouseY = 0.0f;
    Vec3   m_gizmoStartPosition{0, 0, 0};
    float  m_gizmoAxisScreenX = 0.0f;
    float  m_gizmoAxisScreenY = 0.0f;
    float  m_gizmoWorldPerPixel = 0.01f;
    Vec3   m_gizmoStartScale{1.0f, 1.0f, 1.0f};
    Quat   m_gizmoStartRotation{0, 0, 0, 1};
    // 旋转拖动：沿环的屏幕切线做增量，避免“左右晃一下同时绕两轴”的错觉
    float  m_rotateGrabTheta = 0.0f;
    float  m_rotateAccum = 0.0f;
    float  m_rotateScreenRadius = 1.0f;
    float  m_rotateLastMouseX = 0.0f;
    float  m_rotateLastMouseY = 0.0f;

    // 视角状态
    ViewPreset m_viewPreset = ViewPreset::Perspective;
    bool       m_is2DLevel = false;
    bool       m_savedPerspectiveValid = false;
    Camera     m_savedPerspective;

    // 视口信息
    u32   m_viewportWidth = 0;
    u32   m_viewportHeight = 0;
    float m_viewportMinX = 0.0f;
    float m_viewportMinY = 0.0f;
};

} // namespace erm
