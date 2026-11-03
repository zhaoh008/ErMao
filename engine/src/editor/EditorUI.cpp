// ErMao Engine - 编辑器实现（Dear ImGui）
#include "editor/EditorUI.h"
#include "ai/NavGrid.h"
#include "asset/AssetManager.h"
#include "core/FileSystem.h"
#include "core/Json.h"
#include "core/JobSystem.h"
#include "core/Log.h"
#include "editor/ImGuiLayer.h"
#include "game/ThirdPersonController.h"
#include "physics/PhysicsWorld.h"
#include "platform/FileDialog.h"
#include "runtime/Packager.h"
#include "platform/Input.h"
#include "renderer/Camera.h"
#include "renderer/Renderer.h"
#include "scene/Components.h"
#include "scene/World.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <imgui.h>
#include <imgui_internal.h>

namespace erm {

namespace {

ImVec4 ToImVec4(const Vec4& c) { return ImVec4(c.x, c.y, c.z, c.w); }
Vec4   ToVec4(const ImVec4& c) { return Vec4(c.x, c.y, c.z, c.w); }

const char* kLevelReloadLabel = "重新加载演示关卡";

ImGuiID g_dockspaceId = 0;

void BuildDefaultLayout(ImGuiID dockspaceId) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

    ImGuiID center = dockspaceId;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, nullptr, &center);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.26f, nullptr, &center);
    ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.45f, nullptr, &left);
    ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.45f, nullptr, &right);

    ImGui::DockBuilderDockWindow("视口", center);
    ImGui::DockBuilderDockWindow("世界大纲", left);
    ImGui::DockBuilderDockWindow("资源浏览器", leftBottom);
    ImGui::DockBuilderDockWindow("检视器", right);
    ImGui::DockBuilderDockWindow("统计", rightBottom);
    ImGui::DockBuilderDockWindow("设置", rightBottom);
    ImGui::DockBuilderDockWindow("控制台", bottom);
    ImGui::DockBuilderFinish(dockspaceId);
}

} // namespace

void Editor::Init(ImGuiLayer* imguiLayer) {
    m_imgui = imguiLayer;
    // 路径由 Engine 按当前关卡设置（m_scenePath），这里不再写死。
    LogInfo("编辑器初始化（Dear ImGui）");
}

void Editor::Shutdown() {
    m_selected = nullptr;
    m_selectedId = 0;
    m_snapshot.clear();
    m_layoutInitialized = false;
}

void Editor::SetStatus(const std::string& text, bool error) {
    m_statusMessage = text;
    m_statusIsError = error;
    m_statusTime = ImGui::GetTime();
    if (error) {
        LogError("%s", text.c_str());
    } else {
        LogInfo("%s", text.c_str());
    }
}

void Editor::Select(GameObject* object) {
    m_selected = object;
    m_selectedId = object ? object->Id() : 0;
}

namespace {
const char* ViewPresetName(Editor::ViewPreset preset) {
    switch (preset) {
        case Editor::ViewPreset::Front: return "正视图";
        case Editor::ViewPreset::Top: return "俯视图";
        default: return "透视";
    }
}
} // namespace

void Editor::SetViewPreset(Camera& editorCamera, ViewPreset preset) {
    if (m_playing) return;
    if (m_is2DLevel && preset != ViewPreset::Front) {
        SetStatus("2D 关卡固定为正视图（透视/俯视对精灵关卡不适用）", true);
        return;
    }
    if (preset == m_viewPreset) return;

    // 离开透视前保存自由视角，切回来时恢复，不丢用户刚才的观察位置。
    if (preset == ViewPreset::Perspective) {
        m_viewPreset = ViewPreset::Perspective;
        if (m_savedPerspectiveValid) editorCamera = m_savedPerspective;
        editorCamera.orthographic = false;
        editorCamera.up = Vec3(0, 1, 0);
        SetStatus("视角: 透视");
        return;
    }
    if (m_viewPreset == ViewPreset::Perspective) {
        m_savedPerspective = editorCamera;
        m_savedPerspectiveValid = true;
    }

    // 选中物体时围绕它切换；否则围绕视线前方最近的落点，避免视角跳得太远。
    Vec3 focus = m_selected ? m_selected->WorldPosition() : Vec3(0, 0, 0);
    if (!m_selected) {
        const Vec3  forward = editorCamera.Forward();
        float       distanceToGround = 10.0f;
        if (forward.y < -1e-4f) {
            distanceToGround = Clamp((0.0f - editorCamera.position.y) / forward.y, 2.0f, 60.0f);
        }
        focus = editorCamera.position + forward * distanceToGround;
        if (m_is2DLevel) focus = Vec3(0, 0, 0);
    }
    const float distance = Clamp(Length(editorCamera.position - focus), 10.0f, 80.0f);
    const float height = editorCamera.orthographic
                             ? editorCamera.orthoHeight
                             : Clamp(distance * 0.9f, 4.0f, 80.0f);
    m_viewPreset = preset;
    editorCamera.orthographic = true;
    editorCamera.orthoHeight = height;
    if (preset == ViewPreset::Front) {
        editorCamera.position = focus + Vec3(0, 0, distance);
        editorCamera.yaw = 0.0f;
        editorCamera.pitch = 0.0f;
        editorCamera.up = Vec3(0, 1, 0);
    } else {
        editorCamera.position = focus + Vec3(0, distance, 0);
        editorCamera.yaw = 0.0f;
        editorCamera.pitch = -kHalfPi;
        editorCamera.up = Vec3(0, 0, -1);
    }
    SetStatus(std::string("视角: ") + ViewPresetName(preset));
}

void Editor::Enter2DEditorView(Camera& editorCamera) {
    m_is2DLevel = true;
    m_viewPreset = ViewPreset::Front;
    m_savedPerspectiveValid = false;
    editorCamera.orthographic = true;
    editorCamera.up = Vec3(0, 1, 0);
    editorCamera.yaw = 0.0f;
    editorCamera.pitch = 0.0f;
    editorCamera.position = Vec3(0, 0, 20);
    editorCamera.orthoHeight = 13.0f;
}

void Editor::Exit2DEditorView(Camera& editorCamera) {
    if (!m_is2DLevel) return;
    m_is2DLevel = false;
    m_viewPreset = ViewPreset::Perspective;
    m_savedPerspectiveValid = false;
    editorCamera.orthographic = false;
    editorCamera.up = Vec3(0, 1, 0);
    editorCamera.position = Vec3(10, 7, 16);
    editorCamera.yaw = 0.55f;
    editorCamera.pitch = -0.32f;
    editorCamera.orthoHeight = 12.8f;
}

void Editor::Play() {
    m_playing = true;
    m_paused = false;
}

void Editor::EnterPlayMode(World& world) {
    m_snapshot = world.ToJson().Dump();
    Play();
    SetStatus("Play：已保存场景快照，Stop 时回滚");
}

void Editor::ExitPlayMode(World& world) {
    Stop();
    if (!m_snapshot.empty()) {
        std::string error;
        JsonValue root = JsonValue::Parse(m_snapshot, &error);
        if (error.empty()) world.FromJson(root);
    }
    Select(nullptr);
    SetStatus("已停止并回滚场景");
}

void Editor::Pause() {
    if (!m_playing) return;
    m_paused = !m_paused;
    SetStatus(m_paused ? "已暂停" : "继续运行");
}

void Editor::Stop() {
    m_playing = false;
    m_paused = false;
}

void Editor::Draw(World& world, Renderer& renderer, AssetManager& assets, Camera& editorCamera,
                  const ViewportRenderer& renderViewport, float dt) {
    // 先按 id 回查 World，再解引用；World 被关卡/场景替换后 m_selected
    // 可能已经悬垂，不能先调用 m_selected 的成员函数。
    if (m_selected) {
        GameObject* selected = world.FindById(m_selectedId);
        if (!selected || selected != m_selected || m_selected->IsPendingDestroy()) {
            m_selected = nullptr;
            m_selectedId = 0;
        }
    }

    DrawDockspace(world, renderer, assets, editorCamera);

    if (m_showViewport) DrawViewport(world, renderer, editorCamera, renderViewport);
    if (m_showOutliner) DrawOutliner(world);
    if (m_showInspector) DrawInspector(world, assets);
    if (m_showAssets) DrawAssets(world, assets, renderer, editorCamera);
    if (m_showConsole) DrawConsole();
    if (m_showStats) DrawStats(world, renderer);
    if (m_showSettings) DrawSettings(world, renderer, editorCamera);
    DrawStatusBar();

    HandleShortcuts(world, editorCamera);

    // 帧时间曲线
    m_frameTimes[m_frameTimeOffset] = dt * 1000.0f;
    m_frameTimeOffset = (m_frameTimeOffset + 1) % IM_ARRAYSIZE(m_frameTimes);
}

void Editor::DrawDockspace(World& world, Renderer& renderer, AssetManager& assets, Camera& editorCamera) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float statusBarHeight = 26.0f;
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - statusBarHeight));
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##DockHost", nullptr, flags);
    ImGui::PopStyleVar(3);

    if (ImGui::BeginMenuBar()) {
        DrawMenuBar(world, renderer, assets, editorCamera);
        ImGui::EndMenuBar();
    }

    // 工具栏：dock 区域上方的一条固定高度子窗口
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 5));
    ImGui::BeginChild("##toolbar", ImVec2(0, 40.0f), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    DrawToolbar(world, renderer, editorCamera);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    g_dockspaceId = ImGui::GetID("ErMaoDockspace");
    ImGui::DockSpace(g_dockspaceId, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);

    if (!m_layoutInitialized) {
        m_layoutInitialized = true;
        // 审核 P2-22：ini 现在在 exe 旁，而且 --frames/--screenshot 时要强制默认布局，
        // 否则残留的"视口折叠"会让自动截图得到空画面。
        const std::string iniPath = FileSystem::Join(FileSystem::ExecutableDir(), "imgui.ini");
        if (m_forceDefaultLayout || !FileSystem::Exists(iniPath)) {
            BuildDefaultLayout(g_dockspaceId);
        }
    }
    ImGui::End();
}

void Editor::DrawMenuBar(World& world, Renderer& renderer, AssetManager& assets, Camera& editorCamera) {

    if (ImGui::BeginMenu("文件")) {
        if (ImGui::MenuItem("新建场景", "Ctrl+N")) NewScene(world);
        if (ImGui::MenuItem("加载 2D 演示关卡")) {
            if (m_levelLoader) {
                m_levelLoader("survivors");
                Select(nullptr);
                SetStatus("已加载 2D 演示关卡");
            } else {
                SetStatus("关卡加载器未初始化", true);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("打开场景...", "Ctrl+O")) LoadScene(world);
        if (ImGui::MenuItem("保存场景", "Ctrl+S")) SaveScene(world, false);
        if (ImGui::MenuItem("另存为...")) SaveScene(world, true);
        ImGui::Separator();
        if (ImGui::MenuItem("导入模型 (OBJ)...")) ImportModel(world, assets, editorCamera);
        if (ImGui::MenuItem("导入贴图...")) ImportTexture(assets);
        ImGui::Separator();
        if (ImGui::BeginMenu("打包")) {
            if (ImGui::MenuItem("打包 Windows")) {
                const PackageResult result = Packager::PackageWindows();
                SetStatus(result.message, !result.success);
            }
            if (ImGui::MenuItem("打包 Linux（源码包）")) {
                const PackageResult result = Packager::PackageLinuxSource();
                SetStatus(result.message, !result.success);
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("退出", "Alt+F4")) m_wantsExit = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("关卡")) {
        struct LevelItem { const char* id; const char* label; };
        // 只保留两个 demo 关卡：2D 幸存者 / 3D 幸存者。
        const LevelItem levels[] = {
            {"survivors", "2D 幸存者 Demo"},
            {"survivors3d", "3D 幸存者 Demo"},
        };
        for (const LevelItem& level : levels) {
            if (ImGui::MenuItem(level.label)) {
                if (m_levelLoader) m_levelLoader(level.id);
                SetStatus(std::string("已切换关卡: ") + level.label);
            }
        }
        ImGui::Separator();
        ImGui::TextDisabled("也可以在「资源管理器 → 场景」双击关卡");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("编辑")) {
        const bool hasSelection = m_selected != nullptr;
        if (ImGui::MenuItem("复制选中", "Ctrl+D", false, hasSelection)) DuplicateSelected(world);
        if (ImGui::MenuItem("删除选中", "Delete", false, hasSelection)) DeleteSelected(world);
        ImGui::Separator();
        if (ImGui::MenuItem("创建立方体")) SpawnMeshObject(world, "cube");
        if (ImGui::MenuItem("创建球体")) SpawnMeshObject(world, "sphere");
        if (ImGui::MenuItem("创建立方体（静态碰撞）")) {
            SpawnMeshObject(world, "cube");
            if (m_selected) {
                auto* body = m_selected->AddComponent<RigidBodyComponent>();
                body->shape = static_cast<int>(RigidBodyComponent::Shape::Box);
                body->halfExtents = m_selected->Scale() * 0.5f;
                body->isStatic = true;
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("显示")) {
        ImGui::MenuItem("视口", nullptr, &m_showViewport);
        ImGui::MenuItem("世界大纲", nullptr, &m_showOutliner);
        ImGui::MenuItem("检视器", nullptr, &m_showInspector);
        ImGui::MenuItem("资源浏览器", nullptr, &m_showAssets);
        ImGui::MenuItem("控制台", nullptr, &m_showConsole);
        ImGui::MenuItem("统计", nullptr, &m_showStats);
        ImGui::MenuItem("设置", nullptr, &m_showSettings);
        ImGui::Separator();
        if (ImGui::BeginMenu("视角")) {
            if (ImGui::MenuItem("透视图", nullptr, m_viewPreset == ViewPreset::Perspective,
                                !m_playing && !m_is2DLevel)) {
                SetViewPreset(editorCamera, ViewPreset::Perspective);
            }
            if (ImGui::MenuItem("正视图", nullptr, m_viewPreset == ViewPreset::Front,
                                !m_playing && !m_is2DLevel)) {
                SetViewPreset(editorCamera, ViewPreset::Front);
            }
            if (ImGui::MenuItem("俯视图", nullptr, m_viewPreset == ViewPreset::Top,
                                !m_playing && !m_is2DLevel)) {
                SetViewPreset(editorCamera, ViewPreset::Top);
            }
            ImGui::Separator();
            ImGui::TextDisabled("正/俯视为正交相机，右键拖动平移、滚轮缩放");
            ImGui::EndMenu();
        }
        ImGui::MenuItem("线框", "F2", &renderer.Wireframe());
        ImGui::MenuItem("阴影", "F4", &renderer.ShadowsEnabled());
        if (ImGui::BeginMenu("调试视图")) {
            int& view = renderer.DebugView();
            ImGui::MenuItem("正常", nullptr, view == 0, true);
            if (ImGui::MenuItem("阴影系数", nullptr, view == 1)) view = 1;
            if (ImGui::MenuItem("世界法线", nullptr, view == 2)) view = 2;
            if (ImGui::MenuItem("基础色", nullptr, view == 3)) view = 3;
            if (ImGui::MenuItem("阴影贴图", nullptr, view == 5)) view = 5;
            ImGui::EndMenu();
        }
        ImGui::MenuItem("ImGui 示例窗口", nullptr, &m_showDemo);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("帮助")) {
        if (ImGui::MenuItem("快捷键与操作")) ImGui::OpenPopup("##help");
        if (ImGui::MenuItem("关于 ErMao")) ImGui::OpenPopup("##about");
        ImGui::EndMenu();
    }

    if (ImGui::BeginPopup("##help")) {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1), "视口 / 编辑器");
        ImGui::BulletText("透视：右键拖动环视 / WASD 移动 / 滚轮推拉 / Shift 加速");
        ImGui::BulletText("正视图·俯视图：右键拖动平移 / WASD 平移 / 滚轮缩放");
        ImGui::BulletText("V 切换透视 → 正视 → 俯视    左键：拾取物体");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1), "运行模式");
        ImGui::BulletText("WASD 移动 / Shift 奔跑 / 空格 跳跃 / 右键拖动转视角 / 滚轮缩放");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1), "文件与编辑");
        ImGui::BulletText("Ctrl+N 新建   Ctrl+O 打开   Ctrl+S 保存");
        ImGui::BulletText("Ctrl+D 复制   Delete 删除   F5 运行/停止");
        ImGui::BulletText("F2 线框  F4 阴影  F11 隐藏编辑器  F12 截图");
        ImGui::Separator();
        if (ImGui::Button("知道了")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("##about")) {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1), "ErMao Engine v0.2");
        ImGui::Text("Windows / C++17 / D3D11 / Dear ImGui %s", IMGUI_VERSION);
        ImGui::TextWrapped("GAMES104 教学引擎：分层架构、前向 PBR、阴影、代码优先关卡、"
                           "反射驱动编辑器。");
        ImGui::Separator();
        ImGui::TextDisabled("关卡: game/Survivors.cpp / Survivors3D.cpp   UI: editor/EditorUI.cpp");
        ImGui::TextDisabled("文档: 文档/README.md");
        if (ImGui::Button("关闭")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // 右侧状态
    ImGui::Separator();
    const char* mode = m_playing ? (m_paused ? "已暂停" : "运行中") : "编辑";
    ImGui::TextColored(m_playing ? ImVec4(0.45f, 0.85f, 0.55f, 1) : ImVec4(0.65f, 0.68f, 0.72f, 1), "模式: %s", mode);

    if (m_showDemo) ImGui::ShowDemoWindow(&m_showDemo);
}

void Editor::DrawToolbar(World& world, Renderer& renderer, Camera& editorCamera) {
    if (ImGui::Button(m_playing ? "停止" : "运行", ImVec2(64, 26))) {
        if (m_playing) {
            ExitPlayMode(world);
        } else {
            EnterPlayMode(world);
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_playing);
    if (ImGui::Button(m_paused ? "继续" : "暂停", ImVec2(64, 26))) Pause();
    ImGui::EndDisabled();

    // Gizmo 是编辑期工具：运行时禁用，编辑时可用（之前写反了，导致“旋转 E”按钮点不动）。
    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_playing);
    {
        const char* gizmoNames[3] = {"移动 W", "旋转 E", "缩放 R"};
        for (int gi = 0; gi < 3; ++gi) {
            const bool active = (m_gizmoMode == gi);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.62f, 0.22f, 0.85f));
            if (ImGui::Button(gizmoNames[gi], ImVec2(64, 26))) m_gizmoMode = gi;
            if (active) ImGui::PopStyleColor();
            if (gi < 2) ImGui::SameLine();
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine(0, 14);
    ImGui::TextDisabled("视角");
    ImGui::SameLine();
    {
        const struct { const char* label; ViewPreset preset; } views[] = {
            {"透视", ViewPreset::Perspective},
            {"正视", ViewPreset::Front},
            {"俯视", ViewPreset::Top},
        };
        for (int vi = 0; vi < 3; ++vi) {
            const bool active = (m_viewPreset == views[vi].preset);
            const bool disabled = m_playing || (m_is2DLevel && views[vi].preset != ViewPreset::Front);
            ImGui::BeginDisabled(disabled);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.55f, 0.82f, 0.9f));
            if (ImGui::Button(views[vi].label, ImVec2(48, 26))) SetViewPreset(editorCamera, views[vi].preset);
            if (active) ImGui::PopStyleColor();
            ImGui::EndDisabled();
            if (vi < 2) ImGui::SameLine();
        }
    }

    ImGui::SameLine(0, 18);
    if (ImGui::Button("保存", ImVec2(64, 26))) SaveScene(world, false);
    ImGui::SameLine();
    if (ImGui::Button("加载", ImVec2(64, 26))) LoadScene(world);

    ImGui::SameLine(0, 18);
    ImGui::Checkbox("阴影", &renderer.ShadowsEnabled());
    ImGui::SameLine();
    ImGui::Checkbox("线框", &renderer.Wireframe());

    ImGui::SameLine(0, 18);
    ImGui::TextColored(ImVec4(0.55f, 0.58f, 0.63f, 1), "%s", FileSystem::FileNameOf(m_scenePath).c_str());

    ImGui::SameLine(ImGui::GetWindowWidth() - 230.0f);
    const auto& stats = renderer.GetStats();
    ImGui::Text("FPS %.0f | 绘制 %u | 三角 %u", ImGui::GetIO().Framerate, stats.drawCalls, stats.triangles);
}

void Editor::DrawViewport(World& world, Renderer& renderer, Camera& editorCamera,
                          const ViewportRenderer& renderViewport) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("视口", &m_showViewport, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x < 32.0f || avail.y < 32.0f) {
            ImGui::TextUnformatted("视口太小");
        } else {
            m_viewportWidth = static_cast<u32>(avail.x);
            m_viewportHeight = static_cast<u32>(avail.y);

            // 视口宽高比通常不等于窗口宽高比（默认 Dock 尤其如此）。渲染用的
            // 局部 viewCamera 会按视口设置 aspect，gizmo/拾取用的 editorCamera
            // 也必须同步到同一宽高比，否则投影与画面偏移。
            if (m_viewportWidth > 0 && m_viewportHeight > 0) {
                editorCamera.SetViewport(m_viewportWidth, m_viewportHeight);
            }

            // 运行时用游戏相机；编辑时用编辑器相机
            Camera* sceneCamera = &editorCamera;
            if (m_playing) {
                if (Camera* gameCamera = world.GetActiveCamera()) sceneCamera = gameCamera;
            }

            renderViewport(*sceneCamera, m_viewportWidth, m_viewportHeight);
            ImTextureID texture = static_cast<ImTextureID>(reinterpret_cast<intptr_t>(renderer.ViewportTexture()));
            ImGui::Image(texture, avail, ImVec2(0, 0), ImVec2(1, 1));

            const ImVec2 imageMin = ImGui::GetItemRectMin();
            m_viewportMinX = imageMin.x;
            m_viewportMinY = imageMin.y;
            m_viewportHovered = ImGui::IsItemHovered();

            // ---- 编辑器相机
            // 透视：右键环视 + WASD 飞行 + 滚轮推拉。
            // 正交（正视图/俯视图/2D 关卡）：右键拖动平移 + WASD 平移 + 滚轮缩放。
            if (!m_playing && m_viewportHovered && !ImGui::GetIO().WantTextInput) {
                ImGuiIO& io = ImGui::GetIO();
                const Vec3 right = editorCamera.Right();
                const Vec3 up = editorCamera.Up();
                const float speed = editorCamera.moveSpeed * (Input::IsKeyDown(VK_SHIFT) ? 4.0f : 1.0f) * io.DeltaTime;

                if (editorCamera.orthographic) {
                    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                        const float worldPerPixel =
                            editorCamera.orthoHeight / (std::max)(1.0f, static_cast<float>(m_viewportHeight));
                        editorCamera.position -= right * (io.MouseDelta.x * worldPerPixel);
                        editorCamera.position += up * (io.MouseDelta.y * worldPerPixel);
                    }
                    Vec3 move{0, 0, 0};
                    if (ImGui::IsKeyDown(ImGuiKey_W)) move += up;
                    if (ImGui::IsKeyDown(ImGuiKey_S)) move -= up;
                    if (ImGui::IsKeyDown(ImGuiKey_D)) move += right;
                    if (ImGui::IsKeyDown(ImGuiKey_A)) move -= right;
                    if (LengthSq(move) > 0.0f) editorCamera.position += Normalize(move) * speed;
                    if (io.MouseWheel != 0.0f) {
                        editorCamera.orthoHeight =
                            Clamp(editorCamera.orthoHeight * std::pow(0.85f, io.MouseWheel), 0.5f, 200.0f);
                    }
                } else {
                    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                        editorCamera.yaw -= io.MouseDelta.x * 0.0032f;
                        editorCamera.pitch -= io.MouseDelta.y * 0.0032f;
                        editorCamera.pitch = Clamp(editorCamera.pitch, -kHalfPi + 0.02f, kHalfPi - 0.02f);
                    }
                    if (io.MouseWheel != 0.0f) {
                        editorCamera.position += editorCamera.Forward() * (io.MouseWheel * 1.5f);
                    }
                    Vec3 move{0, 0, 0};
                    if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                        if (ImGui::IsKeyDown(ImGuiKey_W)) move += editorCamera.Forward();
                        if (ImGui::IsKeyDown(ImGuiKey_S)) move -= editorCamera.Forward();
                        if (ImGui::IsKeyDown(ImGuiKey_D)) move += right;
                        if (ImGui::IsKeyDown(ImGuiKey_A)) move -= right;
                        if (ImGui::IsKeyDown(ImGuiKey_E)) move += Vec3(0, 1, 0);
                        if (ImGui::IsKeyDown(ImGuiKey_Q)) move -= Vec3(0, 1, 0);
                    }
                    if (LengthSq(move) > 0.0f) editorCamera.position += Normalize(move) * speed;
                }

                // V：透视 → 正视 → 俯视（2D 关卡固定正视，不参与循环）
                if (!m_is2DLevel && ImGui::IsKeyPressed(ImGuiKey_V)) {
                    const ViewPreset next = m_viewPreset == ViewPreset::Perspective
                                                ? ViewPreset::Front
                                                : (m_viewPreset == ViewPreset::Front ? ViewPreset::Top
                                                                                     : ViewPreset::Perspective);
                    SetViewPreset(editorCamera, next);
                }
            }

            // ---- Gizmo 模式切换：W 移动 / E 旋转 / R 缩放
            if (!m_playing && m_viewportHovered && !ImGui::GetIO().WantTextInput &&
                !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                if (ImGui::IsKeyPressed(ImGuiKey_W)) m_gizmoMode = 0;
                if (ImGui::IsKeyPressed(ImGuiKey_E)) m_gizmoMode = 1;
                if (ImGui::IsKeyPressed(ImGuiKey_R)) m_gizmoMode = 2;
            }

            // ---- 位移/旋转/缩放 gizmo（先于拾取，避免拖拽轴时改选中）
            if (!m_playing && m_gizmoEnabled && m_selected && m_viewportHovered) {
                DrawGizmo(world, editorCamera, imageMin,
                          ImVec2(static_cast<float>(m_viewportWidth), static_cast<float>(m_viewportHeight)));
            }

            // ---- 点击拾取
            if (!m_playing && m_viewportHovered && m_gizmoAxis < 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                PickObject(world, editorCamera, ImVec2(mouse.x - imageMin.x, mouse.y - imageMin.y),
                           ImVec2(static_cast<float>(m_viewportWidth), static_cast<float>(m_viewportHeight)));
            }

            // ---- 左上角 HUD（运行时让位给游戏自己的 HUD）
            if (!m_playing) {
                ImGui::SetCursorPos(ImVec2(12, 30));
                ImGui::BeginGroup();
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1), "编辑器视图");
                ImGui::Text("相机 (%.1f, %.1f, %.1f)", sceneCamera->position.x, sceneCamera->position.y,
                            sceneCamera->position.z);
                ImGui::Text("视口 %ux%u", m_viewportWidth, m_viewportHeight);
                ImGui::Text("视角: %s%s", ViewPresetName(m_viewPreset),
                            editorCamera.orthographic ? " (正交)" : " (透视)");
                ImGui::Text("镜头速度: %.1f m/s（设置面板可调）", editorCamera.moveSpeed);
                if (editorCamera.orthographic) {
                    ImGui::TextDisabled("右键拖动平移 / WASD 平移 / 滚轮缩放");
                } else {
                    ImGui::TextDisabled("右键拖动环视 / WASD 移动 / 滚轮推拉 / V 切换视角");
                }
                ImGui::EndGroup();
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void Editor::DrawOutliner(World& world) {
    if (!ImGui::Begin("世界大纲", &m_showOutliner)) {
        ImGui::End();
        return;
    }

    static char filter[128] = {};
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "搜索对象…", filter, sizeof(filter));

    if (ImGui::Button("+ 物体")) ImGui::OpenPopup("CreateObject");
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_selected);
    if (ImGui::Button("删除")) DeleteSelected(world);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_selected);
    if (ImGui::Button("复制")) DuplicateSelected(world);
    ImGui::EndDisabled();

    if (ImGui::BeginPopup("CreateObject")) {
        if (ImGui::MenuItem("立方体")) SpawnMeshObject(world, "cube");
        if (ImGui::MenuItem("球体")) SpawnMeshObject(world, "sphere");
        if (ImGui::MenuItem("圆柱")) SpawnMeshObject(world, "cylinder");
        if (ImGui::MenuItem("圆环")) SpawnMeshObject(world, "torus");
        if (ImGui::MenuItem("空对象")) {
            GameObject* object = world.CreateGameObject("空对象");
            Select(object);
        }
        if (ImGui::MenuItem("点光源")) {
            GameObject* light = world.CreateGameObject("点光源");
            light->SetPosition(Camera().position + Vec3(0, 4, 0));
            auto* point = light->AddComponent<PointLightComponent>();
            point->intensity = 8.0f;
            Select(light);
        }
        if (ImGui::MenuItem("相机")) {
            GameObject* camera = world.CreateGameObject("相机");
            auto* component = camera->AddComponent<CameraComponent>();
            component->primary = false;
            Select(camera);
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();
    ImGui::BeginChild("##tree", ImVec2(0, 0), false);
    const std::string filterText = filter;
    for (GameObject* root : world.RootObjects()) {
        if (filterText.empty()) {
            DrawOutlinerNode(world, root);
        } else {
            bool matched = false;
            root->ForEach([&](GameObject* object) {
                if (object->Name().find(filterText) != std::string::npos) matched = true;
            });
            if (matched) DrawOutlinerNode(world, root);
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

void Editor::DrawOutlinerNode(World& world, GameObject* object) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_DefaultOpen;
    if (object->Children().empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (object == m_selected) flags |= ImGuiTreeNodeFlags_Selected;

    const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(object->Id())), flags, "%s",
                                        object->Name().c_str());
    if (ImGui::IsItemClicked()) {
        Select(object);
    }

    if (ImGui::BeginPopupContextItem()) {
        Select(object);
        if (ImGui::MenuItem("复制")) DuplicateSelected(world);
        if (ImGui::MenuItem("删除")) DeleteSelected(world);
        if (ImGui::MenuItem("移到原点")) object->SetPosition(Vec3(0, 0, 0));
        ImGui::EndPopup();
    }

    if (open) {
        for (GameObject* child : object->Children()) DrawOutlinerNode(world, child);
        ImGui::TreePop();
    }
}

void Editor::DrawInspector(World& world, AssetManager& assets) {
    if (!ImGui::Begin("检视器", &m_showInspector)) {
        ImGui::End();
        return;
    }

    if (!m_selected) {
        ImGui::TextColored(ImVec4(0.6f, 0.63f, 0.68f, 1), "未选中对象");
        ImGui::TextWrapped("在视口左键点击，或在世界大纲里选择对象。");
        ImGui::Separator();
        ImGui::Text("场景统计");
        ImGui::BulletText("对象: %zu", world.ObjectCount());
        ImGui::BulletText("根节点: %zu", world.RootObjects().size());
        ImGui::End();
        return;
    }

    // 名字（可编辑）
    char nameBuffer[128];
    std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", m_selected->Name().c_str());
    ImGui::SetNextItemWidth(-70);
    if (ImGui::InputText("##name", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
        m_selected->SetName(nameBuffer);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("ID %llu", static_cast<unsigned long long>(m_selected->Id()));

    bool active = m_selected->Active();
    if (ImGui::Checkbox("启用", &active)) m_selected->SetActive(active);

    ImGui::Separator();
    DrawTransform(m_selected);
    ImGui::Separator();
    DrawComponents(world, m_selected, assets);

    ImGui::End();
}

void Editor::DrawTransform(GameObject* object) {
    if (!ImGui::CollapsingHeader("变换 (Transform)", ImGuiTreeNodeFlags_DefaultOpen)) return;

    Vec3 position = object->Position();
    if (ImGui::DragFloat3("位置", &position.x, 0.05f)) object->SetPosition(position);

    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    object->Rotation().ToEuler(yaw, pitch, roll);
    Vec3 euler(ToDegrees(pitch), ToDegrees(yaw), ToDegrees(roll));
    if (ImGui::DragFloat3("旋转", &euler.x, 0.5f)) {
        object->SetRotation(Quat::FromEuler(ToRadians(euler.y), ToRadians(euler.x), ToRadians(euler.z)));
    }

    Vec3 scale = object->Scale();
    if (ImGui::DragFloat3("缩放", &scale.x, 0.02f, 0.001f, 1000.0f)) object->SetScale(scale);
}

void Editor::DrawComponents(World& world, GameObject* object, AssetManager& assets) {
    (void)world;
    Component* toRemove = nullptr;

    for (const auto& component : object->Components()) {
        const TypeInfo& info = component->GetTypeInfo();
        ImGui::PushID(component.get());
        if (ImGui::CollapsingHeader(info.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const auto& field : info.fields) {
                if (field.hidden) continue;
                void* value = field.access(component.get());
                ImGui::PushID(field.name.c_str());
                DrawField(field, value, assets);
                ImGui::PopID();
            }
            if (ImGui::Button("移除组件")) toRemove = component.get();
        }
        ImGui::PopID();
    }

    if (toRemove) object->RemoveComponent(toRemove);

    ImGui::Spacing();
    if (ImGui::Button("+ 添加组件", ImVec2(-1, 0))) ImGui::OpenPopup("AddComponent");
    if (ImGui::BeginPopup("AddComponent")) {
        for (const TypeInfo* type : Reflection::All()) {
            if (!type->factory) continue;
            if (ImGui::MenuItem(type->name.c_str())) {
                object->AddComponentByTypeName(type->name);
            }
        }
        ImGui::EndPopup();
    }
}

bool Editor::DrawField(const FieldDesc& field, void* object, AssetManager& assets) {
    bool changed = false;
    switch (field.type) {
        case FieldType::Bool: {
            changed = ImGui::Checkbox(field.label.c_str(), static_cast<bool*>(object));
            break;
        }
        case FieldType::Int: {
            int* value = static_cast<int*>(object);
            if (field.name == "shape") {
                int shape = Clamp(*value, 0, 1);
                const char* items[] = {"球体", "盒体"};
                if (ImGui::Combo(field.label.c_str(), &shape, items, 2)) {
                    *value = shape;
                    changed = true;
                }
            } else {
                changed = ImGui::DragInt(field.label.c_str(), value, 1.0f, static_cast<int>(field.min),
                                         static_cast<int>(field.max));
            }
            break;
        }
        case FieldType::Float: {
            float* value = static_cast<float*>(object);
            changed = ImGui::DragFloat(field.label.c_str(), value, 0.01f, field.min, field.max, "%.3f");
            break;
        }
        case FieldType::Vec2:
            break;
        case FieldType::Vec3: {
            Vec3* value = static_cast<Vec3*>(object);
            changed = ImGui::DragFloat3(field.label.c_str(), &value->x, 0.02f);
            break;
        }
        case FieldType::Vec4:
        case FieldType::Color: {
            Vec4* value = static_cast<Vec4*>(object);
            float color[4] = {value->x, value->y, value->z, value->w};
            if (ImGui::ColorEdit4(field.label.c_str(), color)) {
                *value = Vec4(color[0], color[1], color[2], color[3]);
                changed = true;
            }
            break;
        }
        case FieldType::String: {
            std::string* value = static_cast<std::string*>(object);
            char buffer[256];
            std::snprintf(buffer, sizeof(buffer), "%s", value->c_str());
            if (ImGui::InputText(field.label.c_str(), buffer, sizeof(buffer),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                *value = buffer;
                changed = true;
            }
            break;
        }
        case FieldType::MeshAsset: {
            std::string* value = static_cast<std::string*>(object);
            std::vector<std::string> names{"cube", "sphere", "plane", "torus", "cylinder", "capsule"};
            for (const auto& asset : assets.Assets()) {
                if (asset.type == AssetManager::AssetType::Mesh && !asset.builtin) names.push_back(asset.name);
            }
            if (ImGui::BeginCombo(field.label.c_str(), value->c_str())) {
                for (const auto& name : names) {
                    const bool selected = (*value == name);
                    if (ImGui::Selectable(name.c_str(), selected)) {
                        *value = name;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            break;
        }
        case FieldType::TextureAsset: {
            std::string* value = static_cast<std::string*>(object);
            std::vector<std::string> names{"(无)", "white", "checker", "grid", "noise"};
            for (const auto& asset : assets.Assets()) {
                if (asset.type == AssetManager::AssetType::Texture && !asset.builtin) names.push_back(asset.name);
            }
            const char* preview = value->empty() ? "(无)" : value->c_str();
            if (ImGui::BeginCombo(field.label.c_str(), preview)) {
                for (const auto& name : names) {
                    const bool isNone = (name == "(无)");
                    const bool selected = isNone ? value->empty() : (*value == name);
                    if (ImGui::Selectable(name.c_str(), selected)) {
                        *value = isNone ? "" : name;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            break;
        }
    }
    return changed;
}

void Editor::LoadSceneAsset(World& world, const std::string& name, const std::string& sourcePath) {
    // 内置关卡：交给 Engine 重建（带 2D/3D 模式与玩法导演）
    if (sourcePath.rfind("level://", 0) == 0) {
        const std::string id = sourcePath.substr(8);
        if (m_levelLoader) {
            m_levelLoader(id);
            SetStatus("已打开关卡: " + name);
        } else {
            SetStatus("关卡加载器未初始化", true);
        }
        return;
    }
    if (world.LoadFromFile(sourcePath)) {
        // LoadFromFile 会整体替换 World，旧的选中项已随 Clear 释放。
        Select(nullptr);
        m_scenePath = sourcePath;
        SetStatus("场景已加载: " + name);
        if (m_sceneLoaded) m_sceneLoaded(world.LevelKind());
    } else {
        SetStatus("场景加载失败: " + name, true);
    }
}

void Editor::DrawAssets(World& world, AssetManager& assets, Renderer& renderer, Camera& editorCamera) {
    if (!ImGui::Begin("资源浏览器", &m_showAssets)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("导入模型")) ImportModel(world, assets, editorCamera);
    ImGui::SameLine();
    if (ImGui::Button("导入贴图")) ImportTexture(assets);
    ImGui::SameLine();
    if (ImGui::Button("刷新")) {
        assets.Refresh();
        SetStatus("资源列表已刷新");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##assetfilter", "搜索资源…", m_assetFilterBuffer, sizeof(m_assetFilterBuffer));
    m_assetFilter = m_assetFilterBuffer;

    ImGui::Separator();

    // 左：分类；右：缩略图网格（Unity 风格）
    const float categoryWidth = 86.0f;
    ImGui::BeginChild("##assetCategories", ImVec2(categoryWidth, 0), true);
    const char* categories[] = {"全部", "模型", "贴图", "场景"};
    for (int i = 0; i < 4; ++i) {
        if (ImGui::Selectable(categories[i], m_assetCategory == i)) m_assetCategory = i;
    }
    ImGui::Separator();
    ImGui::TextDisabled("%zu 项", assets.Assets().size());
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##assetGrid", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.66f), true);
    {
        const auto& list = assets.Assets();
        const float tile = 104.0f;
        const float avail = ImGui::GetContentRegionAvail().x;
        int columns = static_cast<int>(avail / (tile + 16.0f));
        if (columns < 1) columns = 1;

        if (ImGui::BeginTable("##assetTiles", columns, ImGuiTableFlags_SizingStretchSame)) {
            for (int i = 0; i < static_cast<int>(list.size()); ++i) {
                // 资源浏览器遍历期间可能加载关卡，向 AssetManager 注册新资源并导致 m_registry 扩容；
                // list 本身（vector 对象）仍有效，但元素引用会失效，必须按值快照后再继续使用。
                const AssetManager::AssetInfo asset = list[i];
                if (m_assetCategory == 1 && asset.type != AssetManager::AssetType::Mesh) continue;
                if (m_assetCategory == 2 && asset.type != AssetManager::AssetType::Texture) continue;
                if (m_assetCategory == 3 && asset.type != AssetManager::AssetType::Scene) continue;
                if (!m_assetFilter.empty() && asset.name.find(m_assetFilter) == std::string::npos) continue;

                ImGui::TableNextColumn();
                ImGui::PushID(i);
                ImGui::BeginGroup();

                ID3D11ShaderResourceView* srv = nullptr;
                if (asset.type == AssetManager::AssetType::Mesh) {
                    srv = renderer.GetMeshThumbnail(assets.GetMesh(asset.name));
                } else if (asset.type == AssetManager::AssetType::Texture) {
                    srv = renderer.GetTextureThumbnail(assets.GetTexture(asset.name));
                }

                const bool selected = (m_selectedAsset == i);
                if (srv) {
                    ImVec2 cursor = ImGui::GetCursorScreenPos();
                    ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(srv)), ImVec2(tile, tile));
                    // 选中/悬停描边
                    if (selected) {
                        ImGui::GetWindowDrawList()->AddRect(cursor, ImVec2(cursor.x + tile, cursor.y + tile),
                                                            IM_COL32(240, 190, 80, 255), 4.0f, 0, 2.5f);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::GetWindowDrawList()->AddRect(cursor, ImVec2(cursor.x + tile, cursor.y + tile),
                                                            IM_COL32(255, 255, 255, 120), 4.0f, 0, 1.5f);
                    }
                } else {
                    // 场景等无预览资产：用按钮占位
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.18f, 0.22f, 1.0f));
                    ImGui::Button(asset.type == AssetManager::AssetType::Scene ? "场景" : "?", ImVec2(tile, tile));
                    ImGui::PopStyleColor();
                }

                if (ImGui::IsItemClicked()) {
                    m_selectedAsset = i;
                    // 关卡是"动作"而不是"文件"：单击直接打开（双击也保留）
                    if (asset.type == AssetManager::AssetType::Scene &&
                        asset.sourcePath.rfind("level://", 0) == 0) {
                        LoadSceneAsset(world, asset.name, asset.sourcePath);
                    }
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (asset.type == AssetManager::AssetType::Mesh) {
                        SpawnMeshObject(world, asset.name);
                        SetStatus("已实例化资源: " + asset.name);
                    } else if (asset.type == AssetManager::AssetType::Scene) {
            LoadSceneAsset(world, asset.name, asset.sourcePath);
                    } else if (asset.type == AssetManager::AssetType::Texture) {
                        if (m_selected && m_selected->GetComponent<MeshRenderer>()) {
                            m_selected->GetComponent<MeshRenderer>()->texture = asset.name;
                            SetStatus("已把贴图赋给选中对象: " + asset.name);
                        }
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s\n%s", asset.name.c_str(),
                                      asset.type == AssetManager::AssetType::Mesh      ? "模型"
                                      : asset.type == AssetManager::AssetType::Texture ? "贴图"
                                                                                       : "场景");
                }

                // 文件名（过长截断）
                std::string label = FileSystem::FileNameOf(asset.name);
                if (label.size() > 14) label = label.substr(0, 13) + "…";
                ImGui::TextWrapped("%s", label.c_str());

                ImGui::EndGroup();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (list.empty()) ImGui::TextDisabled("还没有资源，点上方“导入模型”。");
    }
    ImGui::EndChild();

    // 详情
    ImGui::Separator();
    const auto& list = assets.Assets();
    if (m_selectedAsset >= 0 && m_selectedAsset < static_cast<int>(list.size())) {
        const auto& asset = list[m_selectedAsset];
        ImGui::Text("%s", asset.name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6f, 0.63f, 0.68f, 1), "[%s]",
                           asset.type == AssetManager::AssetType::Mesh      ? "模型"
                           : asset.type == AssetManager::AssetType::Texture ? "贴图"
                                                                            : "场景");
        if (asset.type == AssetManager::AssetType::Mesh) {
            ImGui::Text("顶点 %u / 索引 %u", asset.vertexCount, asset.indexCount);
        }
        if (!asset.sourcePath.empty()) ImGui::TextDisabled("%s", asset.sourcePath.c_str());
        if (asset.type == AssetManager::AssetType::Mesh && ImGui::Button("放入场景")) {
            SpawnMeshObject(world, asset.name);
        }
        if (asset.type == AssetManager::AssetType::Texture && m_selected) {
            ImGui::SameLine();
            if (ImGui::Button("赋给选中对象")) {
                if (auto* renderer2 = m_selected->GetComponent<MeshRenderer>()) renderer2->texture = asset.name;
            }
        }
        if (asset.type == AssetManager::AssetType::Scene && ImGui::Button("加载场景")) {
            LoadSceneAsset(world, asset.name, asset.sourcePath);
        }
    } else {
        ImGui::TextDisabled("选择资源查看详情；双击模型放入场景。");
    }

    ImGui::End();
    (void)renderer;
}
void Editor::DrawConsole() {
    if (!ImGui::Begin("控制台", &m_showConsole)) {
        ImGui::End();
        return;
    }

    ImGui::Checkbox("信息", &m_consoleInfo);
    ImGui::SameLine();
    ImGui::Checkbox("警告", &m_consoleWarn);
    ImGui::SameLine();
    ImGui::Checkbox("错误", &m_consoleError);
    ImGui::SameLine();
    ImGui::Checkbox("自动滚动", &m_consoleAutoScroll);
    ImGui::SameLine();
    if (ImGui::Button("清空")) Log::Clear();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180);
    ImGui::InputTextWithHint("##consolefilter", "过滤…", m_consoleFilter, sizeof(m_consoleFilter));

    ImGui::Separator();
    ImGui::BeginChild("##console", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    const auto& entries = Log::Entries();
    for (const auto& entry : entries) {
        if (entry.level == LogLevel::Info && !m_consoleInfo) continue;
        if (entry.level == LogLevel::Warn && !m_consoleWarn) continue;
        if (entry.level == LogLevel::Error && !m_consoleError) continue;
        if (m_consoleFilter[0] && entry.message.find(m_consoleFilter) == std::string::npos) continue;

        ImVec4 color(0.72f, 0.75f, 0.80f, 1.0f);
        const char* prefix = "信息";
        if (entry.level == LogLevel::Warn) {
            color = ImVec4(0.95f, 0.75f, 0.32f, 1.0f);
            prefix = "警告";
        } else if (entry.level == LogLevel::Error) {
            color = ImVec4(0.95f, 0.40f, 0.36f, 1.0f);
            prefix = "错误";
        } else if (entry.level == LogLevel::Trace) {
            color = ImVec4(0.55f, 0.58f, 0.63f, 1.0f);
            prefix = "跟踪";
        }
        ImGui::TextColored(ImVec4(0.45f, 0.48f, 0.53f, 1.0f), "[%7.2fs]", entry.time);
        ImGui::SameLine();
        ImGui::TextColored(color, "[%s] %s", prefix, entry.message.c_str());
    }
    if (m_consoleAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::End();
}

void Editor::DrawStats(World& world, Renderer& renderer) {
    if (!ImGui::Begin("统计", &m_showStats)) {
        ImGui::End();
        return;
    }

    const auto& stats = renderer.GetStats();
    ImGui::Text("FPS: %.1f (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / (std::max)(1.0f, ImGui::GetIO().Framerate));
    ImGui::Text("CPU 提交: 阴影 %.3f / 不透明 %.3f / 后处理 %.3f ms", stats.cpuShadowMs,
                stats.cpuOpaqueMs, stats.cpuPostMs);
    if (stats.gpuTimingValid) {
        if (stats.gpuRenderValid) {
            ImGui::Text("GPU 渲染: %.2f ms", stats.gpuRenderMs);
        } else {
            ImGui::TextDisabled("GPU 渲染: --");
        }
        ImGui::Text("GPU 帧跨度: %.2f ms", stats.gpuFrameMs);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shadow→Post 是渲染成本；帧跨度含 Present/vsync 等待，\n"
                              "vsync 开启时会接近一帧时间（16.7ms）。");
        }
        ImGui::SameLine();
        ImGui::TextDisabled(stats.gpuTimingValid ? "(有效)" : "(等待查询)");
    } else {
        ImGui::TextDisabled("GPU 时间戳: 采集中…");
    }
    ImGui::Separator();
    ImGui::Text("绘制调用: %u", stats.drawCalls);
        if (stats.gpuCullEnabled) {
            ImGui::Text("GPU 剔除: 可见实例 %u", stats.gpuVisibleInstances);
        } else {
            ImGui::TextDisabled("GPU 剔除: 关闭（CPU 批处理）");
        }
    ImGui::Text("实例: %u   三角形: %u", stats.instances, stats.triangles);
    ImGui::Text("剔除: %u", stats.culled);
    ImGui::Text("对象: %zu", world.ObjectCount());
    if (auto* physics = world.Physics()) {
        ImGui::Text("物理: 动态 %d / 静态 %d / 接触 %d", physics->Stats().dynamicBodies,
                    physics->Stats().staticBodies, physics->Stats().contacts);
    }

    ImGui::Separator();
    ImGui::Text("帧时间 (ms)");
    ImGui::PlotLines("##frametimes", m_frameTimes, IM_ARRAYSIZE(m_frameTimes), m_frameTimeOffset, nullptr, 0.0f,
                     33.3f, ImVec2(0, 60));

    ImGui::Separator();
    ImGui::Text("JobSystem：%u 线程", JobSystem::ThreadCount());
    if (ImGui::Button("CPU 基准 (100 万次)")) {
        m_benchSerial = JobSystem::Benchmark(false, 1000000);
        m_benchParallel = JobSystem::Benchmark(true, 1000000);
    }
    if (m_benchSerial > 0.0f) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1), "串行 %.2f ms / 并行 %.2f ms  (%.1fx)", m_benchSerial,
                           m_benchParallel, m_benchSerial / (std::max)(0.001f, m_benchParallel));
    }

    ImGui::End();
}

void Editor::DrawSettings(World& world, Renderer& renderer, Camera& editorCamera) {
    const bool focusNow = m_focusSettings;
    m_focusSettings = false;
    if (focusNow) ImGui::SetNextWindowFocus();
    if (!ImGui::Begin("设置", &m_showSettings)) {
        ImGui::End();
        return;
    }
    if (focusNow) ImGui::SetWindowFocus();

    if (ImGui::CollapsingHeader("编辑器 / 镜头", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("镜头速度", &editorCamera.moveSpeed, 1.0f, 40.0f, "%.1f m/s");
        ImGui::TextDisabled("Shift 加速 4 倍；正交视图下是平移速度。");
        ImGui::Text("当前视角: %s", ViewPresetName(m_viewPreset));
        ImGui::BeginDisabled(m_playing || m_is2DLevel);
        if (ImGui::Button("正视图")) SetViewPreset(editorCamera, ViewPreset::Front);
        ImGui::SameLine();
        if (ImGui::Button("俯视图")) SetViewPreset(editorCamera, ViewPreset::Top);
        ImGui::SameLine();
        if (ImGui::Button("透视图")) SetViewPreset(editorCamera, ViewPreset::Perspective);
        ImGui::EndDisabled();
        if (ImGui::Button("聚焦选中物体 (F)")) FocusSelected(world, editorCamera);
        ImGui::Separator();
    }

    if (ImGui::CollapsingHeader("渲染", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("曝光", &renderer.Exposure(), 0.2f, 3.0f, "%.2f");
        ImGui::Checkbox("阴影", &renderer.ShadowsEnabled());
        ImGui::Checkbox("线框", &renderer.Wireframe());
        int& debugView = renderer.DebugView();
        const char* views[] = {"正常", "阴影系数", "世界法线", "基础色", "阴影贴图"};
        int index = debugView == 1 ? 1 : debugView == 2 ? 2 : debugView == 3 ? 3 : debugView == 5 ? 4 : 0;
        if (ImGui::Combo("调试视图", &index, views, IM_ARRAYSIZE(views))) {
            const int mapped[] = {0, 1, 2, 3, 5};
            debugView = mapped[index];
        }
    }

    if (ImGui::CollapsingHeader("光照", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("太阳强度", &world.sunIntensity, 0.0f, 10.0f);
        ImGui::ColorEdit3("太阳颜色", &world.sunColor.x);
        ImGui::DragFloat3("环境光(天)", &world.ambientUp.x, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat3("环境光(地)", &world.ambientDown.x, 0.01f, 0.0f, 1.0f);
        ImGui::SliderFloat("环境强度", &world.ambientIntensity, 0.0f, 3.0f);
    }

    if (ImGui::CollapsingHeader("物理")) {
        if (auto* physics = world.Physics()) {
            float gravity = physics->Gravity();
            if (ImGui::SliderFloat("重力", &gravity, -30.0f, 0.0f)) physics->SetGravity(gravity);
            int iterations = physics->SolverIterations();
            if (ImGui::SliderInt("迭代次数", &iterations, 1, 10)) physics->SetSolverIterations(iterations);
        }
    }

    if (ImGui::CollapsingHeader("导航")) {
        const NavGrid& nav = world.Nav();
        ImGui::Text("网格: %d x %d (%.2f m/格)", nav.Width(), nav.Height(), nav.CellSize());
        if (ImGui::Button("重建导航网格")) {
            world.RebuildNavigation();
            SetStatus("导航网格已重建");
        }
    }

    ImGui::End();
}

void Editor::DrawStatusBar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float height = 26.0f;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - height));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, height));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoDocking;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 4));
    if (ImGui::Begin("##StatusBar", nullptr, flags)) {
        ImGui::TextColored(m_statusIsError ? ImVec4(0.95f, 0.4f, 0.36f, 1) : ImVec4(0.6f, 0.78f, 0.95f, 1), "%s",
                           m_statusMessage.c_str());
        ImGui::SameLine(ImGui::GetWindowWidth() - 360.0f);
        ImGui::TextDisabled("ErMao v0.2 | Dear ImGui %s | 右键旋转 / WASD 移动 / 左键拾取", IMGUI_VERSION);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------- 操作
void Editor::SpawnMeshObject(World& world, const std::string& meshName) {
    GameObject* object = world.CreateGameObject(meshName);
    object->SetPosition(Vec3(0, 1, 0));
    auto* renderer = object->AddComponent<MeshRenderer>();
    renderer->mesh = meshName;
    Select(object);
    SetStatus("已创建对象: " + object->Name());
}

void Editor::DeleteSelected(World& world) {
    if (!m_selected) return;
    const std::string name = m_selected->Name();
    world.DestroyObject(m_selected);
    Select(nullptr);
    SetStatus("已删除: " + name);
}

void Editor::DuplicateSelected(World& world) {
    if (!m_selected) return;
    // 简单复制：位置偏移 + 复制 MeshRenderer 参数
    GameObject* copy = world.CreateGameObject(m_selected->Name() + "_副本", m_selected->Parent());
    copy->SetPosition(m_selected->Position() + Vec3(1.2f, 0.0f, 1.2f));
    copy->SetRotation(m_selected->Rotation());
    copy->SetScale(m_selected->Scale());
    if (auto* source = m_selected->GetComponent<MeshRenderer>()) {
        auto* target = copy->AddComponent<MeshRenderer>();
        *target = *source;
    }
    Select(copy);
    SetStatus("已复制对象");
}

void Editor::DrawGizmo(World& world, Camera& editorCamera, const ImVec2& viewportMin, const ImVec2& viewportSize) {
    (void)world;
    if (!m_selected) return;

    const Vec3  origin = m_selected->WorldPosition();
    const float distance = Length(editorCamera.position - origin);
    const float axisLength = Clamp(distance * 0.16f, 0.35f, 6.0f);

    const Vec3  axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const ImU32 colors[3] = {IM_COL32(232, 78, 78, 255), IM_COL32(96, 208, 96, 255), IM_COL32(88, 140, 240, 255)};

    auto project = [&](const Vec3& worldPos, ImVec2& out) -> bool {
        Vec2 screen;
        if (!editorCamera.WorldToScreen(worldPos, Vec2(viewportSize.x, viewportSize.y), screen)) return false;
        out = ImVec2(viewportMin.x + screen.x, viewportMin.y + screen.y);
        return true;
    };

    ImVec2 screenOrigin;
    if (!project(origin, screenOrigin)) return;

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool   activeDrag = m_gizmoAxis >= 0;

    if (m_gizmoMode == 1) {
        // ---------------- 旋转：三个局部坐标环 ----------------
        // 环跟随物体自身的 X/Y/Z（和检视器里的旋转字段语义一致）。拖动时
        // 冻结起始朝向，只累计“绕该环”的角度，避免斜着拖一下同时改变三个角。
        const Quat  referenceRotation = activeDrag ? m_gizmoStartRotation : m_selected->Rotation();
        const Mat4  parentWorld = m_selected->Parent() ? m_selected->Parent()->WorldMatrix() : Mat4::Identity();
        const Vec3  unitAxes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        Vec3        ringAxes[3];
        for (int i = 0; i < 3; ++i) {
            ringAxes[i] = Normalize(parentWorld.TransformVector(referenceRotation.RotateVector(unitAxes[i])));
        }
        const Vec3 toCamera = editorCamera.position - origin;

        auto ringBasis = [](const Vec3& n, Vec3& u, Vec3& v) {
            u = Normalize(Cross(n, std::fabs(n.y) > 0.9f ? Vec3(1, 0, 0) : Vec3(0, 1, 0)));
            v = Cross(n, u);
        };

        int    hovered = -1;
        int    hoveredSample = 0;
        float  best = 9.0f;
        for (int i = 0; i < 3; ++i) {
            const Vec3 n = ringAxes[i];
            Vec3       u, v;
            ringBasis(n, u, v);

            constexpr int kSamples = 48;
            ImVec2        points[kSamples + 1];
            bool          valid[kSamples + 1] = {false};
            bool          front[kSamples + 1] = {false};
            int           validCount = 0;
            for (int k = 0; k <= kSamples; ++k) {
                const float angle = kTwoPi * static_cast<float>(k) / static_cast<float>(kSamples);
                const Vec3  p = origin + (u * std::cos(angle) + v * std::sin(angle)) * axisLength;
                if (project(p, points[k])) {
                    valid[k] = true;
                    front[k] = Dot(p - origin, toCamera) >= 0.0f;
                    ++validCount;
                }
            }
            if (validCount < 8) continue;

            // 只命中朝向相机的前半环：后半环画得淡一些，避免“点到的和看到的不一致”。
            for (int k = 1; k <= kSamples; ++k) {
                if (!valid[k] || !valid[k - 1] || !front[k] || !front[k - 1]) continue;
                const ImVec2 a = points[k - 1];
                const ImVec2 b = points[k];
                const ImVec2 ab(b.x - a.x, b.y - a.y);
                const float  len2 = ab.x * ab.x + ab.y * ab.y;
                if (len2 < 1.0f) continue;
                float t = ((mouse.x - a.x) * ab.x + (mouse.y - a.y) * ab.y) / len2;
                t = Clamp(t, 0.0f, 1.0f);
                const float dx = mouse.x - (a.x + ab.x * t);
                const float dy = mouse.y - (a.y + ab.y * t);
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist < best) {
                    best = dist;
                    hovered = i;
                    hoveredSample = k;
                }
            }

            // 绘制环：前半环实色，后半环淡色
            const bool active = (i == hovered) || (i == m_gizmoAxis);
            ImVec2 last;
            bool   hasLast = false;
            for (int k = 0; k <= kSamples; ++k) {
                if (!valid[k]) { hasLast = false; continue; }
                if (hasLast) {
                    const bool  isFront = front[k] && front[k - 1];
                    const u8    alpha = isFront ? 255 : 64;
                    const ImU32 color = (colors[i] & 0x00FFFFFFu) | (static_cast<ImU32>(alpha) << 24);
                    draw->AddLine(last, points[k], color, (active && isFront) ? 3.5f : (isFront ? 2.2f : 1.2f));
                }
                last = points[k];
                hasLast = true;
            }
        }

        if (!activeDrag && hovered >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            Vec3 u, v;
            ringBasis(ringAxes[hovered], u, v);
            m_gizmoAxis = hovered;
            m_gizmoStartMouseX = mouse.x;
            m_gizmoStartMouseY = mouse.y;
            m_gizmoStartRotation = m_selected->Rotation();
            m_rotateGrabTheta = kTwoPi * static_cast<float>(hoveredSample) / 48.0f;
            m_rotateAccum = 0.0f;
            m_rotateLastMouseX = mouse.x;
            m_rotateLastMouseY = mouse.y;
            m_rotateScreenRadius = (std::max)(std::sqrt((screenOrigin.x - mouse.x) * (screenOrigin.x - mouse.x) +
                                                       (screenOrigin.y - mouse.y) * (screenOrigin.y - mouse.y)),
                                              14.0f);
        }

        if (activeDrag) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && m_gizmoAxis >= 0) {
                const int  axis = m_gizmoAxis;
                Vec3       u, v;
                ringBasis(ringAxes[axis], u, v);

                const float theta = m_rotateGrabTheta + m_rotateAccum;
                const Vec3  point = origin + (u * std::cos(theta) + v * std::sin(theta)) * axisLength;
                ImVec2      p0, p1, p2;
                Vec2        tangent(1, 0);
                if (project(point, p0) && project(origin + (u * std::cos(theta + 0.01f) + v * std::sin(theta + 0.01f)) * axisLength, p1) &&
                    project(origin + (u * std::cos(theta - 0.01f) + v * std::sin(theta - 0.01f)) * axisLength, p2)) {
                    const ImVec2 t(p1.x - p2.x, p1.y - p2.y);
                    const float  len = std::sqrt(t.x * t.x + t.y * t.y);
                    if (len > 1e-3f) tangent = Vec2(t.x / len, t.y / len);
                }
                const float deltaX = mouse.x - m_rotateLastMouseX;
                const float deltaY = mouse.y - m_rotateLastMouseY;
                const float denom = (std::max)(m_rotateScreenRadius, 14.0f);
                const float deltaAngle = Clamp((deltaX * tangent.x + deltaY * tangent.y) / denom, -0.35f, 0.35f);
                m_rotateAccum += deltaAngle;
                m_rotateLastMouseX = mouse.x;
                m_rotateLastMouseY = mouse.y;

                const Quat rotation =
                    (m_gizmoStartRotation * Quat::FromAxisAngle(unitAxes[axis], m_rotateAccum)).Normalized();
                m_selected->SetRotation(rotation);
                m_rotateScreenRadius = (std::max)(m_rotateScreenRadius, 14.0f);

                const char* axisName = axis == 0 ? "X" : (axis == 1 ? "Y" : "Z");
                char        label[64];
                std::snprintf(label, sizeof(label), "绕 %s: %.0f°", axisName, ToDegrees(m_rotateAccum));
                draw->AddText(ImVec2(mouse.x + 16.0f, mouse.y + 8.0f), IM_COL32(245, 230, 180, 255), label);
            } else {
                const int  axis = m_gizmoAxis;
                const char* axisName = axis == 0 ? "X" : (axis == 1 ? "Y" : "Z");
                char        label[96];
                std::snprintf(label, sizeof(label), "旋转 %s: %.1f°", axisName, ToDegrees(m_rotateAccum));
                SetStatus(label);
                m_gizmoAxis = -1;
            }
        }
        draw->AddCircleFilled(screenOrigin, 3.0f, IM_COL32(240, 240, 240, 200));
        return;
    }

    // ---------------- 位移 / 缩放：三轴 ----------------
    int    hoveredAxis = -1;
    float  bestDistance = 10.0f;
    ImVec2 axisEnds[3];
    bool   axisValid[3] = {false, false, false};
    for (int i = 0; i < 3; ++i) {
        axisValid[i] = project(origin + axes[i] * axisLength, axisEnds[i]);
        if (!axisValid[i]) continue;
        const ImVec2 a = screenOrigin;
        const ImVec2 b = axisEnds[i];
        const ImVec2 ab(b.x - a.x, b.y - a.y);
        const float  lengthSq = ab.x * ab.x + ab.y * ab.y;
        if (lengthSq < 1.0f) continue;
        float t = ((mouse.x - a.x) * ab.x + (mouse.y - a.y) * ab.y) / lengthSq;
        t = Clamp(t, 0.0f, 1.0f);
        const float dx = mouse.x - (a.x + ab.x * t);
        const float dy = mouse.y - (a.y + ab.y * t);
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < bestDistance) {
            bestDistance = dist;
            hoveredAxis = i;
        }
    }

    if (!activeDrag && hoveredAxis >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_gizmoAxis = hoveredAxis;
        m_gizmoStartMouseX = mouse.x;
        m_gizmoStartMouseY = mouse.y;
        m_gizmoStartPosition = m_selected->Position();
        m_gizmoStartScale = m_selected->Scale();
        const ImVec2 ab(axisEnds[hoveredAxis].x - screenOrigin.x, axisEnds[hoveredAxis].y - screenOrigin.y);
        const float  length = std::sqrt(ab.x * ab.x + ab.y * ab.y);
        if (length > 2.0f) {
            m_gizmoAxisScreenX = ab.x / length;
            m_gizmoAxisScreenY = ab.y / length;
            m_gizmoWorldPerPixel = axisLength / length;
        } else {
            m_gizmoAxis = -1;
        }
    }

    if (activeDrag) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const float along = (mouse.x - m_gizmoStartMouseX) * m_gizmoAxisScreenX +
                                (mouse.y - m_gizmoStartMouseY) * m_gizmoAxisScreenY;
            if (m_gizmoMode == 2) {
                const float factor = Clamp(1.0f + along * 0.01f, 0.05f, 10.0f);
                Vec3        scale = m_gizmoStartScale;
                if (m_gizmoAxis == 0) scale.x *= factor;
                if (m_gizmoAxis == 1) scale.y *= factor;
                if (m_gizmoAxis == 2) scale.z *= factor;
                m_selected->SetScale(scale);
            } else {
                const Vec3 position = m_gizmoStartPosition + axes[m_gizmoAxis] * (along * m_gizmoWorldPerPixel);
                m_selected->SetPosition(position);
            }
        } else {
            m_gizmoAxis = -1;
        }
    }

    if (activeDrag) hoveredAxis = m_gizmoAxis;
    for (int i = 0; i < 3; ++i) {
        if (!axisValid[i]) continue;
        const bool active = (i == hoveredAxis);
        draw->AddLine(screenOrigin, axisEnds[i], colors[i], active ? 4.0f : 2.5f);
        if (m_gizmoMode == 2) {
            draw->AddRectFilled(ImVec2(axisEnds[i].x - 5, axisEnds[i].y - 5),
                                ImVec2(axisEnds[i].x + 5, axisEnds[i].y + 5), colors[i]);
        } else {
            draw->AddCircleFilled(axisEnds[i], active ? 6.0f : 4.5f, colors[i]);
        }
    }
    draw->AddCircleFilled(screenOrigin, 3.5f, IM_COL32(240, 240, 240, 220));
}

void Editor::FocusSelected(World& world, Camera& editorCamera) {
    if (!m_selected) return;
    AABB bounds;
    if (!world.GetObjectBounds(m_selected, bounds)) {
        bounds = AABB::FromCenterExtents(m_selected->WorldPosition(), Vec3(1.0f));
    }
    const Vec3  center = bounds.Center();
    const float radius = (std::max)(Length(bounds.Extents()), 0.5f);
    if (editorCamera.orthographic) {
        editorCamera.orthoHeight = (std::max)(radius * 2.6f, 1.0f);
        const float distance = (std::max)(Length(editorCamera.position - center), 10.0f);
        editorCamera.position = center - editorCamera.Forward() * distance;
    } else {
        const float distance = radius / std::tan(editorCamera.fovY * 0.5f) * 1.7f;
        editorCamera.position = center - editorCamera.Forward() * distance;
    }
}
void Editor::PickObject(World& world, Camera& camera, const ImVec2& viewportPos, const ImVec2& viewportSize) {
    if (viewportSize.x <= 0 || viewportSize.y <= 0) return;
    const Ray ray = camera.ScreenToRay(Vec2(viewportPos.x, viewportPos.y), Vec2(viewportSize.x, viewportSize.y));
    World::RaycastHit hit;
    if (world.Raycast(ray, 500.0f, hit)) {
        Select(hit.object);
    } else {
        Select(nullptr);
    }
}

void Editor::ImportModel(World& world, AssetManager& assets, Camera& editorCamera) {
    std::string path;
    if (!OpenFileDialog("导入模型",
                        "OBJ 模型\0*.obj\0所有文件\0*.*\0",
                        path)) {
        return;
    }
    std::string error;
    Mesh* mesh = assets.ImportMesh(path, &error);
    if (!mesh) {
        SetStatus("导入失败: " + error, true);
        return;
    }
    assets.Refresh();
    // 导入后直接放到编辑器相机前方
    const Vec3 spawn = editorCamera.position + editorCamera.Forward() * 6.0f;
    GameObject* object = world.CreateGameObject(FileSystem::FileNameOf(path));
    object->SetPosition(spawn);
    auto* renderer = object->AddComponent<MeshRenderer>();
    renderer->mesh = "models/" + FileSystem::FileNameOf(path);
    Select(object);
    SetStatus("已导入模型: " + FileSystem::FileNameOf(path));
}

void Editor::ImportTexture(AssetManager& assets) {
    std::string path;
    if (!OpenFileDialog("导入贴图", "图片\0*.png;*.jpg;*.jpeg;*.bmp\0所有文件\0*.*\0", path)) return;
    std::string error;
    if (!assets.ImportTexture(path, &error)) {
        SetStatus("导入贴图失败: " + error, true);
        return;
    }
    assets.Refresh();
    if (m_selected) {
        if (auto* renderer = m_selected->GetComponent<MeshRenderer>()) {
            renderer->texture = "textures/" + FileSystem::FileNameOf(path);
            SetStatus("贴图已赋给选中对象");
            return;
        }
    }
    SetStatus("贴图导入成功: " + FileSystem::FileNameOf(path));
}

void Editor::SaveScene(World& world, bool saveAs) {
    std::string path = m_scenePath;
    if (saveAs || path.empty()) {
        if (!SaveFileDialog("保存场景", "ErMao 场景\0*.json\0所有文件\0*.*\0", "json", path)) return;
    }
    if (world.SaveToFile(path)) {
        m_scenePath = path;
        SetStatus("场景已保存: " + path);
    } else {
        SetStatus("保存失败: " + path, true);
    }
}

void Editor::LoadScene(World& world) {
    std::string path;
    if (!OpenFileDialog("打开场景", "ErMao 场景\0*.json\0所有文件\0*.*\0", path)) return;
    if (world.LoadFromFile(path)) {
        m_scenePath = path;
        Select(nullptr);
        SetStatus("场景已加载: " + path);
        if (m_sceneLoaded) m_sceneLoaded(world.LevelKind());
    } else {
        SetStatus("加载失败: " + path, true);
    }
}

void Editor::NewScene(World& world) {
    Select(nullptr); // world.Clear 会释放所有对象，先断开选中项
    m_scenePath.clear();
    world.Clear();
    GameObject* camera = world.CreateGameObject("主相机");
    camera->SetPosition(Vec3(0, 4, 12));
    camera->SetRotationEulerDegrees(Vec3(-15, 0, 0));
    auto* cameraComponent = camera->AddComponent<CameraComponent>();
    cameraComponent->primary = true;

    GameObject* sun = world.CreateGameObject("太阳");
    sun->SetRotationEulerDegrees(Vec3(-40, -30, 0));
    sun->AddComponent<DirectionalLightComponent>();

    GameObject* ground = world.CreateGameObject("地面");
    ground->SetScale(Vec3(60, 1, 60));
    auto* renderer = ground->AddComponent<MeshRenderer>();
    renderer->mesh = "plane";
    renderer->texture = "grid";
    renderer->uvScale = 30.0f;

    world.RebuildNavigation();
    Select(nullptr);
    SetStatus("已新建空场景");
    if (m_sceneLoaded) m_sceneLoaded(std::string());
}

void Editor::HandleShortcuts(World& world, Camera& editorCamera) {
    (void)editorCamera;
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) SaveScene(world, false);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) LoadScene(world);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N)) NewScene(world);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) DuplicateSelected(world);
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) DeleteSelected(world);
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F) && m_selected) {
        FocusSelected(world, editorCamera);
        SetStatus("聚焦: " + m_selected->Name());
    }

    if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
        if (m_playing) {
            ExitPlayMode(world);
        } else {
            EnterPlayMode(world);
        }
    }
}

} // namespace erm
