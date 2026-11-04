cmake_minimum_required(VERSION 3.20)
project(ErMaoWeb CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "请用 emcmake 配置：emcmake cmake -S web -B build-web")
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(ERMAO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(ERMAO_SRC ${ERMAO_ROOT}/engine/src)
set(IMGUI_DIR ${ERMAO_ROOT}/third_party/imgui)

set(ERMAO_WEB_SOURCES
    ${ERMAO_SRC}/core/Json.cpp
    ${ERMAO_SRC}/scene/Components.cpp
    ${ERMAO_SRC}/scene/GameObject.cpp
    ${ERMAO_SRC}/scene/World.cpp
    ${ERMAO_SRC}/renderer/Camera.cpp
    ${ERMAO_SRC}/physics/PhysicsWorldSim.cpp
    ${ERMAO_SRC}/ai/Agents.cpp
    ${ERMAO_SRC}/ai/BehaviorTree.cpp
    ${ERMAO_SRC}/ai/NavGrid.cpp
    ${ERMAO_SRC}/animation/Animation.cpp
    ${ERMAO_SRC}/animation/Skeleton.cpp
    ${ERMAO_SRC}/game/Survivors.cpp
    ${ERMAO_SRC}/game/SurvivorsUI.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebApp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebFileSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebTimer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebLog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebInput.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebAudio.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AssetManagerWeb.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/TextureWeb.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Procedural.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

add_executable(ErMaoWeb ${ERMAO_WEB_SOURCES})

target_include_directories(ErMaoWeb PRIVATE
    ${ERMAO_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${ERMAO_ROOT}/third_party/stb
)

target_compile_definitions(ErMaoWeb PRIVATE
    ERMAO_WEB=1
    IMGUI_DEFINE_MATH_OPERATORS
    SDL_MAIN_HANDLED
)

# Emscripten + SDL2（WebGL 1.0 / GLES2，优先微信小游戏兼容性）
target_compile_options(ErMaoWeb PRIVATE -sUSE_SDL=2)
target_link_options(ErMaoWeb PRIVATE
    -sUSE_SDL=2
    -sMIN_WEBGL_VERSION=1
    -sMAX_WEBGL_VERSION=1
    -sFULL_ES2=1
    -sALLOW_MEMORY_GROWTH=1
    # 128MB 初始堆对 iOS 小游戏的 wasm 运行时过于激进；游戏实际只用十几 MB。
    # 初始堆降到 32MB，并把最大堆限制在 256MB（避免声明 2GB 上限）。
    -sINITIAL_MEMORY=33554432
    -sMAXIMUM_MEMORY=268435456
    -sSTACK_SIZE=2097152
    -sFORCE_FILESYSTEM=1
    -sNO_EXIT_RUNTIME=1
    -sASSERTIONS=1
    -sENVIRONMENT=web
    -sEXPORTED_FUNCTIONS=['_main']
    -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']
    --preload-file=${ERMAO_ROOT}/engine/assets@/assets
)

set_target_properties(ErMaoWeb PROPERTIES
    OUTPUT_NAME game
    SUFFIX .js
)

add_custom_command(TARGET ErMaoWeb POST_BUILD
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/patch_webgl_glue.py $<TARGET_FILE:ErMaoWeb>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/www/index.html
            $<TARGET_FILE_DIR:ErMaoWeb>/index.html
    COMMENT "patching WeChat-safe glue and copying web shell"
)
