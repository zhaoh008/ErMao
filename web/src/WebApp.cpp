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
    " !#$%&'()*+,-./0123456789:;<=>?ABCDEFGHIJKLMNOPQRSTUVWXYZ[]^_abcdefghijklmnopqrstuvwxyz{|}~·×“”→、。一丁"
    "三上下不与专且世业丛丢两个中临为主久么之乎乐乘九也习书了事二于互些亡交享亮人什仅仍从他代以们件任份伏休优会传伤伴伸但位低住体何余作使例供依侠侧便保信倍倒候值偏做停像允元先光免兜入全八公共关兵其具兼兽内"
    "册再冒写冰冲决冷冻准减凑几出击刃分切列刘则刚创初判利别到制刷刻前剑剩剪副劈力功加务动励匀包化匹区十升半卓单卡印即却历压原去参友双反发取受变叠口古句另只可台史右号各合同名后向否含启吸告周呼命和品响哪唯器"
    "囊四回因团围固国图圆圈在地场均坏坐块坛垂型垫域基堆塌塞填境墙增墨士声处备复外多够大天太央失头奏奖套奥好如姆始子字存学它安宋完官定宝实审客宣宫害家容宽宿密寸对导寿封射将小少尔尖尚尝就尸尺尾局屁层居屏展属"
    "山工左巨己已布师带帧常幅幕干平并幸幽广序应底度廊廓建开异弃式引张弱弹强当录形彩影径待很得微心必志快态思性怪总恢息悬意感慢戏成或战截户房所手才打执扩扰找技把投抖折抛报抽拉拖拟拥择拾拿持指按挑挡挤损捡换据"
    "掉排接控推掩提携摇摸撞播操擎支收攻放效敌教散数整文斗断新方旁旋无既日旧时明易星是显晃晕普景晰晶暂暖暗暴曲更最月有服朝期木未本术机杀杆材束条来板极构析枚果枝架柄染柔查柱标栈栏树样核根格桃框案桌档桩桶检棱"
    "槽模横橙次止正此步武死残段每毒比毛气水永求江没沿泉法波注洗洞活派流浅测浏浮海消淡深清渐渲游湖源满漂漆演潮火灯灰灵炉炸点烘烟焙焰然照爆爽片版牌牙牢物特状独猎率王玩环现珀球理琥瓶生用由电画界留略痕登白的益"
    "盖盘目直相看真着矢知短石码砖破础硬确碍碎碰磁磨示禁离秃种秒积称移稀程稍空穿突窄窗立竖站端笔符第等签算管箭箱类粒粗精糊系素索紧累约级纯纸纹线组细终经结绕绘给络绝统继绩绪续维绿缓缘缝缩缺网罗置群翻者而职联"
    "肉股胀背胜胶能脆脚脸腐膨自至致色节花苔若英范草莱获菜菱落蓝藏虎蚀蛛蜘血行衡补表衬被裁裂装要覆见观视览觉角解触计认让记许设证识诉试诗话询该误说请读调谷象负责败质贴贼资赖走赶起越足跑距跟跨路跳踏踪身躲轨转"
    "轮软轻载较辑输边达过运近返还这进远连迹追退适选透逐递途通速造逻遇遍道遮避那部都配酒醒采释里重野量金鉴针钉钟钮铺锁锅错锚键镜长闪闭间队阶附际降限除陨随隐隔障雅集雨雪雷雾需霆震霜露青静非靠面音页顶项顺须预"
    "题颜额风飘飞饰馈首香驱驿验骑骤骷髅高鬼鹰黑默鼠齐龙！（），：；";

ImFont* AddFont(ImGuiIO& io, const char* path, float size, const ImWchar* ranges) {
    if (!path || !*path) return nullptr;
    ImFontConfig config;
    config.OversampleH = 1;
    config.OversampleV = 1;
    config.PixelSnapH = true;
    return io.Fonts->AddFontFromFileTTF(path, size, &config, ranges);
}

// 微信小游戏 iOS 与安卓的 DOM/Canvas 适配层并不相同。Emscripten/SDL2 在创建
// 窗口时依赖若干浏览器 API（全局 devicePixelRatio、canvas.getBoundingClientRect、
// document.body.style 等），安卓/开发者工具里有，iOS 真机上可能缺失；缺失时
// SDL 会直接抛异常，微信只显示“程序遇到错误”，没有可读堆栈。
// 这里在 SDL 之前补齐最小接口；浏览器里什么都不做。返回被补丁的位掩码。
int InstallWeChatCompatShims() {
    return EM_ASM_INT({
        try {
            if (typeof wx === 'undefined') return 0;
            var info = {};
            try { info = wx.getWindowInfo ? wx.getWindowInfo() : wx.getSystemInfoSync(); } catch (e) {}
            var dpr = info.pixelRatio || 1;
            var changed = 0;

            if (typeof devicePixelRatio === 'undefined') {
                try { window.devicePixelRatio = dpr; changed |= 1; } catch (e) {}
                try { globalThis.devicePixelRatio = dpr; } catch (e) {}
            }
            if (typeof window !== 'undefined') {
                if (!window.innerWidth) { try { window.innerWidth = info.windowWidth || 1280; changed |= 2; } catch (e) {} }
                if (!window.innerHeight) { try { window.innerHeight = info.windowHeight || 720; changed |= 4; } catch (e) {} }
            }

            var cv = null;
            try { cv = (typeof Module !== 'undefined' && Module.canvas) ? Module.canvas : null; } catch (e) {}
            if (!cv) { try { cv = (typeof canvas !== 'undefined') ? canvas : null; } catch (e) {} }
            if (cv) {
                if (!cv.style) { try { cv.style = {}; changed |= 8; } catch (e) {} }
                if (!cv.getBoundingClientRect) {
                    try {
                        cv.getBoundingClientRect = function () {
                            var w = cv.width || info.windowWidth || 1280;
                            var h = cv.height || info.windowHeight || 720;
                            var r = {};
                            r.left = 0;
                            r.top = 0;
                            r.right = w;
                            r.bottom = h;
                            r.width = w;
                            r.height = h;
                            return r;
                        };
                        changed |= 16;
                    } catch (e) {}
                }
            }

            if (typeof document !== 'undefined') {
                if (!document.body) { try { document.body = {}; changed |= 32; } catch (e) {} }
                try { if (document.body && !document.body.style) { document.body.style = {}; changed |= 64; } } catch (e) {}
                if (!document.documentElement) { try { document.documentElement = {}; changed |= 128; } catch (e) {} }
                try {
                    if (document.documentElement && !document.documentElement.style) {
                        document.documentElement.style = {};
                        changed |= 256;
                    }
                } catch (e) {}
            }

            try {
                var cap = 'dpr=' + (typeof devicePixelRatio) + ' canvas=' + (cv ? 'yes' : 'no') +
                    ' rect=' + (cv && cv.getBoundingClientRect ? 'yes' : 'no') +
                    ' body=' + ((typeof document !== 'undefined' && document.body) ? 'yes' : 'no');
                if (typeof GameGlobal !== 'undefined' && GameGlobal.__ermaoStage) {
                    GameGlobal.__ermaoStage('caps.post ' + cap);
                }
            } catch (e) {}

            return changed;
        } catch (e) {
            return -1;
        }
    });
}

// 把启动阶段发到实时日志：iOS 真机没有 vConsole 时，至少能从运维中心看到
// 卡在 SDL/GL/字体/资源中的哪一步。由微信调试包注入的 ermao-logger.js 接收；
// 浏览器/普通包里是 no-op。
void WeChatDebugStage(const char* stage) {
    EM_ASM({
        try {
            if (typeof GameGlobal !== 'undefined' && GameGlobal.__ermaoStage) {
                GameGlobal.__ermaoStage(UTF8ToString($0));
            }
        } catch (e) {}
    }, stage);
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

    const int shims = InstallWeChatCompatShims();
    if (shims != 0) {
        LogInfo("[Web] 微信 DOM/Canvas 兼容补丁: mask=0x%X（0 表示浏览器/无需补）", static_cast<unsigned>(shims));
    }
    char shimStage[64];
    std::snprintf(shimStage, sizeof(shimStage), "shim.done mask=0x%X", static_cast<unsigned>(shims));
    WeChatDebugStage(shimStage);

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
    WeChatDebugStage("sdl.window");

    m_glContext = SDL_GL_CreateContext(m_window);
    if (!m_glContext) {
        LogError("[Web] 创建 GLES2 上下文失败: %s", SDL_GetError());
        return false;
    }
    WeChatDebugStage("gl.context");
    SDL_GL_MakeCurrent(m_window, m_glContext);
#ifndef __EMSCRIPTEN__
    SDL_GL_SetSwapInterval(1);
#else
    // Emscripten 的 SDL_GL_SetSwapInterval 会在 main() 创建 main loop 之前调用
    // emscripten_set_main_loop_timing，打印 “Cannot set timing mode ...”；浏览器/
    // 微信本来就由 requestAnimationFrame 驱动，这里跳过即可。
#endif

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
    WeChatDebugStage("imgui.init");

    // 2D UI 会用平台回调切换采样器；OpenGL 后端没有该回调时补一个空实现，
    // 避免 ImDrawList::AddCallback(nullptr)。
    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    platform.DrawCallback_SetSamplerLinear = [](const ImDrawList*, const ImDrawCmd*) {};
    platform.DrawCallback_SetSamplerNearest = [](const ImDrawList*, const ImDrawCmd*) {};

    if (!m_renderer.Init()) {
        LogError("[Web] 2D 渲染器初始化失败");
        return false;
    }
    WeChatDebugStage("renderer.init");

    m_assets.Init();
    RegisterSurvivorsComponents();
    m_world.Init(&m_assets);
    BuildSurvivorsLevel(m_world, m_assets);

    m_survivors = new SurvivorsDirector();
    m_survivors->InitializeUI(m_assets, m_bodyFont, m_largeFont, m_displayFont);
    m_survivors->ReturnToMenu();
    WeChatDebugStage("game.ready");

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
        // H5 演示/截图直接开局：URL 可用 class=0|1|2 指定职业，默认游侠。
        int classIndex = 0;
        const char* classParam = std::strstr(search, "class=");
        if (classParam) classIndex = std::atoi(classParam + 6);
        m_survivors->SelectPlayerClass(m_world, classIndex);
        m_survivors->Reset(m_world);
    }
    // 截图走查：与桌面 --ui-preview 共用同一份状态构造逻辑。
    if (search) {
        const char* preview = std::strstr(search, "ui-preview=");
        if (preview) {
            std::string screen;
            for (const char* p = preview + 11; *p && *p != '&' && *p != '#'; ++p) screen.push_back(*p);
            if (!screen.empty()) m_survivors->ApplyPreviewState(m_world, screen);
        }
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
        // 手机 HUD 需要避开刘海、状态栏与微信胶囊：读取 safeArea 与菜单按钮底边。
        // 宿主不提供时保持 0，HUD 会自行在竖屏保留顶部空间。
        auto inset = [](int edge) -> float {
            return static_cast<float>(EM_ASM_DOUBLE({
                try {
                    var info = wx.getWindowInfo ? wx.getWindowInfo() : wx.getSystemInfoSync();
                    var safe = info.safeArea || {};
                    var w = info.windowWidth || 0;
                    var h = info.windowHeight || 0;
                    if ($0 === 0) {
                        var top = safe.top || 0;
                        try {
                            var capsule = wx.getMenuButtonBoundingClientRect();
                            if (capsule && capsule.bottom) top = Math.max(top, capsule.bottom);
                        } catch (e) {}
                        return top;
                    }
                    if ($0 === 1) return safe.left || 0;
                    if ($0 === 2) return (safe.right && w > safe.right) ? (w - safe.right) : 0;
                    return (safe.bottom && h > safe.bottom) ? (h - safe.bottom) : 0;
                } catch (e) {
                    return 0;
                }
            }, edge));
        };
        m_safeInsets = Vec4(inset(1), inset(0), inset(2), inset(3));
        LogInfo("[Web] 安全区（CSS 像素）：左 %.1f 上 %.1f 右 %.1f 下 %.1f", m_safeInsets.x, m_safeInsets.y,
                m_safeInsets.z, m_safeInsets.w);
        // 小游戏启动后先让玩家选职业，不再默认“游侠”直接开局。
        WeChatDebugStage("game.classselect.before");
        m_survivors->ShowClassSelect(m_world);
        WeChatDebugStage("game.classselect.after");
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
    WeChatDebugStage("init.done");
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

            if (down && m_survivors && m_survivors->GetState() == SurvivorsDirector::State::Playing) {
                // 暂停键是战斗 HUD 里唯一需要点的按钮；其余任意位置都立刻成为摇杆原点。
                if (m_survivors->HitPauseButton(x, y)) {
                    if (m_touchActive) {
                        m_touchActive = false;
                        m_touchFinger = -1;
                        ReleaseTouchKeys();
                    }
                    m_survivors->TogglePause();
                    break;
                }
                // 只让第一根手指驱动移动；摇杆 UI 完全隐藏，屏幕任意位置按下即可拖动。
                if (!m_touchActive) {
                    m_touchActive = true;
                    m_touchFinger = event.tfinger.fingerId;
                    m_touchOrigin = Vec2(x, y);
                    m_touchPosition = Vec2(x, y);
                }
            } else if (m_touchActive && event.tfinger.fingerId == m_touchFinger &&
                       (event.type == SDL_FINGERMOTION || up)) {
                m_touchPosition = Vec2(x, y);
                if (up) {
                    m_touchActive = false;
                    m_touchFinger = -1;
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

    const auto state = m_survivors->GetState();
    const bool playing = state == SurvivorsDirector::State::Playing;
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
    }
    // 抽奖动画由 SurvivorsDirector 自己推进（State::Gacha 时 UpdateChestGacha），
    // 但此时已经不在 Playing；如果只按 Playing 调 Update，m_gachaTime 会永远停在
    // 0，画面卡在开箱第一帧，点击也永远等不到 reveal。世界更新仍冻结，只推进导演。
    if (playing || state == SurvivorsDirector::State::Gacha) {
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
    // 竖屏视口很窄，7.2 的视野宽度只有约 6.6 世界单位，镜头会显得贴脸；竖屏拉远。
    const bool portrait = windowHeight > windowWidth;
    camera2D.halfHeight = portrait ? 13.0f : 7.2f;
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
        m_survivors->DrawUI(m_world, 0.0f, 0.0f, static_cast<float>(windowWidth),
                            static_cast<float>(windowHeight), m_safeInsets);
        m_survivors->DrawCombatFeedback(m_world, m_scene.camera2D, 0.0f, 0.0f,
                                        static_cast<float>(windowWidth), static_cast<float>(windowHeight));
    }
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

    // 标题用素材包里的书法字体子集（只含“二毛幸存者”+ASCII），正文仍用 Apache-2.0 的
    // 文泉驿子集，保证小字号中文清晰。
    const std::string titleFontPath = FileSystem::AssetPath("fonts/InkWashTitle.ttf");
    if (!titleFontPath.empty()) {
        ImFontGlyphRangesBuilder titleBuilder;
        titleBuilder.AddRanges(io.Fonts->GetGlyphRangesDefault());
        titleBuilder.AddText("二毛幸存者");
        ImVector<ImWchar> titleRanges;
        titleBuilder.BuildRanges(&titleRanges);
        m_displayFont = AddFont(io, titleFontPath.c_str(), 96.0f, titleRanges.Data);
    }
    if (!m_displayFont) m_displayFont = AddFont(io, fontPath.c_str(), 96.0f, ranges.Data);
    if (!m_bodyFont || !m_largeFont || !m_displayFont) return false;
    LogInfo("[Web] UI 字体: %s / 标题 %s", fontPath.c_str(),
            titleFontPath.empty() ? "(BODY)" : titleFontPath.c_str());
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


void WebApp::Quit() {
    m_running = false;
    emscripten_cancel_main_loop();
}

} // namespace erm
