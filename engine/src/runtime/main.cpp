// ErMao Engine - 程序入口
//
// 发布版与开发版是同一个 exe，区别在启动方式（见 run.bat）：
//   * 发布版：GUI 子系统，双击不弹控制台，默认不写日志；
//   * 开发版：加 --dev 启动，复用（或新建）控制台并输出日志。
// 因此这里必须同时提供 main 与 WinMain：CMake 把 ErMao 标记为 WIN32_EXECUTABLE 后
// 由 WinMain 进入；被其他工具按控制台子系统链接时由 main 进入。
#include "runtime/Engine.h"

#include <windows.h>

#include <cstdlib>

namespace {

int ErmMain(int argc, char** argv) {
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // 控制台初始化（UTF-8 / 日志颜色 / 开发版控制台）在 Engine::Run 解析完参数后做：
    // 发布版没有控制台，直接设置只会失败且没有意义。
    erm::Engine engine;
    return engine.Run(argc, argv);
}

} // namespace

int main(int argc, char** argv) { return ErmMain(argc, argv); }

// GUI 子系统入口（CMake WIN32_EXECUTABLE）。
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return ErmMain(__argc, __argv); }
