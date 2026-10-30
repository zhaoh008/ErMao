// ErMao Engine - Win32 窗口与鼠标捕获（平台层，不依赖任何上层模块）
#pragma once

#include "core/Types.h"
#include "core/Math.h"
#include <functional>

namespace erm {

// Win32 窗口封装。
// 负责：创建窗口、消息泵、鼠标捕获（FPS 视角）、把输入事件通过回调转发给 Input。
// 注意：平台层不认识 Scene/Renderer，只暴露原生句柄给 RHI。
class Window {
public:
    struct Callbacks {
        std::function<void(int vk, bool down, bool repeat)> onKey;
        std::function<void(int button, bool down)>         onMouseButton;
        std::function<void(unsigned int codepoint)>        onChar;
        // 把原生消息透传给 UI 层（ImGui 的 Win32 后端需要先于窗口逻辑看到消息）
        std::function<bool(void* hwnd, unsigned int msg, unsigned long long wparam, long long lparam)> onRawMessage;
    };

    Window() = default;
    ~Window();

    bool Create(const char* title, int width, int height);
    void Destroy();

    // 处理所有待处理消息；返回 false 表示窗口已关闭
    bool Poll();

    void SetMouseCaptured(bool captured);
    bool IsMouseCaptured() const { return m_mouseCaptured; }

    Vec2  MousePosition() const { return m_mousePos; }
    Vec2  ConsumeMouseDelta();
    float ConsumeWheel();

    u32  Width() const { return m_width; }
    u32  Height() const { return m_height; }
    bool WasResized() const { return m_resized; }
    void ClearResized() { m_resized = false; }
    bool IsFocused() const { return m_focused; }

    void* NativeHandle() const { return m_hwnd; }
    Callbacks& Events() { return m_callbacks; }

private:
    static long long __stdcall WndProcStatic(void* hwnd, unsigned int msg, unsigned long long wparam, long long lparam);
    long long HandleMessage(void* hwnd, unsigned int msg, unsigned long long wparam, long long lparam);

    void*  m_hwnd = nullptr;
    void*  m_hinstance = nullptr;
    u32    m_width = 0;
    u32    m_height = 0;
    bool   m_resized = false;
    bool   m_focused = true;
    bool   m_mouseCaptured = false;
    Vec2   m_mousePos{};
    Vec2   m_mouseDelta{};
    float  m_wheel = 0.0f;
    bool   m_shouldClose = false;
    Callbacks m_callbacks;
};

} // namespace erm
