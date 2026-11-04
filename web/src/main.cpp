// ErMao Engine - Web / 微信小游戏入口
#include "WebApp.h"

#include <emscripten/emscripten.h>

int main() {
    static erm::WebApp app;
    if (!app.Init()) return 1;
    // 浏览器/微信都要求非阻塞主循环：交给 requestAnimationFrame 驱动。
    emscripten_set_main_loop_arg([](void* arg) { static_cast<erm::WebApp*>(arg)->Frame(); }, &app, 0, 1);
    return 0;
}
