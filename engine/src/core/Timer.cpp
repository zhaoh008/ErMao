// ErMao Engine - 计时器实现
#include "core/Timer.h"

#include <windows.h>

namespace erm {

double Timer::s_start = 0.0;
double Timer::s_last = 0.0;
double Timer::s_freq = 1.0;
float  Timer::s_delta = 0.0f;
float  Timer::s_smoothed = 0.016f;
u64    Timer::s_frame = 0;

double Timer::Now() {
    if (s_freq <= 1.0) { // 允许在任何显式 Init 之前被调用（日志初始化就会用到）
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        s_freq = static_cast<double>(f.QuadPart);
        if (f.QuadPart == 0) s_freq = 1.0;
    }
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / s_freq;
}

void Timer::Init() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    s_freq = static_cast<double>(f.QuadPart);
    s_start = Now();
    s_last = s_start;
}

void Timer::Tick() {
    const double now = Now();
    const double raw = now - s_last;
    s_last = now;
    s_delta = static_cast<float>(raw);
    // 用指数移动平均抑制抖动，同时保留突变（卡顿）的可观测性
    s_smoothed = s_smoothed * 0.9f + s_delta * 0.1f;
    ++s_frame;
}

float Timer::Delta() { return s_delta; }
float Timer::SmoothedDelta() { return s_smoothed; }
float Timer::Fps() { return s_smoothed > 1e-6f ? 1.0f / s_smoothed : 0.0f; }
u64   Timer::FrameCount() { return s_frame; }

} // namespace erm
