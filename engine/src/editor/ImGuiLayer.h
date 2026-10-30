// ErMao Engine - ImGui 集成层（Win32 + D3D11 后端）
//
// 职责：
//   * 创建/销毁 ImGui 上下文与后端；
//   * 把原生窗口消息转发给 ImGui（必须比窗口逻辑更早看到消息）；
//   * 每帧 BeginFrame / EndFrame（EndFrame 把 UI 画到当前绑定的后备缓冲上）。
#pragma once

#include "core/Types.h"

struct ImGuiContext;

namespace erm {

class Window;

class ImGuiLayer {
public:
    bool Init(Window& window, void* d3dDevice, void* d3dContext);
    void Shutdown();

    void BeginFrame();
    void EndFrame();

    // 供 Window 的消息钩子调用；返回 true 表示消息已被 UI 消费
    bool WndProc(void* hwnd, unsigned int msg, unsigned long long wparam, long long lparam);

    ImGuiContext* Context() const { return m_context; }
    ImGuiContext* Get() const { return m_context; }

    float  FontSize() const { return m_fontSize; }
    float  DpiScale() const { return m_dpiScale; }

private:
    void ApplyStyle(float scale);
    bool LoadFonts(float scale);

    ImGuiContext* m_context = nullptr;
    float         m_fontSize = 16.0f;
    float         m_dpiScale = 1.0f;
    bool          m_initialized = false;
};

} // namespace erm
