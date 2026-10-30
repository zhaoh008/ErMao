// ErMao Engine - ImGui 集成层实现
#include "editor/ImGuiLayer.h"
#include "core/Log.h"
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

bool ImGuiLayer::Init(Window& window, void* d3dDevice, void* d3dContext) {
    IMGUI_CHECKVERSION();
    m_context = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_context);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.IniFilename = "imgui.ini";

    HWND hwnd = static_cast<HWND>(window.NativeHandle());
    m_dpiScale = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f;
    if (m_dpiScale < 1.0f) m_dpiScale = 1.0f;
    m_fontSize = 16.0f * m_dpiScale;

    ApplyStyle(m_dpiScale);
    if (!LoadFonts(m_dpiScale)) {
        LogWarn("ImGui 字体加载失败，回退默认字体（中文会显示为方块）");
        io.Fonts->AddFontDefault();
    }

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

bool ImGuiLayer::LoadFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    const char* candidates[] = {
        "C:/Windows/Fonts/msyh.ttc",   // 微软雅黑
        "C:/Windows/Fonts/msyh.ttf",
        "C:/Windows/Fonts/simhei.ttf", // 黑体
        "C:/Windows/Fonts/segoeui.ttf",
    };

    for (const char* path : candidates) {
        if (!FileExists(path)) continue;
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 2;
        cfg.PixelSnapH = true;
        ImFont* font = io.Fonts->AddFontFromFileTTF(path, m_fontSize, &cfg,
                                                    io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        if (font) {
            LogInfo("UI 字体: %s (%.1fpx)", path, m_fontSize);
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
