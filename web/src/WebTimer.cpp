// ErMao Engine - Web 计时器（Emscripten 高精度时钟）
#include "core/Timer.h"

#include <emscripten/emscripten.h>

namespace erm {

double Timer::s_start = 0.0;
double Timer::s_last = 0.0;
double Timer::s_freq = 1000.0;
float  Timer::s_delta = 0.0f;
float  Timer::s_smoothed = 0.016f;
u64    Timer::s_frame = 0;

double Timer::Now() {
    return emscripten_get_now() / 1000.0;
}

void Timer::Init() {
    s_freq = 1000.0;
    s_start = Now();
    s_last = s_start;
}

void Timer::Tick() {
    const double now = Now();
    const double raw = now - s_last;
    s_last = now;
    s_delta = static_cast<float>(raw);
    s_smoothed = s_smoothed * 0.9f + s_delta * 0.1f;
    ++s_frame;
}

float Timer::Delta() { return s_delta; }
float Timer::SmoothedDelta() { return s_smoothed; }
float Timer::Fps() { return s_smoothed > 1e-6f ? 1.0f / s_smoothed : 0.0f; }
u64   Timer::FrameCount() { return s_frame; }

} // namespace erm
