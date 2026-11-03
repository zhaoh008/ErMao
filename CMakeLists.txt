cmake_minimum_required(VERSION 3.20)
project(ErMao VERSION 0.8.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------- 编译器要求
# ErMao 只支持 MSVC（D3D11 / WIC / XAudio2 / HLSL 都依赖 Windows SDK 与 MSVC ABI）。
# CLion 默认可能选 MinGW 或没有工具链，这里给出可操作的修复提示。
if(NOT MSVC)
    message(FATAL_ERROR
        "ErMao 需要 MSVC（Visual Studio 2022 x64），当前编译器是 ${CMAKE_CXX_COMPILER_ID}。\n"
        "CLion 修复步骤：\n"
        "  1) Settings -> Build, Execution, Deployment -> Toolchains：选择 Visual Studio（不要用 MinGW）；\n"
        "  2) Settings -> Build -> CMake：Generator 选 \"Visual Studio 17 2022\"（推荐）或 Ninja；\n"
        "     也可以直接选仓库里的 CMakePresets.json 预设 vs2022；\n"
        "  3) 删除旧的 cmake-build-* 目录后重新 Reload CMake Project。\n"
        "命令行：运行 setup.bat，或用 \"x64 Native Tools Command Prompt for VS 2022\"。\n"
        "详见 文档/版本文档/构建与运行.md")
endif()

# 所有可执行文件/静态库统一输出到 build/bin
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${cfg} CFG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/bin)
    # 静态库按配置分目录：run.bat 在 Release/Debug 之间切换时，避免 VS 多配置
    # 生成器把另一个配置的 .lib 留在 build/lib 导致 LNK4098（Debug/Release CRT 混链）。
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/lib/${cfg})
endforeach()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_definitions(
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
    ERM_PLATFORM_WINDOWS=1
    IMGUI_DEFINE_MATH_OPERATORS
)
if(MSVC)
    add_compile_options(/utf-8 /W3 /permissive-)
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        add_compile_options(/MP) # Ninja 下由 jobserver 并行，不需要 /MP
    endif()
endif()
add_compile_options($<$<CONFIG:Debug>:/Zi> $<$<CONFIG:Debug>:/Od>)

# ---------------------------------------------------------------- Dear ImGui
# 单独成 target：只把 include 目录与 d3d11 依赖"私有"地传给引擎库，
# 避免 IMGUI 的宏/符号泄漏到所有链接 ErMaoEngine 的目标（审核 P3-39）。
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(imgui PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
target_link_libraries(imgui PUBLIC d3d11 dxgi)

# ---------------------------------------------------------------- engine lib
file(GLOB_RECURSE ERMAO_ENGINE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/engine/src/*.cpp)
list(REMOVE_ITEM ERMAO_ENGINE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/engine/src/runtime/main.cpp)

add_library(ErMaoEngine STATIC ${ERMAO_ENGINE_SOURCES})
target_include_directories(ErMaoEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/engine/src)
target_compile_definitions(ErMaoEngine PRIVATE ERMAO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ErMaoEngine
    PUBLIC d3d11 dxgi d3dcompiler dxguid user32 gdi32 ole32 windowscodecs shcore xaudio2
    PRIVATE imgui
)

# 只为 engine 源码做分组，ImGui 走默认分组
set(ERMAO_SOURCE_GROUP_FILES "")
foreach(src ${ERMAO_ENGINE_SOURCES})
    if(src MATCHES "engine/src")
        list(APPEND ERMAO_SOURCE_GROUP_FILES ${src})
    endif()
endforeach()
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/engine/src FILES ${ERMAO_SOURCE_GROUP_FILES})

# ---------------------------------------------------------------- executable
add_executable(ErMao ${CMAKE_CURRENT_SOURCE_DIR}/engine/src/runtime/main.cpp)

# ---------------------------------------------------------------- Vulkan（可选）
# 没有 Vulkan SDK 的机器必须能正常编译：ERMAO_HAS_VULKAN 控制源码实现，
# 未安装时 VulkanBackend.cpp 退化为空实现（--vulkan-test 提示不可用，D3D11 不受影响）。
set(ERMAO_VULKAN_SDK "$ENV{VULKAN_SDK}")
if(ERMAO_VULKAN_SDK AND EXISTS "${ERMAO_VULKAN_SDK}/Include/vulkan/vulkan.h")
    message(STATUS "Vulkan SDK: ${ERMAO_VULKAN_SDK}（启用 Vulkan 后端）")
    target_compile_definitions(ErMaoEngine PRIVATE ERMAO_HAS_VULKAN=1)
    target_include_directories(ErMaoEngine PRIVATE "${ERMAO_VULKAN_SDK}/Include")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        target_link_directories(ErMaoEngine PRIVATE "${ERMAO_VULKAN_SDK}/Lib")
        target_link_directories(ErMao PRIVATE "${ERMAO_VULKAN_SDK}/Lib")
    else()
        target_link_directories(ErMaoEngine PRIVATE "${ERMAO_VULKAN_SDK}/Lib32")
        target_link_directories(ErMao PRIVATE "${ERMAO_VULKAN_SDK}/Lib32")
    endif()
    target_link_libraries(ErMaoEngine PUBLIC vulkan-1)
else()
    message(STATUS "Vulkan SDK 未安装：跳过 Vulkan 后端（D3D11 仍可用）")
endif()
# shell32: runtime/main.cpp 用 CommandLineToArgvW 从 GetCommandLineW 还原 UTF-8 参数。
target_link_libraries(ErMao PRIVATE ErMaoEngine shell32)
# GUI 子系统：发布版双击/run.bat 不再额外弹一个 CMD 控制台。
# 开发版（run.bat dev / --dev）在运行时自己挂接或新建控制台并输出日志，入口见 main.cpp 的 WinMain。
set_target_properties(ErMao PROPERTIES
    WIN32_EXECUTABLE TRUE
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# 把 shaders / assets 拷到可执行文件旁边，保证 exe 目录自包含
add_custom_command(TARGET ErMao POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/engine/shaders $<TARGET_FILE_DIR:ErMao>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/engine/assets  $<TARGET_FILE_DIR:ErMao>/assets
    COMMENT "copying shaders and assets"
)
