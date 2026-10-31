// ErMao Engine - 引擎主程序实现
#include "runtime/Engine.h"
#include "core/FileSystem.h"
#include "core/JobSystem.h"
#include "core/Log.h"
#include "core/Timer.h"
#include "game/Levels.h"
#include "game/ParticleEmitter.h"
#include "game/SkinningDemo.h"
#include "game/ThirdPersonController.h"
#include "physics/PhysicsWorld.h"
#include "rhi/VulkanBackend.h"
#include "platform/Audio.h"
#include "platform/Input.h"
#include "rhi/Screenshot.h"
#include "core/FileSystem.h"
#include "runtime/Packager.h"
#include "scene/Components.h"

#include <windows.h>

#include <cstdlib>
#include <imgui.h>
#include <sstream>

namespace erm {

namespace {
constexpr float kFixedDt = 1.0f / 60.0f;
constexpr int   kMaxSubSteps = 5;
}

int Engine::Run(int argc, char** argv) {
    // 游戏包：exe 旁的 game.cfg 决定默认关卡与模式（命令行参数优先级更高）
    const std::string gameConfigPath = FileSystem::Join(FileSystem::ExecutableDir(), "game.cfg");
    if (FileSystem::Exists(gameConfigPath)) {
        std::string text;
        if (FileSystem::ReadTextFile(gameConfigPath, text)) {
            std::istringstream stream(text);
            std::string        line;
            while (std::getline(stream, line)) {
                const size_t eq = line.find('=');
                if (eq == std::string::npos) continue;
                const std::string key = line.substr(0, eq);
                std::string value = line.substr(eq + 1);
                if (!value.empty() && value.back() == '\r') value.pop_back();
                if (key == "level") m_levelName = value;
                else if (key == "editor") m_showEditor = (value != "0");
                else if (key == "play") m_startPlay = (value != "0");
            }
            LogInfo("游戏配置: level=%s editor=%d play=%d", m_levelName.c_str(), m_showEditor ? 1 : 0,
                    m_startPlay ? 1 : 0);
        }
    }

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
        } else if (arg == "--package-game" && i + 1 < argc) {
            m_packageGame = argv[++i];
        } else if (arg == "--package" && i + 1 < argc) {
            m_packageTarget = argv[++i];
        } else if (arg == "--import" && i + 1 < argc) {
            m_importPath = argv[++i];
        } else if (arg == "--vulkan-test") {
            m_vulkanTest = true;
        } else if (arg == "--audio-test") {
            m_audioTest = true;
        } else if (arg == "--demo3d") {
            m_levelName = "survivors3d";
            m_showEditor = false;
            m_startPlay = true;
            m_demoMode = true;
        } else if (arg == "--demo2d") {
            // 一键 2D 演示：幸存者关卡 + 自动开局 + 无编辑器
            m_levelName = "survivors";
            m_showEditor = false;
            m_startPlay = true;
            m_demoMode = true;
        } else if (arg == "--demo") {
            // 一键演示：竞技场 + 自动开火 + 无编辑器
            m_levelName = "arena";
            m_showEditor = false;
            m_startPlay = true;
            m_demoMode = true;
        } else if (arg == "--no-gpu-cull") {
            m_noGpuCull = true;
        } else if (arg == "--noshadows") {
            m_noShadows = true;
        } else if (arg == "--debugview" && i + 1 < argc) {
            m_debugView = std::atoi(argv[++i]);
        } else if (arg == "--roughness" && i + 1 < argc) {
            m_overrideRoughness = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--metallic" && i + 1 < argc) {
            m_overrideMetallic = static_cast<float>(std::atof(argv[++i]));
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
    if (m_vulkanTest) {
        VulkanDemo demo;
        if (demo.Init(m_window)) {
            demo.Run(120, "docs/screenshots/vulkan.png");
        } else {
            LogError("[Vulkan] 初始化失败");
        }
        demo.Shutdown();
        m_window.Destroy();
        std::exit(0);
    }

    // 输入：窗口回调负责产生边沿，Application 只做一次鼠标状态快照
    auto& events = m_window.Events();
    events.onKey = [](int vk, bool down, bool repeat) { Input::OnKey(vk, down, repeat); };
    events.onMouseButton = [](int button, bool down) { Input::OnMouseButton(button, down); };

    if (!m_device.Init(&m_window)) return false;
    AudioSystem::Get().Init();
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
    } else if (m_levelName == "thirdperson") {
        game::BuildThirdPersonLevel(m_world);
    } else if (m_levelName == "cull") {
        game::BuildCullTestLevel(m_world);
    } else if (m_levelName == "survivors3d") {
        BuildSurvivors3DLevel(m_world, m_assets);
        m_survivors3dLevel = true;
    } else if (m_levelName == "survivors") {
        BuildSurvivorsLevel(m_world, m_assets);
        m_survivorsLevel = true;
        m_is2D = true;
    } else if (m_levelName == "skinning") {
        game::BuildSkinningLevel(m_world, m_renderer);
    } else {
        m_waveLevel = true;
        game::BuildWaveArena(m_world);
    }

    // 场景文件统一放在 exe 旁的 assets/scenes（与编辑器、资源浏览器一致）
    m_scenePath = FileSystem::Join(FileSystem::Join(FileSystem::WritableAssetDir(), "scenes"),
                                   m_levelName + ".ermao.json");

    if (m_testSerialize) {
        // 场景往返自检：第一次加载可能因四元数归一化产生 1 ULP 漂移，
        // 因此断言"再次往返完全一致"（序列化幂等），并记录首次差异。
        const std::string original = m_world.ToJson().Dump();
        const size_t before = m_world.ObjectCount();

        auto reload = [this](const std::string& text) -> std::string {
            m_world.Clear();
            std::string error;
            JsonValue root = JsonValue::Parse(text, &error);
            if (!error.empty()) return std::string();
            m_world.FromJson(root);
            return m_world.ToJson().Dump();
        };

        const std::string first = reload(original);
        const std::string second = first.empty() ? std::string() : reload(first);

        if (first.empty() || second.empty()) {
            LogError("[自检] 序列化往返失败：JSON 解析错误");
        } else {
            const bool stable = (first == second);
            const bool identical = (original == first);
            LogInfo("[自检] 序列化往返: %zu -> %zu 个对象，再次往返稳定=%s，首次字节一致=%s", before,
                    m_world.ObjectCount(), stable ? "是" : "否", identical ? "是" : "否");
            if (!identical) {
                size_t diff = 0;
                while (diff < original.size() && diff < first.size() && original[diff] == first[diff]) ++diff;
                const size_t from = diff > 70 ? diff - 70 : 0;
                std::string a = original.substr(from, 120);
                std::string b = first.substr(from, 120);
                for (char& c : a) if (c == '\n' || c == '\r') c = ' ';
                for (char& c : b) if (c == '\n' || c == '\r') c = ' ';
                LogInfo("[自检] 首次漂移 @%zu | 原: %s | 新: %s", diff, a.c_str(), b.c_str());
            }
            m_world.SaveToFile(m_scenePath);
        }
    }
    // 代码优先：启动时总是用 C++ 构建关卡，存档只通过编辑器的"加载"显式读取。
    // 这样改代码立刻生效，不会被旧的 .ermao.json 覆盖。

    m_editorCamera.position = Vec3(10.0f, 7.0f, 16.0f);
    m_editorCamera.yaw = 0.55f;
    m_editorCamera.pitch = -0.32f;
    m_editorCamera.aspect = static_cast<float>(m_window.Width()) / static_cast<float>(m_window.Height());

    const bool autoRun = (m_autoFrames > 0) || !m_screenshotPath.empty();
    if (!m_imgui.Init(m_window, m_device.GetDevice(), m_device.GetContext(), !autoRun)) return false;
    if (autoRun) m_editor.ForceDefaultLayout();
    m_editor.Init(&m_imgui);
    if (m_startPlay) m_editor.Play();
    if (m_waveLevel && !m_showEditor) m_waveGame.Reset(m_world);
    if (m_survivorsLevel && (m_demoMode || m_smoke)) m_survivors.Reset(m_world);
    if (m_survivors3dLevel && (m_demoMode || m_smoke)) m_survivors3d.Reset(m_world);
    if (m_demoMode) CombatComponent::SetAutoFire(true);
    if (m_smoke) {
        GameObject* player = m_world.FindByName("玩家");
        LogInfo("[冒烟] 玩家对象: %s", player ? player->Name().c_str() : "<未找到>");
    }

    events.onRawMessage = [this](void* hwnd, unsigned int msg, unsigned long long wparam, long long lparam) {
        return m_imgui.WndProc(hwnd, msg, wparam, lparam);
    };


    if (!m_packageGame.empty()) {
        const bool linuxPackage = (m_packageGame == "linux");
        const PackageResult result2 = linuxPackage ? Packager::PackageGameLinuxSource("ErMaoSurvivors", "survivors")
                                                   : Packager::PackageGameWindows("ErMaoSurvivors", "survivors");
        const PackageResult result3 = linuxPackage ? Packager::PackageGameLinuxSource("ErMaoSurvivors3D", "survivors3d")
                                                   : Packager::PackageGameWindows("ErMaoSurvivors3D", "survivors3d");
        LogInfo("[打包自检] 2D 游戏包 success=%d %s", result2.success ? 1 : 0, result2.message.c_str());
        LogInfo("[打包自检] 3D 游戏包 success=%d %s", result3.success ? 1 : 0, result3.message.c_str());
        m_running = false;
    }
    if (!m_packageTarget.empty()) {
        // --package windows|linux：无界面打包自检
        const PackageResult result = (m_packageTarget == "linux") ? Packager::PackageLinuxSource()
                                                                  : Packager::PackageWindows();
        LogInfo("[打包自检] success=%d %s", result.success ? 1 : 0, result.message.c_str());
        m_running = false;
    }
    // 教学截图开关：阴影/调试视图/材质覆盖
    if (m_noShadows) m_renderer.ShadowsEnabled() = false;
    if (m_noGpuCull) m_renderer.SetGpuCullEnabled(false);
    if (m_debugView >= 0) m_renderer.DebugView() = m_debugView;
    if (m_overrideRoughness >= 0.0f || m_overrideMetallic >= 0.0f) {
        for (GameObject* root : m_world.RootObjects()) {
            root->ForEach([this](GameObject* object) {
                auto* mr = object->GetComponent<MeshRenderer>();
                if (!mr) return;
                if (m_overrideRoughness >= 0.0f) mr->roughness = m_overrideRoughness;
                if (m_overrideMetallic >= 0.0f) mr->metallic = m_overrideMetallic;
            });
        }
    }
    LogInfo("初始化完成：%zu 个对象，JobSystem %u 线程", m_world.ObjectCount(), JobSystem::ThreadCount());
    return true;
}

void Engine::Shutdown() {
    AudioSystem::Get().Shutdown();
    m_editor.Shutdown();
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

            if (m_survivors3dLevel) {
                // 3D 幸存者：第三人称俯视跟随
                if (GameObject* player = m_world.FindByName("玩家")) {
                    const Vec3 p = player->Position();
                    viewCamera.position = p + Vec3(0.0f, 6.4f, 8.8f);
                    const Vec3 dir = Normalize((p + Vec3(0.0f, 1.0f, 0.0f)) - viewCamera.position);
                    viewCamera.yaw = std::atan2(-dir.x, -dir.z);
                    viewCamera.pitch = std::asin(Clamp(dir.y, -1.0f, 1.0f));
                }
            }
    if (m_survivors3dLevel) {
        if (GameObject* player = m_world.FindByName("玩家")) {
            const Vec3 p = player->Position();
            viewCamera.position = p + Vec3(0.0f, 6.4f, 8.8f);
            const Vec3 dir = Normalize((p + Vec3(0.0f, 1.0f, 0.0f)) - viewCamera.position);
            viewCamera.yaw = std::atan2(-dir.x, -dir.z);
            viewCamera.pitch = std::asin(Clamp(dir.y, -1.0f, 1.0f));
        }
    }
    m_world.BuildRenderScene(m_scene, viewCamera); // shadowFocus 由 World 统一决定（审核 P3-42）
    if (m_is2D) {
        // 2D：正交相机跟随玩家（超出关卡边界时夹住）
        Camera2D camera2D;
        camera2D.halfHeight = 6.4f;
        if (GameObject* player = m_world.FindByName("玩家")) {
            const Vec3 position = player->Position();
            camera2D.position = Vec2(Clamp(position.x, -20.0f, 20.0f), Clamp(position.y, -8.0f, 8.0f));
        }
        m_scene.is2D = true;
        m_scene.camera2D = camera2D;
    }
    game::CollectSkinnedDraws(m_world, m_scene);
    CollectParticles(m_world, m_scene);


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
        AudioSystem::Get().Update(Timer::Delta());
        if (m_audioTest && m_frameIndex % 18 == 0) {
            static const char* kTestSounds[] = {"click", "shoot", "hit", "pickup", "levelup", "death", "magic", "swing"};
            static int testIndex = 0;
            if (testIndex < 8) {
                PlaySfx(kTestSounds[testIndex], 0.7f);
                LogInfo("[音频] 播放 %s（活跃 voice %u）", kTestSounds[testIndex], AudioSystem::Get().ActiveVoices());
                ++testIndex;
            }
        }

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
        GameObject* smokePlayer = m_smoke ? m_world.FindByName("玩家") : nullptr; // 每次重查，避免 PIE 重建后悬垂
        if (smokePlayer) {
            // 脚本化输入：第 30~120 帧向前走，第 60 帧起跳
            if (m_frameIndex >= 30 && m_frameIndex < 120) {
                ThirdPersonController::SetSimulatedInput(Vec2(0.0f, 1.0f), m_frameIndex >= 60 && m_frameIndex < 66,
                                                         false);
            } else {
                ThirdPersonController::ClearSimulatedInput();
            }
            if (m_frameIndex == 30 || m_frameIndex == 60 || m_frameIndex == 120 || m_frameIndex == 180) {
                const Vec3 p = smokePlayer->Position();
                LogInfo("[冒烟] 帧 %d 玩家位置 (%.2f, %.2f, %.2f)", m_frameIndex, p.x, p.y, p.z);
            }
        }
        UpdateGameplay(dt);

        // 3) 热键：文本输入时不要抢键盘（审核 P2-17/P2-19）
        const bool wantKeyboard = m_showEditor && ImGui::GetIO().WantCaptureKeyboard;
        if (!wantKeyboard) {
            if (Input::IsKeyPressed(VK_F11)) m_showEditor = !m_showEditor;
            if (Input::IsKeyPressed(VK_F12)) {
                CaptureScreenshot("screenshot_" + std::to_string(m_frameIndex) + ".png");
            }
            if (Input::IsKeyPressed(VK_ESCAPE) && !m_showEditor) break;
        }

        // 4) 决定本帧相机
        const bool playing = m_showEditor ? m_editor.IsPlaying() : m_startPlay;
        Camera viewCamera = m_editorCamera;
        if (playing) {
            if (Camera* gameCamera = m_world.GetActiveCamera()) {
                viewCamera = *gameCamera;
            }
        }
        bool gameplayInput = playing && (!m_showEditor || m_editor.ViewportHovered());
        if (m_waveLevel) {
            if (playing) m_waveGame.Update(m_world, dt, gameplayInput);
            if (m_waveGame.GetState() != WaveGame::State::Playing) gameplayInput = false;
            if (!playing) m_waveGame.ReturnToMenu();
        }
        if (m_survivors3dLevel) {
            if (playing) m_survivors3d.Update(m_world, dt);
            if (m_survivors3d.GetState() != Survivors3DDirector::State::Playing) gameplayInput = false;
            if (!playing) m_survivors3d.ReturnToMenu();
            Survivors3DPlayerComponent::SetInputEnabled(gameplayInput);
        }
        if (m_survivorsLevel) {
            if (playing) m_survivors.Update(m_world, dt);
            if (m_survivors.GetState() != SurvivorsDirector::State::Playing) gameplayInput = false;
            if (!playing) m_survivors.ReturnToMenu();
            SurvivorsPlayerComponent::SetInputEnabled(gameplayInput);
        }
        ThirdPersonController::SetInputEnabled(gameplayInput);

        // 5) UI + 视口渲染
        // 5) UI + 视口渲染
        Camera* barCamera = &viewCamera;
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
            if (m_waveLevel && playing) {
                m_waveGame.DrawUI(m_world, m_editor.ViewportMinX(), m_editor.ViewportMinY(),
                                  static_cast<float>(m_editor.ViewportWidthPx()),
                                  static_cast<float>(m_editor.ViewportHeightPx()), m_editor.ViewportHovered());
                // 3D UI：头顶血条（世界 → 屏幕投影）
                m_waveGame.DrawHealthBars(m_world, viewCamera, m_editor.ViewportMinX(), m_editor.ViewportMinY(),
                                          static_cast<float>(m_editor.ViewportWidthPx()),
                                          static_cast<float>(m_editor.ViewportHeightPx()));
                if (m_waveGame.WantsQuit()) m_running = false;
            }
            if (m_survivorsLevel && playing) {
                m_survivors.DrawUI(m_world, m_editor.ViewportMinX(), m_editor.ViewportMinY(),
                                   static_cast<float>(m_editor.ViewportWidthPx()),
                                   static_cast<float>(m_editor.ViewportHeightPx()));
                if (m_survivors.WantsQuit()) m_running = false;
                m_imgui.EndFrame();
            }
            if (m_survivors3dLevel && playing) {
                m_survivors3d.DrawUI(m_world, m_editor.ViewportMinX(), m_editor.ViewportMinY(),
                                     static_cast<float>(m_editor.ViewportWidthPx()),
                                     static_cast<float>(m_editor.ViewportHeightPx()));
                if (m_survivors3d.WantsQuit()) m_running = false;
            }

            float clear[4] = {0.055f, 0.06f, 0.07f, 1.0f};
            m_device.BeginFrame(clear);
            m_imgui.EndFrame();
        } else {
            m_renderer.Debug().Begin();
            if (m_survivors3dLevel) {
                // 3D 幸存者：第三人称俯视跟随
                if (GameObject* player = m_world.FindByName("玩家")) {
                    const Vec3 p = player->Position();
                    viewCamera.position = p + Vec3(0.0f, 6.4f, 8.8f);
                    const Vec3 dir = Normalize((p + Vec3(0.0f, 1.0f, 0.0f)) - viewCamera.position);
                    viewCamera.yaw = std::atan2(-dir.x, -dir.z);
                    viewCamera.pitch = std::asin(Clamp(dir.y, -1.0f, 1.0f));
                }
            }
            m_world.BuildRenderScene(m_scene, viewCamera);
    game::CollectSkinnedDraws(m_world, m_scene);
    CollectParticles(m_world, m_scene);
            if (m_is2D) {
                Camera2D camera2D;
                camera2D.halfHeight = 6.4f;
                if (GameObject* player = m_world.FindByName("玩家")) {
                    const Vec3 position = player->Position();
                    camera2D.position = Vec2(Clamp(position.x, -20.0f, 20.0f), Clamp(position.y, -8.0f, 8.0f));
                }
                m_scene.is2D = true;
                m_scene.camera2D = camera2D;
            }
            m_renderer.Render(viewCamera, m_scene, m_time, m_renderer.Wireframe());

            // 无编辑器时也要有 HUD/血条：UI 直接叠加在已渲染的后备缓冲上
            if (m_waveLevel && playing) {
                m_imgui.BeginFrame();
                m_waveGame.DrawUI(m_world, 0.0f, 0.0f, static_cast<float>(m_window.Width()),
                                  static_cast<float>(m_window.Height()), true);
                m_waveGame.DrawHealthBars(m_world, *barCamera, 0.0f, 0.0f, static_cast<float>(m_window.Width()),
                                          static_cast<float>(m_window.Height()));
                if (m_waveGame.WantsQuit()) m_running = false;
                m_imgui.EndFrame();
            }
            if (m_survivorsLevel && playing) {
                m_imgui.BeginFrame();
                m_survivors.DrawUI(m_world, 0.0f, 0.0f, static_cast<float>(m_window.Width()),
                                   static_cast<float>(m_window.Height()));
                if (m_survivors.WantsQuit()) m_running = false;
                m_imgui.EndFrame();
            }
            if (m_survivors3dLevel && playing) {
                m_imgui.BeginFrame();
                m_survivors3d.DrawUI(m_world, 0.0f, 0.0f, static_cast<float>(m_window.Width()),
                                     static_cast<float>(m_window.Height()));
                if (m_survivors3d.WantsQuit()) m_running = false;
                m_imgui.EndFrame();
            }
        }

        // 6) 自动截图 + 退出
        ++m_frameIndex;
        if (m_autoFrames > 0 && m_frameIndex >= m_autoFrames) {
            if (!m_screenshotPath.empty()) CaptureScreenshot(m_screenshotPath);
            const auto& stats = m_renderer.GetStats();
            LogInfo("自动截图完成：第 %d 帧 | 绘制 %u | 实例 %u | 三角 %u | GPU %.2fms | GPU可见 %u", m_frameIndex,
                    stats.drawCalls, stats.instances, stats.triangles, stats.gpuFrameMs,
                    stats.gpuVisibleInstances);
            break;
        }

        m_device.EndFrame(m_vsync);
    }
}

} // namespace erm
