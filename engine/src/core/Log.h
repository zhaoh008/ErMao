// ErMao Engine - 日志系统（GAMES104 第 13 讲 工具链）
#pragma once

#include "core/Types.h"
#include <string>
#include <vector>
#include <mutex>

namespace erm {

enum class LogLevel : u8 { Trace = 0, Info, Warn, Error };

struct LogEntry {
    LogLevel    level;
    std::string message;
    double      time; // 引擎启动后的秒数
};

// 全局日志：同时写控制台、环形缓冲（编辑器 Console 读取）与日志文件
class Log {
public:
    static void Init(const std::string& logFile);
    static void Shutdown();

    static void Write(LogLevel level, const char* fmt, ...);

    static const std::vector<LogEntry>& Entries();
    static void Clear();

    static void SetConsoleEcho(bool on) { s_echo = on; }
    static int  CountAtLeast(LogLevel level);

private:
    static std::vector<LogEntry> s_entries;
    static std::mutex            s_mutex;
    static bool                  s_echo;
    static std::string           s_timeOrigin;
    static double                s_startTime;
};

// 可变参数模板包装，保持 printf 风格
template <typename... Args>
inline void LogInfo(const char* fmt, Args... args)  { Log::Write(LogLevel::Info,  fmt, args...); }
template <typename... Args>
inline void LogWarn(const char* fmt, Args... args)  { Log::Write(LogLevel::Warn,  fmt, args...); }
template <typename... Args>
inline void LogError(const char* fmt, Args... args) { Log::Write(LogLevel::Error, fmt, args...); }
template <typename... Args>
inline void LogTrace(const char* fmt, Args... args) { Log::Write(LogLevel::Trace, fmt, args...); }

} // namespace erm

// ------------------------------------------------------------------ 断言
#ifdef _DEBUG
#ifdef _MSC_VER
#include <intrin.h>
#define ER_DEBUGBREAK() __debugbreak()
#else
#define ER_DEBUGBREAK() ((void)0)
#endif
#else
#define ER_DEBUGBREAK() ((void)0) // Release 不中断，只记录日志
#endif

#define ER_ASSERT(cond, ...)                                                          \
    do {                                                                              \
        if (!(cond)) {                                                                \
            ::erm::Log::Write(::erm::LogLevel::Error, "ASSERT FAILED: %s (%s:%d)",    \
                              #cond, __FILE__, __LINE__);                             \
            ::erm::Log::Write(::erm::LogLevel::Error, __VA_ARGS__);                   \
            ER_DEBUGBREAK();                                                          \
        }                                                                             \
    } while (0)

#define ER_CORE_ASSERT ER_ASSERT

// HRESULT 检查：失败时记录并中断，绝不静默吞掉
#define ER_CHECK_HR(hr, msg)                                                          \
    do {                                                                              \
        HRESULT _er_hr = (hr);                                                        \
        if (FAILED(_er_hr)) {                                                        \
            ::erm::Log::Write(::erm::LogLevel::Error, "%s failed (hr=0x%08X) %s:%d",  \
                              msg, (unsigned)_er_hr, __FILE__, __LINE__);             \
            ER_DEBUGBREAK();                                                          \
        }                                                                             \
    } while (0)
