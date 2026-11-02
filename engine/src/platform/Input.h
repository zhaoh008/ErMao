// ErMao Engine - 输入（帧一致的双缓冲 + 边沿检测）
//
// 关键约定（修复审核报告 P0-1）：
//   * 边沿（pressed/released）在**回调里**根据 down 的前后变化计算，不再由 Update 事后比对；
//   * 帧首调用 BeginFrame() 清空本帧的边沿/滚轮/鼠标增量，之后的消息泵会重新填充它们；
//   * 因此 BeginFrame() 必须在 Window::Poll() 之前调用。
#pragma once

#include "core/Math.h"
#include "core/Types.h"

namespace erm {

class Input {
public:
    static void BeginFrame(); // 帧首：清空边沿、滚轮、鼠标增量
    static void Shutdown();

    // 由窗口回调调用
    static void OnKey(int vk, bool down, bool repeat);
    static void OnMouseButton(int button, bool down);
    // 由 Application 在 Poll 之后拉取一次（单一真相：窗口持有状态，输入层只做快照）
    static void SetMouseState(const Vec2& position, const Vec2& delta, float wheel);

    static bool IsKeyDown(int vk);
    static bool IsKeyPressed(int vk);  // 本帧刚按下
    static bool IsKeyReleased(int vk); // 本帧刚松开

    static bool IsMouseDown(int button); // 0=左 1=右 2=中
    static bool IsMousePressed(int button);
    static bool IsMouseReleased(int button);

    static Vec2  MousePosition();
    static Vec2  MouseDelta();
    static float WheelDelta();

private:
    static bool  s_down[256];
    static bool  s_pressed[256];
    static bool  s_released[256];
    static bool  s_mouseDown[3];
    static bool  s_mousePressed[3];
    static bool  s_mouseReleased[3];
    static Vec2  s_mousePos;
    static Vec2  s_mouseDelta;
    static float s_wheel;
};

} // namespace erm
