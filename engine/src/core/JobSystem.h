// ErMao Engine - 任务系统（GAMES104 第 20 讲：Data-Oriented Programming 与 Job System）
//
// 实现取舍：这里用"每次 ParallelFor 临时拉起线程 + 原子块调度"的模型。
// 它比常驻线程池多了线程创建开销（几十微秒），但完全没有生命周期/唤醒丢失的风险，
// 代码 80 行就能讲清楚"分块、抢占、join"三件事。工业引擎会用常驻池 + fiber 依赖图，
// 文档里给出了演进路径。
#pragma once

#include "core/Types.h"

#include <functional>

namespace erm {

class JobSystem {
public:
    static void Init(u32 threadCount = 0); // 0 = 硬件线程数
    static void Shutdown();

    static u32 ThreadCount();

    // 把 [begin, end) 按 chunkSize 切块并行执行；返回时全部完成
    static void ParallelFor(u32 begin, u32 end, const std::function<void(u32)>& body, u32 chunkSize = 32);
    static void ParallelFor(u32 begin, u32 end, const std::function<void(u32, u32)>& rangeBody,
                            u32 chunkSize = 32);

    // 压力测试：串行 vs 并行（Stats 面板里展示加速比）
    static float Benchmark(bool parallel, u32 iterations);

private:
    static u32 s_threadCount;
};

} // namespace erm
