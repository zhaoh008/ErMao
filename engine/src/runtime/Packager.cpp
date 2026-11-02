// ErMao Engine - 打包实现
#include "runtime/Packager.h"
#include "core/FileSystem.h"
#include "core/Log.h"

#include <filesystem>

namespace erm {

namespace {

std::string DefaultOutputRoot() {
#ifdef ERMAO_SOURCE_DIR
    return FileSystem::Join(ERMAO_SOURCE_DIR, "dist");
#else
    return FileSystem::Join(FileSystem::ExecutableDir(), "dist");
#endif
}

std::string ProjectRoot() {
#ifdef ERMAO_SOURCE_DIR
    return ERMAO_SOURCE_DIR;
#else
    return FileSystem::ExecutableDir();
#endif
}

bool CopyTree(const std::string& source, const std::string& destination) {
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::u8path(source), ec)) return false;
    std::filesystem::create_directories(std::filesystem::u8path(destination), ec);
    std::filesystem::copy(std::filesystem::u8path(source), std::filesystem::u8path(destination),
                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                          ec);
    if (ec) {
        LogError("复制目录失败 %s -> %s: %s", source.c_str(), destination.c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

bool WriteText(const std::string& path, const std::string& content) {
    return FileSystem::WriteTextFile(path, content);
}

} // namespace

PackageResult Packager::PackageWindows(const std::string& outputRoot) {
    PackageResult result;
    const std::string root = outputRoot.empty() ? DefaultOutputRoot() : outputRoot;
    const std::string target = FileSystem::Join(root, "ErMao-Windows");
    result.outputDir = target;

    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::u8path(target), ec);
    std::filesystem::create_directories(std::filesystem::u8path(target), ec);

    const std::string exeDir = FileSystem::ExecutableDir();
    const std::string exeName = "ErMao.exe";

    if (!FileSystem::CopyFileTo(FileSystem::Join(exeDir, exeName), FileSystem::Join(target, exeName))) {
        result.message = "复制 ErMao.exe 失败";
        return result;
    }
    if (!CopyTree(FileSystem::Join(exeDir, "shaders"), FileSystem::Join(target, "shaders"))) {
        result.message = "复制 shaders 失败";
        return result;
    }
    if (!CopyTree(FileSystem::Join(exeDir, "assets"), FileSystem::Join(target, "assets"))) {
        result.message = "复制 assets 失败";
        return result;
    }

    // 发布版：GUI 子系统 + 默认无日志，双击只出现游戏/编辑器窗口；
    // 开发版：同一个 exe 加 --dev，复用 run-dev.bat 的控制台输出日志。
    WriteText(FileSystem::Join(target, "run.bat"),
              "@echo off\r\n"
              "rem Release: no console window, no log file.\r\n"
              "start \"\" \"%~dp0ErMao.exe\" %*\r\n");
    WriteText(FileSystem::Join(target, "run-dev.bat"),
              "@echo off\r\n"
              "rem Development: keep this console open for the log output.\r\n"
              "start \"\" /wait \"%~dp0ErMao.exe\" --dev %*\r\n");
    WriteText(FileSystem::Join(root, "GENERATED_DO_NOT_EDIT.txt"),
              "dist/ 是打包产物，不要手改。\r\n改源码请改工程目录，然后重新执行 文件 → 打包。\r\n");
    WriteText(FileSystem::Join(target, "README.txt"),
              "ErMao Engine - Windows package\r\n"
              "\r\n"
              "1. 双击 run.bat（发布版：无控制台、无日志）\r\n"
              "2. 排错时双击 run-dev.bat（开发版：控制台日志）\r\n"
              "3. 默认进入 2D 幸存者 Demo；「关卡」菜单可切换到 3D 幸存者 Demo\r\n"
              "4. 编辑器：左键拾取，WASD/QE 移动，F 聚焦，F5 直接开局\r\n"
              "5. 视角：V 切换 透视 / 正视图 / 俯视；设置面板可调镜头速度\r\n"
              "\r\n"
              "assets/ 下是场景与模型，shaders/ 是运行时编译的 HLSL，删改前请先备份。\r\n");

    result.success = true;
    result.message = "Windows 包已生成：" + target;
    LogInfo("[打包] %s", result.message.c_str());
    return result;
}

PackageResult Packager::PackageLinuxSource(const std::string& outputRoot) {
    PackageResult result;
    const std::string root = outputRoot.empty() ? DefaultOutputRoot() : outputRoot;
    const std::string target = FileSystem::Join(root, "ErMao-Linux-Source");
    result.outputDir = target;

    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::u8path(target), ec);
    std::filesystem::create_directories(std::filesystem::u8path(target), ec);

    const std::string project = ProjectRoot();
    // 源码 + 第三方 + 资源 + 脚本
    if (!CopyTree(FileSystem::Join(project, "engine"), FileSystem::Join(target, "engine"))) {
        result.message = "复制 engine 源码失败（需要从工程目录运行）";
        return result;
    }
    CopyTree(FileSystem::Join(project, "third_party"), FileSystem::Join(target, "third_party"));
    CopyTree(FileSystem::Join(project, "文档"), FileSystem::Join(target, "文档"));
    FileSystem::CopyFileTo(FileSystem::Join(project, "CMakeLists.txt"), FileSystem::Join(target, "CMakeLists.txt"));

    WriteText(FileSystem::Join(target, "build.sh"),
              "#!/usr/bin/env bash\n"
              "# ErMao v0.3 的图形/窗口层是 Win32 + D3D11，Linux 上无法直接构建。\n"
              "# 这个脚本只做环境检查，避免让 cmake 抛出一个看不懂的 FATAL_ERROR。\n"
              "set -e\n"
              "if [ \"$(uname -s)\" != \"Linux\" ]; then\n"
              "  echo \"这是一个 Linux 源码包；请在 Linux 上运行。\"\n"
              "fi\n"
              "echo \"\"\n"
              "echo \"ErMao 目前没有 Linux 运行时：需要先补 Vulkan/OpenGL 后端。\"\n"
              "echo \"步骤见 README-LINUX.md：\"\n"
              "echo \"  1) 平台层换 GLFW/SDL2    2) rhi 增加 Vulkan/OpenGL\"\n"
              "echo \"  3) HLSL 转 SPIR-V/GLSL   4) ImGui 换 glfw+vulkan 后端\"\n"
              "echo \"  5) 打包 assets/shaders\"\n"
              "echo \"\"\n"
              "echo \"补完后端后可执行：cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j\"\n"
              "exit 0\n");

    WriteText(FileSystem::Join(target, "README-LINUX.md"),
              "# ErMao Linux 源码包\n\n"
              "## 现状（重要）\n\n"
              "ErMao v0.2 的图形与窗口层是 **Win32 + D3D11**，没有 Linux 运行时，\n"
              "所以这个包不是可直接运行的二进制，而是 **源码 + 资源 + 构建脚本**。\n\n"
              "## 在 Linux 上跑起来需要补什么\n\n"
              "1. 平台层：用 GLFW/SDL2 替换 `platform/Window`（输入、文件对话框也要替换）；\n"
              "2. 图形层：新增 Vulkan 或 OpenGL 后端，实现 `rhi/` 下的 Device/Buffer/Texture/RenderTarget/Shader；\n"
              "3. 着色器：D3D11 的 HLSL 需要翻译成 GLSL/SPIR-V（或直接用 DXC → SPIR-V 保留 HLSL）；\n"
              "4. UI：ImGui 换成 `imgui_impl_glfw` + `imgui_impl_vulkan/opengl3`；\n"
              "5. 打包：把 `assets/`、`shaders/` 与可执行文件放在同一目录。\n\n"
              "引擎的场景、序列化、物理、AI、玩法代码与图形后端无关，可以直接复用。\n");

    result.success = true;
    result.message = "Linux 源码包已生成（需自行补 Vulkan/OpenGL 后端）：" + target;
    LogWarn("[打包] %s", result.message.c_str());
    return result;
}


PackageResult Packager::PackageGameWindows(const std::string& gameName, const std::string& level,
                                           const std::string& outputRoot) {
    PackageResult result;
    const std::string root = outputRoot.empty() ? DefaultOutputRoot() : outputRoot;
    const std::string target = FileSystem::Join(root, gameName + "-Windows");
    result.outputDir = target;

    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::u8path(target), ec);
    std::filesystem::create_directories(std::filesystem::u8path(target), ec);

    const std::string exeDir = FileSystem::ExecutableDir();
    const std::string exeTarget = FileSystem::Join(target, gameName + ".exe");
    if (!FileSystem::CopyFileTo(FileSystem::Join(exeDir, "ErMao.exe"), exeTarget)) {
        result.message = "复制 ErMao.exe 失败";
        return result;
    }
    if (!CopyTree(FileSystem::Join(exeDir, "shaders"), FileSystem::Join(target, "shaders")) ||
        !CopyTree(FileSystem::Join(exeDir, "assets"), FileSystem::Join(target, "assets"))) {
        result.message = "复制 shaders/assets 失败";
        return result;
    }

    // 游戏启动配置：exe 名称无关，game.cfg 决定默认关卡与模式
    WriteText(FileSystem::Join(target, "game.cfg"),
              "game=" + gameName + "\r\nlevel=" + level + "\r\neditor=0\r\nplay=1\r\n");

    WriteText(FileSystem::Join(target, "run.bat"),
              "@echo off\r\n"
              "rem Release: no console window, no log file.\r\n"
              "start \"\" \"%~dp0" + gameName + ".exe\" %*\r\n");
    WriteText(FileSystem::Join(target, "run-dev.bat"),
              "@echo off\r\n"
              "rem Development: keep this console open for the log output.\r\n"
              "start \"\" /wait \"%~dp0" + gameName + ".exe\" --dev %*\r\n");
    WriteText(FileSystem::Join(target, "README.txt"),
              gameName + " - Windows 游戏包\r\n"
              "\r\n"
              "双击 run.bat 启动（发布版：无控制台、无日志）。\r\n"
              "排错时双击 run-dev.bat（开发版：控制台输出日志）。\r\n"
              "本包包含游戏本身（引擎 + 资源），不是只打包引擎。\r\n"
              "\r\n"
              "操作：WASD 移动，自动攻击，Esc 暂停。\r\n"
              "assets/ 是游戏资源，shaders/ 是运行时编译的 HLSL；修改前请备份。\r\n");

    // 2D 素材许可（如果存在）
    const std::string license = FileSystem::Join(exeDir, "assets/sprites/LICENSE-2D-ASSETS.txt");
    if (FileSystem::Exists(license)) {
        FileSystem::CopyFileTo(license, FileSystem::Join(target, "LICENSE-2D-ASSETS.txt"));
    }

    result.success = true;
    result.message = "Windows 游戏包已生成：" + target;
    LogInfo("[打包] %s", result.message.c_str());
    return result;
}

PackageResult Packager::PackageGameLinuxSource(const std::string& gameName, const std::string& level,
                                               const std::string& outputRoot) {
    PackageResult result;
    const std::string root = outputRoot.empty() ? DefaultOutputRoot() : outputRoot;
    const std::string target = FileSystem::Join(root, gameName + "-Linux-Source");
    result.outputDir = target;

    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::u8path(target), ec);
    std::filesystem::create_directories(std::filesystem::u8path(target), ec);

    const std::string project = ProjectRoot();
    if (!CopyTree(FileSystem::Join(project, "engine"), FileSystem::Join(target, "engine"))) {
        result.message = "复制 engine 源码失败（需要从工程目录运行）";
        return result;
    }
    CopyTree(FileSystem::Join(project, "third_party"), FileSystem::Join(target, "third_party"));
    CopyTree(FileSystem::Join(project, "文档"), FileSystem::Join(target, "文档"));
    FileSystem::CopyFileTo(FileSystem::Join(project, "CMakeLists.txt"), FileSystem::Join(target, "CMakeLists.txt"));
    WriteText(FileSystem::Join(target, "game.cfg"),
              "game=" + gameName + "\r\nlevel=" + level + "\r\neditor=0\r\nplay=1\r\n");
    WriteText(FileSystem::Join(target, "build.sh"),
              "#!/usr/bin/env bash\n"
              "set -e\n"
              "echo \"这是 " + gameName + " 的 Linux 源码包。\"\n"
              "echo \"引擎目前只有 Win32 + D3D11 后端，Linux 上还不能直接构建出可执行文件。\"\n"
              "echo \"需要补：GLFW/SDL 平台层、Vulkan/OpenGL 的 rhi、HLSL->SPIR-V、ImGui 后端。\"\n"
              "echo \"补完后：cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j\"\n"
              "exit 0\n");
    WriteText(FileSystem::Join(target, "README-LINUX.md"),
              "# " + gameName + " - Linux 源码包\n\n"
              "游戏逻辑（2D 精灵、玩法、HUD、菜单）与资源都在这个包里，但引擎的图形/窗口是 Win32 + D3D11，\n"
              "因此本包**不含 Linux 可执行文件**。要得到 Linux 版游戏，需要先补平台与图形后端：\n\n"
              "1. 平台层：GLFW/SDL2 替换 `platform/Window` 与输入；\n"
              "2. 图形层：`rhi/` 增加 Vulkan 或 OpenGL 实现；\n"
              "3. 着色器：HLSL 转 SPIR-V/GLSL（或直接 DXC → SPIR-V）；\n"
              "4. UI：ImGui 换 `imgui_impl_glfw` + `imgui_impl_vulkan`；\n"
              "5. 用 `game.cfg` 指定默认关卡（" + level + "）。\n\n"
              "游戏资源在 `engine/assets/`，构建入口是 `CMakeLists.txt`。\n");

    result.success = true;
    result.message = "Linux 游戏源码包已生成（需自行补图形后端）：" + target;
    LogWarn("[打包] %s", result.message.c_str());
    return result;
}
} // namespace erm
