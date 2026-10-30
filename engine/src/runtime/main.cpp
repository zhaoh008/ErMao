// ErMao Engine - 程序入口
#include "runtime/Engine.h"

#include <windows.h>

int main(int argc, char** argv) {
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ::SetConsoleOutputCP(CP_UTF8);
    // 打开控制台 VT 转义支持（日志着色）
    HANDLE h = ::GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (::GetConsoleMode(h, &mode)) ::SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    erm::Engine engine;
    return engine.Run(argc, argv);
}
