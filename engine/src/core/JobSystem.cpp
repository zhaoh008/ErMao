// ErMao Engine - 任务系统实现
#include "core/JobSystem.h"
#include "core/Log.h"
#include "core/Timer.h"

#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

namespace erm {

u32 JobSystem::s_threadCount = 0;

void JobSystem::Init(u32 threadCount) {
    const u32 hardware = (std::max)(1u, std::thread::hardware_concurrency());
    s_threadCount = (threadCount == 0) ? hardware : threadCount;
    LogInfo("JobSystem 就绪：并行度 %u（硬件线程 %u）", s_threadCount, hardware);
}

void JobSystem::Shutdown() { s_threadCount = 0; }

u32 JobSystem::ThreadCount() { return s_threadCount; }

void JobSystem::ParallelFor(u32 begin, u32 end, const std::function<void(u32, u32)>& rangeBody, u32 chunkSize) {
    if (end <= begin) return;
    chunkSize = (std::max)(1u, chunkSize);

    const u32 total = end - begin;
    if (s_threadCount <= 1 || total <= chunkSize) {
        rangeBody(begin, end);
        return;
    }

    const u32 chunks = (total + chunkSize - 1) / chunkSize;
    const u32 workers = (std::min)(chunks, s_threadCount);
    std::atomic<u32> nextChunk{0};

    auto worker = [&]() {
        for (;;) {
            const u32 index = nextChunk.fetch_add(1);
            if (index >= chunks) break;
            const u32 lo = begin + index * chunkSize;
            const u32 hi = (std::min)(lo + chunkSize, end);
            rangeBody(lo, hi);
        }
    };

    // 主线程也参与，避免只等不干
    std::vector<std::thread> threads;
    threads.reserve(workers - 1);
    for (u32 i = 0; i + 1 < workers; ++i) threads.emplace_back(worker);
    worker();
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
}

void JobSystem::ParallelFor(u32 begin, u32 end, const std::function<void(u32)>& body, u32 chunkSize) {
    ParallelFor(
        begin, end,
        [&body](u32 lo, u32 hi) {
            for (u32 i = lo; i < hi; ++i) body(i);
        },
        chunkSize);
}

float JobSystem::Benchmark(bool parallel, u32 iterations) {
    auto work = [](u32 lo, u32 hi) {
        float sum = 0.0f;
        for (u32 i = lo; i < hi; ++i) {
            const float x = static_cast<float>(i) * 0.0001f;
            sum += std::sqrt(x) * std::sin(x) + std::cos(x * 3.0f);
        }
        // 防止被优化掉
        if (sum == 12345.678f) std::printf("unreachable\n");
    };

    const double t0 = Timer::Now();
    if (parallel) {
        ParallelFor(0, iterations, work, 4096);
    } else {
        work(0, iterations);
    }
    return static_cast<float>((Timer::Now() - t0) * 1000.0);
}

} // namespace erm
