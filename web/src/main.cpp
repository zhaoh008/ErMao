// ErMao Engine - Web / 微信小游戏入口
#include "WebApp.h"

#include <emscripten/emscripten.h>

int main() {
    static erm::WebApp app;
    if (!app.Init()) {
        // Init 走的是 return false 而不是抛异常，微信不会弹默认错误页；主动把
        // 失败暴露到实时日志并显示在手机上，避免只看到黑屏/卡封面。
        EM_ASM({
            try {
                if (typeof GameGlobal !== 'undefined' && GameGlobal.__ermaoReportError) {
                    GameGlobal.__ermaoReportError("init.failed", "WebApp::Init() returned false; check the stage logs");
                } else if (typeof wx !== 'undefined' && wx.showModal) {
                    var opts = {};
                    opts.title = 'ErMao 启动失败';
                    opts.content = '初始化失败，请查看实时日志';
                    opts.showCancel = false;
                    wx.showModal(opts);
                }
            } catch (e) {}
        });
        return 1;
    }
    // 浏览器/微信都要求非阻塞主循环：交给 requestAnimationFrame 驱动。
    emscripten_set_main_loop_arg([](void* arg) { static_cast<erm::WebApp*>(arg)->Frame(); }, &app, 0, 1);
    return 0;
}
