// ErMao Engine - 输入实现
#include "platform/Input.h"

namespace erm {

bool  Input::s_down[256]{};
bool  Input::s_pressed[256]{};
bool  Input::s_released[256]{};
bool  Input::s_mouseDown[3]{};
bool  Input::s_mousePressed[3]{};
bool  Input::s_mouseReleased[3]{};
Vec2  Input::s_mousePos{};
Vec2  Input::s_mouseDelta{};
float Input::s_wheel = 0.0f;

void Input::BeginFrame() {
    std::memset(s_pressed, 0, sizeof(s_pressed));
    std::memset(s_released, 0, sizeof(s_released));
    std::memset(s_mousePressed, 0, sizeof(s_mousePressed));
    std::memset(s_mouseReleased, 0, sizeof(s_mouseReleased));
    s_mouseDelta = Vec2(0, 0);
    s_wheel = 0.0f;
}

void Input::Reset() {
    std::memset(s_down, 0, sizeof(s_down));
    std::memset(s_pressed, 0, sizeof(s_pressed));
    std::memset(s_released, 0, sizeof(s_released));
    std::memset(s_mouseDown, 0, sizeof(s_mouseDown));
    std::memset(s_mousePressed, 0, sizeof(s_mousePressed));
    std::memset(s_mouseReleased, 0, sizeof(s_mouseReleased));
    s_mouseDelta = Vec2(0, 0);
    s_wheel = 0.0f;
}

void Input::Shutdown() {
    std::memset(s_down, 0, sizeof(s_down));
    std::memset(s_mouseDown, 0, sizeof(s_mouseDown));
}

void Input::OnKey(int vk, bool down, bool repeat) {
    if (vk < 0 || vk >= 256) return;
    if (down && repeat) return; // 系统重复不产生新的边沿
    if (down && !s_down[vk]) s_pressed[vk] = true;
    if (!down && s_down[vk]) s_released[vk] = true;
    s_down[vk] = down;
}

void Input::OnMouseButton(int button, bool down) {
    if (button < 0 || button > 2) return;
    if (down && !s_mouseDown[button]) s_mousePressed[button] = true;
    if (!down && s_mouseDown[button]) s_mouseReleased[button] = true;
    s_mouseDown[button] = down;
}

void Input::SetMouseState(const Vec2& position, const Vec2& delta, float wheel) {
    s_mousePos = position;
    s_mouseDelta = delta;
    s_wheel = wheel;
}

bool Input::IsKeyDown(int vk) { return vk >= 0 && vk < 256 && s_down[vk]; }
bool Input::IsKeyPressed(int vk) { return vk >= 0 && vk < 256 && s_pressed[vk]; }
bool Input::IsKeyReleased(int vk) { return vk >= 0 && vk < 256 && s_released[vk]; }

bool Input::IsMouseDown(int b) { return b >= 0 && b < 3 && s_mouseDown[b]; }
bool Input::IsMousePressed(int b) { return b >= 0 && b < 3 && s_mousePressed[b]; }
bool Input::IsMouseReleased(int b) { return b >= 0 && b < 3 && s_mouseReleased[b]; }

Vec2  Input::MousePosition() { return s_mousePos; }
Vec2  Input::MouseDelta() { return s_mouseDelta; }
float Input::WheelDelta() { return s_wheel; }

} // namespace erm
