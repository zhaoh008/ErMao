cmake_minimum_required(VERSION 3.20)
project(ErMao VERSION 0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC)
    message(FATAL_ERROR "ErMao 目前只支持 MSVC (Visual Studio 2022 x64)")
endif()

# 所有可执行文件/静态库统一输出到 build/bin
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${cfg} CFG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/lib)
endforeach()

add_compile_definitions(
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
    ERM_PLATFORM_WINDOWS=1
    IMGUI_DEFINE_MATH_OPERATORS
)
add_compile_options(/utf-8 /MP /W3 /permissive-)
add_compile_options($<$<CONFIG:Debug>:/Zi> $<$<CONFIG:Debug>:/Od>)

# ---------------------------------------------------------------- Dear ImGui
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

# ---------------------------------------------------------------- engine lib
file(GLOB_RECURSE ERMAO_ENGINE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/engine/src/*.cpp)
list(REMOVE_ITEM ERMAO_ENGINE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/engine/src/runtime/main.cpp)

add_library(ErMaoEngine STATIC ${ERMAO_ENGINE_SOURCES} ${IMGUI_SOURCES})
target_include_directories(ErMaoEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(ErMaoEngine PUBLIC
    d3d11 dxgi d3dcompiler dxguid
    user32 gdi32 ole32 windowscodecs shcore
)
# ImGui 的 DX11 后端需要链接 d3dcompiler（着色器运行时编译）
# 上面已经包含，这里只是说明依赖来源。

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
target_link_libraries(ErMao PRIVATE ErMaoEngine)
set_target_properties(ErMao PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# 把 shaders / assets 拷到可执行文件旁边，保证 exe 目录自包含
add_custom_command(TARGET ErMao POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/engine/shaders $<TARGET_FILE_DIR:ErMao>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/engine/assets  $<TARGET_FILE_DIR:ErMao>/assets
    COMMENT "copying shaders and assets"
)
