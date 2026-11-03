// ErMao Engine - Win32 窗口实现
#include "platform/Window.h"
#include "core/Log.h"

#include <windows.h>
#include <windowsx.h>

namespace erm {

namespace {
const wchar_t* kClassName = L"ErMaoEngineWindow";
}

Window::~Window() { Destroy(); }

bool Window::Create(const char* title, int width, int height) {
    m_hinstance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&Window::WndProcStatic);
    wc.hInstance = static_cast<HINSTANCE>(m_hinstance);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) {
        LogError("RegisterClassEx 失败: %lu", GetLastError());
        return false;
    }

    RECT rect{0, 0, width, height};
    AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);
    const int w = rect.right - rect.left;
    const int h = rect.bottom - rect.top;

    wchar_t wtitle[256]{};
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 255);

    m_hwnd = CreateWindowExW(0, kClassName, wtitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, w, h,
                             nullptr, nullptr, static_cast<HINSTANCE>(m_hinstance), this);
    if (!m_hwnd) {
        LogError("CreateWindowEx 失败: %lu", GetLastError());
        return false;
    }

    m_width = static_cast<u32>(width);
    m_height = static_cast<u32>(height);
    ShowWindow(static_cast<HWND>(m_hwnd), SW_SHOW);
    UpdateWindow(static_cast<HWND>(m_hwnd));
    LogInfo("窗口创建成功: %dx%d", width, height);
    return true;
}

void Window::Destroy() {
    if (m_hwnd) {
        DestroyWindow(static_cast<HWND>(m_hwnd));
        m_hwnd = nullptr;
    }
    if (m_hinstance) {
        UnregisterClassW(kClassName, static_cast<HINSTANCE>(m_hinstance));
        m_hinstance = nullptr;
    }
}

bool Window::Poll() {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { m_shouldClose = true; break; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 鼠标捕获时：把光标锁在窗口中心，用位移驱动视角
    if (m_mouseCaptured && m_hwnd && m_focused) {
        POINT center{static_cast<LONG>(m_width / 2), static_cast<LONG>(m_height / 2)};
        ClientToScreen(static_cast<HWND>(m_hwnd), &center);
        POINT cur{};
        GetCursorPos(&cur);
        const float dx = static_cast<float>(cur.x - center.x);
        const float dy = static_cast<float>(cur.y - center.y);
        if (dx != 0.0f || dy != 0.0f) {
            m_mouseDelta += Vec2(dx, dy);
            SetCursorPos(center.x, center.y);
        }
        m_mousePos = Vec2(static_cast<float>(m_width) * 0.5f, static_cast<float>(m_height) * 0.5f);
    }
    return !m_shouldClose;
}

void Window::SetMouseCaptured(bool captured) {
    if (m_mouseCaptured == captured) return;
    m_mouseCaptured = captured;
    if (captured) {
        ShowCursor(FALSE);
        SetCapture(static_cast<HWND>(m_hwnd));
    } else {
        ShowCursor(TRUE);
        ReleaseCapture();
    }
    m_mouseDelta = Vec2(0, 0);
}

Vec2 Window::ConsumeMouseDelta() {
    const Vec2 d = m_mouseDelta;
    m_mouseDelta = Vec2(0, 0);
    return d;
}

float Window::ConsumeWheel() {
    const float w = m_wheel;
    m_wheel = 0.0f;
    return w;
}

long long __stdcall Window::WndProcStatic(void* hwnd, unsigned int msg, unsigned long long wparam, long long lparam) {
    Window* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<Window*>(cs->lpCreateParams);
        SetWindowLongPtrW(static_cast<HWND>(hwnd), GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<Window*>(GetWindowLongPtrW(static_cast<HWND>(hwnd), GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(hwnd, msg, wparam, lparam);
    return DefWindowProcW(static_cast<HWND>(hwnd), msg, static_cast<WPARAM>(wparam), static_cast<LPARAM>(lparam));
}

long long Window::HandleMessage(void* hwnd, unsigned int msg, unsigned long long wparam, long long lparam) {
    // UI 层（ImGui）只做“观察”，不消费消息（审核 P2-16）；
    // 唯一例外是输入法的提交结果，见下面的 WM_IME_COMPOSITION 分支。
    bool rawHandled = false;
    if (m_callbacks.onRawMessage) {
        rawHandled = m_callbacks.onRawMessage(hwnd, msg, wparam, lparam);
    }
    switch (msg) {
        case WM_CLOSE:
            m_shouldClose = true;
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED) {
                const u32 nw = LOWORD(lparam);
                const u32 nh = HIWORD(lparam);
                if (nw > 0 && nh > 0 && (nw != m_width || nh != m_height)) {
                    m_width = nw;
                    m_height = nh;
                    m_resized = true;
                }
            }
            return 0;
        case WM_SETFOCUS:
            m_focused = true;
            return 0;
        case WM_KILLFOCUS:
            m_focused = false;
            if (m_mouseCaptured) SetMouseCaptured(false);
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            const bool repeat = (lparam & (1LL << 30)) != 0;
            if (m_callbacks.onKey) m_callbacks.onKey(static_cast<int>(wparam), true, repeat);
            return 0;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (m_callbacks.onKey) m_callbacks.onKey(static_cast<int>(wparam), false, false);
            return 0;
        case WM_CHAR:
            if (m_callbacks.onChar) m_callbacks.onChar(static_cast<unsigned int>(wparam));
            return 0;
        case WM_IME_COMPOSITION:
            // ImGui 的 Win32 后端在 raw 回调里已经对提交结果（GCS_RESULTSTR）
            // 调用过 DefWindowProcW（见 imgui_impl_win32.cpp 的 WM_IME_COMPOSITION
            // 分支），提交字符串由它发出的 WM_IME_CHAR → WM_CHAR 进入 ImGui。
            // 窗口层再走一次默认处理会再送一遍，导致每个提交的汉字重复。
            if (rawHandled) return 0;
            break;
        case WM_MOUSEMOVE: {
            if (!m_mouseCaptured) {
                const float x = static_cast<float>(GET_X_LPARAM(lparam));
                const float y = static_cast<float>(GET_Y_LPARAM(lparam));
                m_mouseDelta += Vec2(x, y) - m_mousePos;
                m_mousePos = Vec2(x, y);
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN: {
            const int button = (msg == WM_LBUTTONDOWN) ? 0 : (msg == WM_RBUTTONDOWN) ? 1 : 2;
            if (m_callbacks.onMouseButton) m_callbacks.onMouseButton(button, true);
            // 捕获鼠标以便拖动 UI / 转视角
            SetCapture(static_cast<HWND>(hwnd));
            return 0;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP: {
            const int button = (msg == WM_LBUTTONUP) ? 0 : (msg == WM_RBUTTONUP) ? 1 : 2;
            if (m_callbacks.onMouseButton) m_callbacks.onMouseButton(button, false);
            if (!m_mouseCaptured) ReleaseCapture();
            return 0;
        }
        case WM_MOUSEWHEEL:
            m_wheel += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / 120.0f;
            return 0;
        case WM_ERASEBKGND:
            return 1;
        default:
            break;
    }
    return DefWindowProcW(static_cast<HWND>(hwnd), msg, static_cast<WPARAM>(wparam), static_cast<LPARAM>(lparam));
}

} // namespace erm
