// ErMao Engine - Web / 微信小游戏宿主（2D 幸存者）
#include "WebApp.h"

#include "core/FileSystem.h"
#include "core/Log.h"
#include "core/Timer.h"
#include "game/Survivors.h"
#include "platform/Audio.h"
#include "platform/Input.h"
#include "rhi/Texture.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include <SDL.h>
#include <GLES2/gl2.h>
#include <emscripten/emscripten.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace erm {

namespace {

constexpr float kFixedDt = 1.0f / 60.0f;
constexpr int   kMaxSubSteps = 5;

// 2D 幸存者 UI 用到的全部字符（从 ImGuiLayer.cpp 的同名字形表复制）。
// 字体子集 ErMaoUI.ttf 就是按这份表 + 源文件中的中文生成的。
const char* kSurvivorsUIGlyphs =
    "·×“”→、。一上下不与丛个中为主之乐了二人件休会伤住体使保倍倒值偏停入八关具兽内册再写冰冻准减出击刃分切到制刻前力加动化升半单卡即去发取可史各名后向吸命和回围图在地坏型士备复外大失好姆始子字存完定宝害家对寿射将少就局已并幸幽序度开弃强当录径得怪恢戏成战手打扩找技把择拾持挑损据提支攻放效敌散数整文斗新方旋无时明景暂最有木未本杀束来析柱标档检槽次每毒毛没法活测游满演潮火灰灵炸点爆片牢物猎率玩环现球瓶生用界的目矢知短石确碍示离秒称移程立等箭箱类精系索级纯经结绕绘统继绩绪续缩缺置者背能自色英范草莱获菜落蛛蜘血行角解计认设说读象败起距踏躲转载近返进连退选速道避部里重量鉴钉锁间附隔障雷雾霆霜面音页项额飞验骑骷髅高默！（），：；三且世业丢两久乎乘九也书于互些仅从以优伴但位低余例依侧信做像允元先光免全共其冒冲决冷几列则初别刷劈区十却历压原双反变叠口另只右号同否周响唯器团固圆场均坐垂域基塌墙多够太头奥如它实宫容宽导封小尝尾层屏展工左布带帧常幅应底廊廓式引张弹形影微心必态性息悬慢或截房所才投抖折抽拉拖拥指按挡挤捡掉排接携撞收断既旧易是显暖暗暴更朝期术机条板构果枝架染查栏样格桌桶模横橙正此步死比永沿注洗浮淡清源烟焰牌特状由电画留白直相看真着砖础硬碰磁磨秃稍穿窄窗端符第签算粒素纹线终给绿缓群翻而职致苔菱补表被裁裂装要见观视觉让许证试询调贴资走足跟跳轨轮较辑边达过还远迹透逐递通逻遍遮那都配醒采释针钮铺锅锚镜长闪际限除随需露青靠顶顺须颜飘饰馈";

ImFont* AddFont(ImGuiIO& io, const char* path, float size, const ImWchar* ranges) {
    if (!path || !*path) return nullptr;
    ImFontConfig config;
    config.OversampleH = 1;
    config.OversampleV = 1;
    config.PixelSnapH = true;
    return io.Fonts->AddFontFromFileTTF(path, size, &config, ranges);
}

} // namespace

WebApp::WebApp() = default;

WebApp::~WebApp() {
    delete m_survivors;
    m_survivors = nullptr;
}

bool WebApp::Init() {
    Log::Init("");
    Timer::Init();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        LogError("[Web] SDL_Init 失败: %s", SDL_GetError());
        return false;
    }

    // Emscripten 的 EGL 适配层对 PROFILE_ES / DEPTH_SIZE 很挑；WebGL 版本
    // 已由 -sMAX_WEBGL_VERSION=1 固定，这里只保留必要的双缓冲属性。
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    // 让 SDL 画布跟随浏览器/设备窗口，而不是固定 1280x720；
    // 这样桌面截图、手机横屏和微信小游戏屏幕适配共用同一条路径。
    int cssWidth = EM_ASM_INT({ return (typeof window !== 'undefined' && window.innerWidth) ? window.innerWidth : 1280; });
    int cssHeight = EM_ASM_INT({ return (typeof window !== 'undefined' && window.innerHeight) ? window.innerHeight : 720; });
    if (cssWidth < 320) cssWidth = 320;
    if (cssHeight < 240) cssHeight = 240;

    m_window = SDL_CreateWindow("ErMao Web", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, cssWidth, cssHeight,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                                    SDL_WINDOW_SHOWN);
    if (!m_window) {
        LogError("[Web] 创建窗口失败: %s", SDL_GetError());
        return false;
    }

    m_glContext = SDL_GL_CreateContext(m_window);
    if (!m_glContext) {
        LogError("[Web] 创建 GLES2 上下文失败: %s", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(m_window, m_glContext);
    // 不要在 main loop 建立前调用 SDL_GL_SetSwapInterval：Emscripten 的实现会立刻
    // 调 emscripten_set_main_loop_timing，此时会报 "Cannot set timing mode..."。
    // 主循环用 fps=0，Emscripten 会自动切到 requestAnimationFrame。

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ApplyStyle();
    if (!LoadFonts()) {
        LogWarn("[Web] 字体加载失败，回退 ImGui 默认字体");
        io.Fonts->AddFontDefault();
    }

    if (!ImGui_ImplSDL2_InitForOpenGL(m_window, m_glContext)) {
        LogError("[Web] ImGui SDL2 后端初始化失败");
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 100")) {
        LogError("[Web] ImGui OpenGL3 后端初始化失败");
        return false;
    }

    // 2D UI 会用平台回调切换采样器；OpenGL 后端没有该回调时补一个空实现，
    // 避免 ImDrawList::AddCallback(nullptr)。
    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    platform.DrawCallback_SetSamplerLinear = [](const ImDrawList*, const ImDrawCmd*) {};
    platform.DrawCallback_SetSamplerNearest = [](const ImDrawList*, const ImDrawCmd*) {};

    if (!m_renderer.Init()) {
        LogError("[Web] 2D 渲染器初始化失败");
        return false;
    }

    m_assets.Init();
    RegisterSurvivorsComponents();
    m_world.Init(&m_assets);
    BuildSurvivorsLevel(m_world, m_assets);

    m_survivors = new SurvivorsDirector();
    m_survivors->InitializeUI(m_assets, m_bodyFont, m_largeFont, m_displayFont);
    m_survivors->ReturnToMenu();

    m_camera.aspect = 16.0f / 9.0f;
    m_camera.position = Vec3(0.0f, 0.0f, 10.0f);

    AudioSystem::Get().Init();

    // URL 参数：?play=1 直接开局（截图/演示），?frames=N 渲染 N 帧后停住并标记 ready。
    // 微信小游戏没有 URL，也不保证 document/window 可写；先识别 wx 再决定是否解析 URL，
    // 避免在小游戏环境里执行 location/document.title 等浏览器专属操作。
    const bool isWeChat = EM_ASM_INT({ return (typeof wx !== 'undefined') ? 1 : 0; }) != 0;
    const char* search = nullptr;
    if (!isWeChat) {
        search = emscripten_run_script_string(
            "(typeof window !== 'undefined' && window.location && window.location.search) ? "
            "window.location.search : ''");
    }
    if (search && std::strstr(search, "play=1")) {
        m_survivors->Reset(m_world);
    }
    if (search) {
        const char* frames = std::strstr(search, "frames=");
        if (frames) m_prewarmFrames = std::atoi(frames + 7);
    }
    m_mobile = isWeChat || EM_ASM_INT({
        return (typeof window !== 'undefined' &&
                (('ontouchstart' in window) ||
                 (typeof navigator !== 'undefined' && navigator.maxTouchPoints > 0)))
                   ? 1
                   : 0;
    }) != 0;
    if (search && std::strstr(search, "mobile=1")) m_mobile = true;
    if (isWeChat) {
        // 小游戏从游戏画面直接进入；菜单/暂停仍由游戏内 UI 处理。
        m_survivors->Reset(m_world);
    }

    // 截图/自动化：在进入 requestAnimationFrame 前同步推进 N 个固定步。
    // headless Chrome 的 virtual-time 不会可靠地驱动 RAF，预先推进才能得到
    // 与桌面 --frames N 相同的游戏状态。
    if (m_prewarmFrames > 0) {
        const int steps = m_prewarmFrames;
        for (int i = 0; i < steps; ++i) {
            m_world.FixedUpdate(kFixedDt);
            m_world.Update(kFixedDt);
            if (m_survivors->GetState() == SurvivorsDirector::State::Playing) {
                m_survivors->Update(m_world, kFixedDt);
            }
            m_time += kFixedDt;
        }
        m_prewarmFrames = 0;
        EM_ASM({
            try {
                if (typeof document !== 'undefined') document.title = 'ermao-ready';
            } catch (e) {}
        });
        LogInfo("[Web] 预推进 %d 帧完成（游戏时间 %.2fs）", steps, m_time);
    }

    m_initialised = true;
    LogInfo("[Web] iOS GL 兼容模式：已跳过 ImGui 状态查询");
    LogInfo("[Web] 初始化完成（%s，触摸 %s）", "WebGL 1.0 / GLES2", m_mobile ? "是" : "否");
    return true;
}

void WebApp::Shutdown() {
    if (!m_initialised) return;
    m_initialised = false;

    AudioSystem::Get().Shutdown();
    if (m_survivors) {
        m_survivors->ReturnToMenu();
        delete m_survivors;
        m_survivors = nullptr;
    }
    m_world.Shutdown();
    m_assets.Shutdown();
    m_renderer.Shutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if (m_glContext) {
        SDL_GL_DeleteContext(m_glContext);
        m_glContext = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();
    Log::Shutdown();
}

void WebApp::Frame() {
    if (!m_running) return;

    Input::BeginFrame();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ProcessEvent(event);
    }
    if (!m_running) return;

    Input::SetMouseState(Vec2(static_cast<float>(m_mouseX), static_cast<float>(m_mouseY)),
                         Vec2(static_cast<float>(m_mouseDeltaX), static_cast<float>(m_mouseDeltaY)), m_wheel);
    m_mouseDeltaX = 0;
    m_mouseDeltaY = 0;
    m_wheel = 0.0f;

    Timer::Tick();
    const float dt = (std::min)(Timer::Delta(), 0.1f);
    m_time += dt;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    Update(dt);
    Render();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(m_window);

}

void WebApp::ProcessEvent(SDL_Event& event) {
    ImGui_ImplSDL2_ProcessEvent(&event);

    switch (event.type) {
        case SDL_QUIT:
            Quit();
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            int key = 0;
            switch (event.key.keysym.sym) {
                case SDLK_ESCAPE: key = 27; break;
                case SDLK_RETURN: key = 13; break;
                case SDLK_SPACE: key = 32; break;
                case SDLK_TAB: key = 9; break;
                case SDLK_BACKSPACE: key = 8; break;
                default:
                    if (event.key.keysym.sym >= SDLK_a && event.key.keysym.sym <= SDLK_z) {
                        key = static_cast<int>(event.key.keysym.sym) - 'a' + 'A';
                    } else if (event.key.keysym.sym >= SDLK_0 && event.key.keysym.sym <= SDLK_9) {
                        key = static_cast<int>(event.key.keysym.sym);
                    }
                    break;
            }
            if (key != 0) Input::OnKey(key, event.type == SDL_KEYDOWN, event.key.repeat != 0);
            if (event.type == SDL_KEYDOWN && key == 27 && m_survivors) {
                const auto state = m_survivors->GetState();
                if (state == SurvivorsDirector::State::Playing || state == SurvivorsDirector::State::Paused) {
                    m_survivors->TogglePause();
                }
            }
            break;
        }
        case SDL_MOUSEMOTION:
            m_mouseDeltaX += event.motion.xrel;
            m_mouseDeltaY += event.motion.yrel;
            m_mouseX = event.motion.x;
            m_mouseY = event.motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            if (event.button.button >= SDL_BUTTON_LEFT && event.button.button <= SDL_BUTTON_MIDDLE) {
                Input::OnMouseButton(event.button.button - 1, event.type == SDL_MOUSEBUTTONDOWN);
            }
            break;
        case SDL_MOUSEWHEEL:
            m_wheel += static_cast<float>(event.wheel.y);
            break;
        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION:
        case SDL_FINGERUP: {
            int ww = 0;
            int wh = 0;
            SDL_GetWindowSize(m_window, &ww, &wh);
            const float x = event.tfinger.x * static_cast<float>(ww);
            const float y = event.tfinger.y * static_cast<float>(wh);
            const bool down = event.type == SDL_FINGERDOWN;
            const bool up = event.type == SDL_FINGERUP;

            if (down) {
                m_touchFinger = event.tfinger.fingerId;
                // 右上角是游戏 HUD 的“Esc 暂停”区域：触摸等价于按 Esc。
                if (m_survivors && x > static_cast<float>(ww) - 170.0f && y < 90.0f) {
                    const auto state = m_survivors->GetState();
                    if (state == SurvivorsDirector::State::Playing || state == SurvivorsDirector::State::Paused) {
                        m_survivors->TogglePause();
                    }
                    break;
                }
                // 左半屏下方 = 摇杆；其余触摸交给 ImGui（按钮/菜单）。
                if (m_survivors && m_survivors->GetState() == SurvivorsDirector::State::Playing &&
                    x < static_cast<float>(ww) * 0.45f && y > static_cast<float>(wh) * 0.45f) {
                    m_touchActive = true;
                    m_touchOrigin = Vec2(x, y);
                    m_touchPosition = Vec2(x, y);
                }
            } else if (m_touchActive && (up || event.type == SDL_FINGERMOTION)) {
                m_touchPosition = Vec2(x, y);
                if (up) {
                    m_touchActive = false;
                    ReleaseTouchKeys();
                }
            }

            if (!m_touchActive) {
                ImGuiIO& io = ImGui::GetIO();
                io.AddMousePosEvent(x, y);
                if (down) io.AddMouseButtonEvent(0, true);
                if (up) io.AddMouseButtonEvent(0, false);
            }
            break;
        }
        default:
            break;
    }
}

void WebApp::Update(float dt) {
    if (!m_survivors) return;

    AudioSystem::Get().Update(dt);

    const bool playing = m_survivors->GetState() == SurvivorsDirector::State::Playing;
    if (playing) {
        m_accumulator += dt;
        int steps = 0;
        while (m_accumulator >= kFixedDt && steps < kMaxSubSteps) {
            m_world.FixedUpdate(kFixedDt);
            m_accumulator -= kFixedDt;
            ++steps;
        }
        if (steps == kMaxSubSteps) m_accumulator = 0.0f;
        m_world.Update(dt);
        m_survivors->Update(m_world, dt);
    }

    if (m_touchActive) UpdateTouchInput();
    SurvivorsPlayerComponent::SetInputEnabled(playing);
}

void WebApp::Render() {
    int windowWidth = 0;
    int windowHeight = 0;
    SDL_GetWindowSize(m_window, &windowWidth, &windowHeight);
    int drawableWidth = 0;
    int drawableHeight = 0;
    SDL_GL_GetDrawableSize(m_window, &drawableWidth, &drawableHeight);
    if (windowWidth <= 0 || windowHeight <= 0 || drawableWidth <= 0 || drawableHeight <= 0) return;

    Camera viewCamera = m_camera;
    viewCamera.aspect = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
    m_world.BuildRenderScene(m_scene, viewCamera);
    m_scene.is2D = true;

    Camera2D camera2D;
    camera2D.halfHeight = 7.2f;
    if (GameObject* player = m_world.FindByName("玩家")) {
        const Vec3 position = player->Position();
        Vec2 half(20.0f, 8.0f);
        if (auto* component = player->GetComponent<SurvivorsPlayerComponent>()) half = component->arenaHalf;
        camera2D.position = Vec2(Clamp(position.x, -half.x + 11.0f, half.x - 11.0f),
                                 Clamp(position.y, -half.y + 6.0f, half.y - 6.0f));
    }
    m_scene.camera2D = camera2D;

    m_renderer.Render(m_scene, static_cast<u32>(drawableWidth), static_cast<u32>(drawableHeight));

    if (m_survivors) {
        m_survivors->DrawUI(m_world, 0.0f, 0.0f, static_cast<float>(windowWidth), static_cast<float>(windowHeight));
        m_survivors->DrawCombatFeedback(m_world, m_scene.camera2D, 0.0f, 0.0f,
                                        static_cast<float>(windowWidth), static_cast<float>(windowHeight));
    }
    DrawTouchUI(static_cast<float>(windowWidth), static_cast<float>(windowHeight));
}

void WebApp::ApplyStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(10, 8);
    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 5);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.080f, 0.092f, 0.98f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.085f, 0.090f, 0.104f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.085f, 0.090f, 0.104f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.20f, 0.22f, 0.26f, 0.80f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.145f, 0.155f, 0.175f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.22f, 0.25f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.28f, 0.32f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.13f, 0.145f, 0.17f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.18f, 0.20f, 0.24f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.29f, 0.34f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.95f, 0.72f, 0.28f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.24f, 0.30f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.31f, 0.38f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.32f, 0.38f, 0.46f, 1.00f);
    colors[ImGuiCol_Text] = ImVec4(0.88f, 0.89f, 0.92f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.55f, 0.60f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.72f, 0.28f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.95f, 0.72f, 0.28f, 0.90f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.80f, 0.36f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.22f, 0.24f, 0.28f, 0.90f);
}

bool WebApp::LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    const std::string fontPath = FileSystem::AssetPath("fonts/ErMaoUI.ttf");
    if (fontPath.empty()) {
        LogError("[Web] 找不到字体 assets/fonts/ErMaoUI.ttf");
        return false;
    }

    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    builder.AddText(kSurvivorsUIGlyphs);
    ImVector<ImWchar> ranges;
    builder.BuildRanges(&ranges);

    m_bodyFont = AddFont(io, fontPath.c_str(), 24.0f, ranges.Data);
    m_largeFont = AddFont(io, fontPath.c_str(), 48.0f, ranges.Data);
    m_displayFont = AddFont(io, fontPath.c_str(), 96.0f, ranges.Data);
    if (!m_bodyFont || !m_largeFont || !m_displayFont) return false;
    LogInfo("[Web] UI 字体: %s", fontPath.c_str());
    return true;
}

void WebApp::UpdateTouchInput() {
    const float dx = m_touchPosition.x - m_touchOrigin.x;
    const float dy = m_touchPosition.y - m_touchOrigin.y;
    const float deadZone = 14.0f;
    const bool left = dx < -deadZone;
    const bool right = dx > deadZone;
    const bool up = dy < -deadZone;
    const bool down = dy > deadZone;

    auto setKey = [&](int key, bool state, int bit) {
        if (state) {
            Input::OnKey(key, true, false);
            m_touchKeys |= bit;
        } else if (m_touchKeys & bit) {
            Input::OnKey(key, false, false);
            m_touchKeys &= ~bit;
        }
    };
    setKey('A', left, 1);
    setKey('D', right, 2);
    setKey('W', up, 4);
    setKey('S', down, 8);
}

void WebApp::ReleaseTouchKeys() {
    if (m_touchKeys & 1) Input::OnKey('A', false, false);
    if (m_touchKeys & 2) Input::OnKey('D', false, false);
    if (m_touchKeys & 4) Input::OnKey('W', false, false);
    if (m_touchKeys & 8) Input::OnKey('S', false, false);
    m_touchKeys = 0;
}

void WebApp::DrawTouchUI(float width, float height) {
    if (!m_mobile || !m_survivors) return;
    const auto state = m_survivors->GetState();
    if (state != SurvivorsDirector::State::Playing && state != SurvivorsDirector::State::Paused) return;

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const float baseX = m_touchActive ? m_touchOrigin.x : 96.0f;
    const float baseY = m_touchActive ? m_touchOrigin.y : height - 96.0f;
    const ImU32 ring = IM_COL32(255, 255, 255, 42);
    const ImU32 knob = IM_COL32(255, 214, 96, 110);
    draw->AddCircleFilled(ImVec2(baseX, baseY), 58.0f, ring, 40);
    draw->AddCircle(ImVec2(baseX, baseY), 58.0f, IM_COL32(255, 255, 255, 80), 40, 2.0f);

    float knobX = baseX;
    float knobY = baseY;
    if (m_touchActive) {
        const float dx = m_touchPosition.x - m_touchOrigin.x;
        const float dy = m_touchPosition.y - m_touchOrigin.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        const float radius = 58.0f;
        if (length > radius) {
            knobX += dx / length * radius;
            knobY += dy / length * radius;
        } else {
            knobX += dx;
            knobY += dy;
        }
    }
    draw->AddCircleFilled(ImVec2(knobX, knobY), 24.0f, knob, 28);
}

void WebApp::Quit() {
    m_running = false;
    emscripten_cancel_main_loop();
}

} // namespace erm
