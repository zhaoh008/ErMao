// ErMao Engine - ImGui 集成层实现
#include "editor/ImGuiLayer.h"
#include "core/Log.h"
#include "core/FileSystem.h"
#include "platform/Window.h"

#include <windows.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace erm {

namespace {
bool FileExists(const char* path) {
    const DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}
} // namespace

bool ImGuiLayer::Init(Window& window, void* d3dDevice, void* d3dContext, bool useIniFile) {
    IMGUI_CHECKVERSION();
    m_context = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_context);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;


    HWND hwnd = static_cast<HWND>(window.NativeHandle());
    m_dpiScale = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f;
    if (m_dpiScale < 1.0f) m_dpiScale = 1.0f;
    m_fontSize = 16.0f * m_dpiScale;

    // ini 放在 exe 旁边，避免从不同 CWD 启动时到处生成 imgui.ini（审核 P3-36）
    static std::string iniPath = FileSystem::Join(FileSystem::ExecutableDir(), "imgui.ini");
    io.IniFilename = useIniFile ? iniPath.c_str() : nullptr;

    ApplyStyle(m_dpiScale);
    if (!LoadFonts(m_dpiScale)) {
        LogWarn("ImGui 字体加载失败，回退默认字体（中文会显示为方块）");
        io.Fonts->AddFontDefault();
    }
    LoadSurvivorsUIFonts();

    if (!ImGui_ImplWin32_Init(hwnd)) {
        LogError("ImGui Win32 后端初始化失败");
        return false;
    }
    if (!ImGui_ImplDX11_Init(static_cast<ID3D11Device*>(d3dDevice),
                             static_cast<ID3D11DeviceContext*>(d3dContext))) {
        LogError("ImGui DX11 后端初始化失败");
        return false;
    }

    m_initialized = true;
    LogInfo("ImGui 初始化完成（版本 %s，DPI 缩放 %.2f）", IMGUI_VERSION, m_dpiScale);
    return true;
}

void ImGuiLayer::Shutdown() {
    if (!m_initialized) return;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(m_context);
    m_context = nullptr;
    m_initialized = false;
}

void ImGuiLayer::ApplyStyle(float scale) {
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
    style.WindowMenuButtonPosition = ImGuiDir_None;

    // 暗色 + 琥珀色强调（与引擎 logo 色一致）
    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.080f, 0.092f, 0.98f);
    c[ImGuiCol_ChildBg] = ImVec4(0.085f, 0.090f, 0.104f, 1.00f);
    c[ImGuiCol_PopupBg] = ImVec4(0.085f, 0.090f, 0.104f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.20f, 0.22f, 0.26f, 0.80f);
    c[ImGuiCol_FrameBg] = ImVec4(0.145f, 0.155f, 0.175f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.22f, 0.25f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.28f, 0.32f, 1.00f);
    c[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.13f, 0.145f, 0.17f, 1.00f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
    c[ImGuiCol_Button] = ImVec4(0.18f, 0.20f, 0.24f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.29f, 0.34f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.95f, 0.72f, 0.28f, 1.00f);
    c[ImGuiCol_Header] = ImVec4(0.20f, 0.24f, 0.30f, 1.00f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.31f, 0.38f, 1.00f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.32f, 0.38f, 0.46f, 1.00f);
    c[ImGuiCol_Tab] = ImVec4(0.11f, 0.12f, 0.145f, 1.00f);
    c[ImGuiCol_TabHovered] = ImVec4(0.30f, 0.34f, 0.40f, 1.00f);
    c[ImGuiCol_TabSelected] = ImVec4(0.18f, 0.21f, 0.26f, 1.00f);
    c[ImGuiCol_DockingPreview] = ImVec4(0.95f, 0.72f, 0.28f, 0.55f);
    c[ImGuiCol_DockingEmptyBg] = ImVec4(0.06f, 0.065f, 0.075f, 1.00f);
    c[ImGuiCol_Text] = ImVec4(0.88f, 0.89f, 0.92f, 1.00f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.55f, 0.60f, 1.00f);
    c[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.72f, 0.28f, 1.00f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.95f, 0.72f, 0.28f, 0.90f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.80f, 0.36f, 1.00f);
    c[ImGuiCol_Separator] = ImVec4(0.22f, 0.24f, 0.28f, 0.90f);
    c[ImGuiCol_ResizeGrip] = ImVec4(0.30f, 0.34f, 0.40f, 0.60f);
    c[ImGuiCol_PlotLines] = ImVec4(0.55f, 0.78f, 0.95f, 1.00f);
    c[ImGuiCol_PlotHistogram] = ImVec4(0.95f, 0.72f, 0.28f, 1.00f);

    style.ScaleAllSizes(scale);
}

namespace {
// 2D 幸存者 UI 只使用这些中文/符号；按实际文本裁剪字形，避免在 4096 图集里塞入整套常用字。
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
} // namespace

void ImGuiLayer::LoadSurvivorsUIFonts() {
    ImGuiIO& io = ImGui::GetIO();
    wchar_t windowsDir[MAX_PATH]{};
    GetWindowsDirectoryW(windowsDir, MAX_PATH);
    const std::string fontDir = FileSystem::WideToUtf8(windowsDir) + "\\Fonts\\";

    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    builder.AddText(kSurvivorsUIGlyphs);
    ImVector<ImWchar> ranges;
    builder.BuildRanges(&ranges);

    ImFontConfig cfg;
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;
    cfg.PixelSnapH = true;

    auto load = [&](const char* fileName, float size) -> ImFont* {
        const std::string path = fontDir + fileName;
        if (!FileExists(path.c_str())) return nullptr;
        return io.Fonts->AddFontFromFileTTF(path.c_str(), size, &cfg, ranges.Data);
    };

    // 正文字形覆盖面最广：微软雅黑。标题用雅黑粗体，展示标题用宋体（更接近原型的衬线标题）。
    m_uiBodyFont = load("msyh.ttc", 24.0f);
    if (!m_uiBodyFont) m_uiBodyFont = load("simhei.ttf", 24.0f);
    m_uiLargeFont = load("msyhbd.ttc", 48.0f);
    if (!m_uiLargeFont) m_uiLargeFont = load("msyh.ttc", 48.0f);
    m_uiDisplayFont = load("simsun.ttc", 96.0f);
    if (!m_uiDisplayFont) m_uiDisplayFont = load("msyhbd.ttc", 96.0f);

    if (m_uiBodyFont && m_uiLargeFont && m_uiDisplayFont) {
        LogInfo("2D UI 字体: 正文 %.0fpx / 标题 %.0fpx / 展示 %.0fpx", 24.0f, 48.0f, 96.0f);
    } else {
        LogWarn("2D UI 字体加载不完整（正文=%p 标题=%p 展示=%p），将退回默认 ImGui 字体", m_uiBodyFont,
                m_uiLargeFont, m_uiDisplayFont);
    }
}

bool ImGuiLayer::LoadFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    wchar_t windowsDir[MAX_PATH]{};
    GetWindowsDirectoryW(windowsDir, MAX_PATH);
    const std::string fontDir = FileSystem::WideToUtf8(windowsDir) + "\\Fonts\\";
    const std::string candidates[] = {
        fontDir + "msyh.ttc",   // 微软雅黑
        fontDir + "msyh.ttf",
        fontDir + "simhei.ttf", // 黑体
        fontDir + "segoeui.ttf",
    };

    for (const std::string& path : candidates) {
        if (!FileExists(path.c_str())) continue;
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 2;
        cfg.PixelSnapH = true;
        ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), m_fontSize, &cfg,
                                                    io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        if (font) {
            LogInfo("UI 字体: %s (%.1fpx)", path.c_str(), m_fontSize);
            return true;
        }
    }
    return false;
}

void ImGuiLayer::BeginFrame() {
    if (!m_initialized) return;
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::EndFrame() {
    if (!m_initialized) return;
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

bool ImGuiLayer::WndProc(void* hwnd, unsigned int msg, unsigned long long wparam, long long lparam) {
    if (!m_initialized) return false;
    return ImGui_ImplWin32_WndProcHandler(static_cast<HWND>(hwnd), msg, static_cast<WPARAM>(wparam),
                                          static_cast<LPARAM>(lparam)) != 0;
}

} // namespace erm
