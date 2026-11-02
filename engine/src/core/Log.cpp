// ErMao Engine - 日志系统实现
#include "core/Log.h"
#include "core/Timer.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace erm {

std::vector<LogEntry> Log::s_entries;
std::mutex            Log::s_mutex;
bool                  Log::s_echo = false; // 发布版不往控制台回显；--dev 启动时由 Engine::Run 打开
std::string           Log::s_timeOrigin;
double                Log::s_startTime = 0.0;

static const char* LevelTag(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

static FILE* g_logFile = nullptr;

void Log::Init(const std::string& logFile) {
    s_startTime = Timer::Now();
    if (!logFile.empty()) {
        fopen_s(&g_logFile, logFile.c_str(), "w");
        if (g_logFile) {
            std::time_t t = std::time(nullptr);
            char buf[64]{};
            ctime_s(buf, sizeof(buf), &t);
            fprintf(g_logFile, "=== ErMao Engine log %s", buf);
        }
    }
    Write(LogLevel::Info, "Log initialized (file: %s)", logFile.empty() ? "<none>" : logFile.c_str());
}

void Log::Shutdown() {
    if (g_logFile) {
        fflush(g_logFile);
        fclose(g_logFile);
        g_logFile = nullptr;
    }
}

void Log::Write(LogLevel level, const char* fmt, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    const double now = Timer::Now() - s_startTime;

    {
        std::scoped_lock lock(s_mutex);
        if (s_entries.size() > 4096) s_entries.erase(s_entries.begin(), s_entries.begin() + 1024);
        s_entries.push_back(LogEntry{level, buffer, now});
    }

    if (s_echo) {
        // 控制台按等级着色（Windows 10 控制台支持 VT 序列）
        const char* color = "";
        const char* reset = "";
        switch (level) {
            case LogLevel::Trace: break;
            case LogLevel::Info:  color = "\x1b[37m"; break;
            case LogLevel::Warn:  color = "\x1b[33m"; break;
            case LogLevel::Error: color = "\x1b[31m"; break;
        }
        if (color[0]) reset = "\x1b[0m";
        std::printf("%s[%7.3f][%s] %s%s\n", color, now, LevelTag(level), buffer, reset);
        std::fflush(stdout);
    }
    if (g_logFile) {
        std::fprintf(g_logFile, "[%7.3f][%s] %s\n", now, LevelTag(level), buffer);
        std::fflush(g_logFile);
    }
    if (level == LogLevel::Error) {
        ::OutputDebugStringA(buffer);
        ::OutputDebugStringA("\n");
    }
}

const std::vector<LogEntry>& Log::Entries() { return s_entries; }

void Log::Clear() {
    std::scoped_lock lock(s_mutex);
    s_entries.clear();
}

int Log::CountAtLeast(LogLevel level) {
    std::scoped_lock lock(s_mutex);
    int n = 0;
    for (const auto& e : s_entries)
        if (e.level >= level) ++n;
    return n;
}

} // namespace erm
