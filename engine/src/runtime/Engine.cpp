// ErMao Engine - 引擎主程序实现
#include "runtime/Engine.h"
#include "core/FileSystem.h"
#include "core/JobSystem.h"
#include "core/Log.h"
#include "core/Timer.h"
#include "game/Levels.h"
#include "game/ThirdPersonController.h"
#include "physics/PhysicsWorld.h"
#include "platform/Input.h"
#include "rhi/Screenshot.h"
#include "scene/Components.h"

#include <windows.h>

namespace erm {

namespace {
constexpr float kFixedDt = 1.0f / 60.0f;
constexpr int   kMaxSubSteps = 5;
}

int Engine::Run(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--screenshot" && i + 1 < argc) {
            m_screenshotPath = argv[++i];
            if (m_autoFrames == 0) m_autoFrames = 90;
        } else if (arg == "--frames" && i + 1 < argc) {
            m_autoFrames = std::atoi(argv[++i]);
        } else if (arg == "--play") {
            m_startPlay = true;
        } else if (arg == "--noeditor") {
            m_showEditor = false;
        } else if (arg == "--level" && i + 1 < argc) {
            m_levelName = argv[++i];
        } else if (arg == "--test-serialize") {
            m_testSerialize = true;
        } else if (arg == "--import" && i + 1 < argc) {
            m_importPath = argv[++i];
        } else if (arg == "--smoke") {
            m_smoke = true;
            m_startPlay = true;
        }
    }

    if (!Init()) {
        Shutdown();
        return 1;
    }
    MainLoop();
    Shutdown();
    return 0;
}

bool Engine::Init() {
    Log::Init("ErMao.log");
    LogInfo("ErMao Engine 启动（v0.2 / Dear ImGui）");
    Timer::Init();
    Input::BeginFrame();
    JobSystem::Init();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (!m_window.Create("ErMao Engine 0.2 — GAMES104 教学引擎", 1600, 900)) return false;

    // 输入：窗口回调负责产生边沿，Application 只做一次鼠标状态快照
    auto& events = m_window.Events();
    events.onKey = [](int vk, bool down, bool repeat) { Input::OnKey(vk, down, repeat); };
    events.onMouseButton = [](int button, bool down) { Input::OnMouseButton(button, down); };

    if (!m_device.Init(&m_window)) return false;
    if (!m_renderer.Init(&m_device)) return false;
    if (!m_assets.Init(m_device.GetDevice())) return false;

    m_world.Init(&m_assets);

    // 资源导入自检：把外部 OBJ 复制进 assets/models 再加载
    if (!m_importPath.empty()) {
        std::string error;
        Mesh* mesh = m_assets.ImportMesh(m_importPath, &error);
        if (mesh) {
            LogInfo("[自检] 导入成功：%s（%u 顶点 / %u 三角形）", m_importPath.c_str(), mesh->CPU().VertexCount(),
                    mesh->IndexCount() / 3);
        } else {
            LogError("[自检] 导入失败：%s", error.c_str());
        }
    }
    if (m_levelName == "demo") {
        game::BuildDemoScene(m_world);
    } else {
        game::BuildThirdPersonLevel(m_world);
    }

    // 场景文件统一放在 exe 旁的 assets/scenes（与编辑器、资源浏览器一致）
    m_scenePath = FileSystem::Join(FileSystem::Join(FileSystem::WritableAssetDir(), "scenes"),
                                   m_levelName + ".ermao.json");

    if (m_testSerialize) {
        const std::string text = m_world.ToJson().Dump();
        const std::string original = text;
        const size_t before = m_world.ObjectCount();
        m_world.Clear();
        std::string error;
        JsonValue root = JsonValue::Parse(text, &error);
        if (!error.empty()) {
            LogError("[自检] JSON 解析失败: %s", error.c_str());
        } else {
            m_world.FromJson(root);
            const std::string roundTrip = m_world.ToJson().Dump();
            LogInfo("[自检] 序列化往返: %zu -> %zu 个对象，字节相等=%s", before, m_world.ObjectCount(),
                    original == roundTrip ? "是" : "否");
            m_world.SaveToFile(m_scenePath);
        }
    }
    // 代码优先：启动时总是用 C++ 构建关卡，存档只通过编辑器的"加载"显式读取。
    // 这样改代码立刻生效，不会被旧的 .ermao.json 覆盖。

    m_editorCamera.position = Vec3(10.0f, 7.0f, 16.0f);
    m_editorCamera.yaw = 0.55f;
    m_editorCamera.pitch = -0.32f;
    m_editorCamera.aspect = static_cast<float>(m_window.Width()) / static_cast<float>(m_window.Height());

    if (!m_imgui.Init(m_window, m_device.GetDevice(), m_device.GetContext())) return false;
    m_editor.Init(&m_imgui);
    if (m_startPlay) m_editor.Play();
    m_player = m_world.FindByName("玩家");
    if (m_smoke) {
        LogInfo("[冒烟] 玩家对象: %s", m_player ? m_player->Name().c_str() : "<未找到>");
    }

    events.onRawMessage = [this](void* hwnd, unsigned int msg, unsigned long long wparam, long long lparam) {
        return m_imgui.WndProc(hwnd, msg, wparam, lparam);
    };

    LogInfo("初始化完成：%zu 个对象，JobSystem %u 线程", m_world.ObjectCount(), JobSystem::ThreadCount());
    return true;
}

void Engine::Shutdown() {
    m_editor = Editor{};
    m_imgui.Shutdown();
    m_world.Shutdown();
    m_assets.Shutdown();
    m_renderer.Shutdown();
    m_device.Shutdown();
    m_window.Destroy();
    JobSystem::Shutdown();
    CoUninitialize();
    LogInfo("ErMao Engine 退出");
    Log::Shutdown();
}

void Engine::UpdateGameplay(float dt) {
    const bool playing = m_showEditor ? m_editor.IsPlaying() : true;
    const bool paused = m_showEditor ? m_editor.IsPaused() : false;
    if (!playing || paused) return;

    m_accumulator += dt;
    int steps = 0;
    while (m_accumulator >= kFixedDt && steps < kMaxSubSteps) {
        m_world.FixedUpdate(kFixedDt);
        m_accumulator -= kFixedDt;
        ++steps;
    }
    // 防死亡螺旋：丢弃积压时间（审核 P1）
    if (steps == kMaxSubSteps) m_accumulator = 0.0f;

    m_world.Update(dt);
}

void Engine::RenderViewportScene(const Camera& camera, u32 width, u32 height) {
    if (width == 0 || height == 0) return;

    Camera viewCamera = camera;
    viewCamera.aspect = static_cast<float>(width) / static_cast<float>(height);

    m_world.BuildRenderScene(m_scene, viewCamera);
    m_scene.shadowFocus = viewCamera.position + viewCamera.Forward() * 12.0f;

    m_renderer.Debug().Begin();
    m_renderer.RenderToViewport(viewCamera, m_scene, m_time, width, height, m_renderer.Wireframe());
}

void Engine::CaptureScreenshot(const std::string& path) {
    // 在 Present 之前读后备缓冲（审核 P2-1）
    SaveBackbufferToPng(m_device.GetDevice(), m_device.GetContext(), m_device.GetSwapChain(), path);
}

void Engine::MainLoop() {
    // 不自动写盘：编辑器里点"保存"才会生成场景文件

    while (m_running) {
        // 1) 帧首清边沿，再收消息（审核 P0-1：顺序不能反）
        Input::BeginFrame();
        if (!m_window.Poll()) break;
        Input::SetMouseState(m_window.MousePosition(), m_window.ConsumeMouseDelta(), m_window.ConsumeWheel());
        Timer::Tick();

        if (m_window.WasResized()) {
            m_device.Resize(m_window.Width(), m_window.Height());
            m_renderer.SetWindowSize(m_window.Width(), m_window.Height());
            m_editorCamera.SetViewport(m_window.Width(), m_window.Height());
            m_window.ClearResized();
        }
        if (m_window.Width() == 0 || m_window.Height() == 0) continue;

        const float dt = (std::min)(Timer::Delta(), 0.1f);
        m_time += dt;

        // 2) 玩法更新（编辑器模式下不 tick AI/物理）
        if (m_smoke && m_player) {
            // 脚本化输入：第 30~120 帧向前走，第 60 帧起跳
            if (m_frameIndex >= 30 && m_frameIndex < 120) {
                ThirdPersonController::SetSimulatedInput(Vec2(0.0f, 1.0f), m_frameIndex >= 60 && m_frameIndex < 66,
                                                         false);
            } else {
                ThirdPersonController::ClearSimulatedInput();
            }
            if (m_frameIndex == 30 || m_frameIndex == 60 || m_frameIndex == 120 || m_frameIndex == 180) {
                const Vec3 p = m_player->Position();
                LogInfo("[冒烟] 帧 %d 玩家位置 (%.2f, %.2f, %.2f)", m_frameIndex, p.x, p.y, p.z);
            }
        }
        UpdateGameplay(dt);

        // 3) 热键（现在边沿判定是可靠的）
        if (Input::IsKeyPressed(VK_F11)) m_showEditor = !m_showEditor;
        if (Input::IsKeyPressed(VK_F12)) {
            CaptureScreenshot("screenshot_" + std::to_string(m_frameIndex) + ".png");
        }
        if (Input::IsKeyPressed(VK_ESCAPE) && !m_showEditor) break;

        // 4) 决定本帧相机
        const bool playing = m_showEditor ? m_editor.IsPlaying() : m_startPlay;
        Camera viewCamera = m_editorCamera;
        if (playing) {
            if (Camera* gameCamera = m_world.GetActiveCamera()) {
                viewCamera = *gameCamera;
            }
        }
        ThirdPersonController::SetInputEnabled(playing && (!m_showEditor || m_editor.ViewportHovered()));

        // 5) UI + 视口渲染
        if (m_showEditor) {
            m_imgui.BeginFrame();
            m_editor.Draw(m_world, m_renderer, m_assets, m_editorCamera,
                          [this](const Camera& camera, u32 width, u32 height) {
                              RenderViewportScene(camera, width, height);
                          },
                          dt);
            if (m_editor.WantsExit()) {
                m_running = false;
                m_editor.ClearExitRequest();
            }

            float clear[4] = {0.055f, 0.06f, 0.07f, 1.0f};
            m_device.BeginFrame(clear);
            m_imgui.EndFrame();
        } else {
            m_renderer.Debug().Begin();
            m_world.BuildRenderScene(m_scene, viewCamera);
            m_renderer.Render(viewCamera, m_scene, m_time, m_renderer.Wireframe());
        }

        // 6) 自动截图 + 退出
        ++m_frameIndex;
        if (m_autoFrames > 0 && m_frameIndex >= m_autoFrames) {
            if (!m_screenshotPath.empty()) CaptureScreenshot(m_screenshotPath);
            const auto& stats = m_renderer.GetStats();
            LogInfo("自动截图完成：第 %d 帧 | 绘制 %u | 实例 %u | 三角 %u | GPU %.2fms", m_frameIndex,
                    stats.drawCalls, stats.instances, stats.triangles, stats.gpuFrameMs);
            break;
        }

        m_device.EndFrame(m_vsync);
    }
}

} // namespace erm
