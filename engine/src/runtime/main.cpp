// ErMao Engine - 程序入口
//
// 发布版与开发版是同一个 exe，区别在启动方式（见 run.bat）：
//   * 发布版：GUI 子系统，双击不弹控制台，默认不写日志；
//   * 开发版：加 --dev 启动，复用（或新建）控制台并输出日志。
// 因此这里必须同时提供 main 与 WinMain：CMake 把 ErMao 标记为 WIN32_EXECUTABLE 后
// 由 WinMain 进入；被其他工具按控制台子系统链接时由 main 进入。
#include "runtime/Engine.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdlib>
#include <string>
#include <vector>

namespace {

int ErmMain(int argc, char** argv) {
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // 控制台初始化（UTF-8 / 日志颜色 / 开发版控制台）在 Engine::Run 解析完参数后做：
    // 发布版没有控制台，直接设置只会失败且没有意义。
    erm::Engine engine;
    return engine.Run(argc, argv);
}

std::string WideToUtf8(const wchar_t* text) {
    if (!text || !*text) return {};
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string out(static_cast<size_t>(size) - 1, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}

// 从 GetCommandLineW 重新解析参数并转成 UTF-8，避免中文 Windows（ACP=936）
// 下 CRT 的 ANSI argv/__argv 把中文路径变成非 UTF-8 字节，导致 --screenshot /
// --script / --json-out / --import 等参数在 FileSystem 的 CP_UTF8 转换处失效。
int ErmMainFromCommandLine() {
    int argc = 0;
    LPWSTR* wargv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!wargv) return ErmMain(__argc, __argv); // 解析失败时退回 CRT 参数
    std::vector<std::string> args;
    args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) args.push_back(WideToUtf8(wargv[i]));
    ::LocalFree(wargv);
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (auto& arg : args) argv.push_back(arg.data());
    return ErmMain(static_cast<int>(argv.size()), argv.data());
}

} // namespace

int main() { return ErmMainFromCommandLine(); }

// GUI 子系统入口（CMake WIN32_EXECUTABLE）。
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return ErmMainFromCommandLine(); }
