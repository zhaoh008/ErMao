// ErMao Engine - Web 日志（浏览器控制台 + 可选写入 MEMFS）
#include "core/Log.h"
#include "core/Timer.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace erm {

std::vector<LogEntry> Log::s_entries;
std::mutex            Log::s_mutex;
bool                  Log::s_echo = true;
std::string           Log::s_timeOrigin;
double                Log::s_startTime = 0.0;

namespace {
const char* LevelTag(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

FILE* g_logFile = nullptr;
} // namespace

void Log::Init(const std::string& logFile) {
    s_startTime = Timer::Now();
    s_echo = true;
    if (!logFile.empty()) {
        g_logFile = std::fopen(logFile.c_str(), "w");
        if (g_logFile) {
            std::time_t t = std::time(nullptr);
            char buf[64]{};
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
            std::fprintf(g_logFile, "=== ErMao Engine web log %s\n", buf);
        }
    }
    Write(LogLevel::Info, "Log initialized (web, file: %s)", logFile.empty() ? "<none>" : logFile.c_str());
}

void Log::Shutdown() {
    if (g_logFile) {
        std::fflush(g_logFile);
        std::fclose(g_logFile);
        g_logFile = nullptr;
    }
}

void Log::Write(LogLevel level, const char* fmt, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    const double now = Timer::Now() - s_startTime;
    {
        std::scoped_lock lock(s_mutex);
        if (s_entries.size() > 4096) s_entries.erase(s_entries.begin(), s_entries.begin() + 1024);
        s_entries.push_back(LogEntry{level, buffer, now});
    }

    if (s_echo) {
        std::fprintf(stderr, "[%7.3f][%s] %s\n", now, LevelTag(level), buffer);
        std::fflush(stderr);
    }
    if (g_logFile) {
        std::fprintf(g_logFile, "[%7.3f][%s] %s\n", now, LevelTag(level), buffer);
        std::fflush(g_logFile);
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
    for (const auto& entry : s_entries) {
        if (entry.level >= level) ++n;
    }
    return n;
}

} // namespace erm
