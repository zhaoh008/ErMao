// ErMao Engine - 高精度计时（GAMES104 第 2 讲：引擎的"心跳"）
#pragma once

#include "core/Types.h"

namespace erm {

// 基于 QueryPerformanceCounter 的帧计时器。
// 固定步长更新用 FixedDt，渲染用可变 Delta。
class Timer {
public:
    static void  Init();
    static double Now();          // 引擎启动后的秒数（单调递增）
    static float Delta();         // 上一帧时间（秒）
    static float SmoothedDelta(); // 指数平滑后的帧时间
    static float Fps();
    static u64   FrameCount();
    static void  Tick();          // 每帧开头调用

private:
    static double s_start;
    static double s_last;
    static double s_freq;
    static float  s_delta;
    static float  s_smoothed;
    static u64    s_frame;
};

} // namespace erm
