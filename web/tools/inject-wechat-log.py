#!/usr/bin/env python3
"""Inject the ErMao WeChat startup logger as the first ES module.

WeChat mini-games on iOS do not expose a console for preview builds and a
startup exception only shows the generic "程序遇到错误" page. This logger is
installed *before* wx-game-kit/framework so it can:

  * forward errors/system info/stage markers to wx.getRealtimeLogManager
    (mp.weixin.qq.com -> 运维中心 -> 实时日志, filter `ermao`);
  * enable vConsole via wx.setEnableDebug for the diagnostic package;
  * show the first uncaught error in a wx.showModal on the phone, so a
    screenshot is enough even without a cable or vConsole;
  * record the iOS/Android DOM, canvas and WebAssembly capabilities, which is
    how we pinpoint missing browser APIs in the WeChat iOS adapter.

Enabled with WECHAT_DEBUG_LOG=1 by package-wechat.sh.
"""
import re
import sys

MARKER = "/* ERMAO_WECHAT_LOG */"

LOGGER = r"""/* ERMAO_WECHAT_LOG */
(function () {
  var G = (typeof GameGlobal !== 'undefined') ? GameGlobal : ((typeof window !== 'undefined') ? window : {});
  var rt = null;
  try {
    if (typeof wx !== 'undefined' && wx.getRealtimeLogManager) {
      rt = wx.getRealtimeLogManager();
      try { rt.setFilterMsg('ermao'); } catch (e) {}
      try { rt.addFilterMsg('wechat'); } catch (e) {}
    }
  } catch (e) {}

  function toText(args) {
    var out = [];
    for (var i = 0; i < args.length; i++) {
      var a = args[i];
      if (typeof a === 'string') out.push(a);
      else { try { out.push(JSON.stringify(a)); } catch (e) { out.push(String(a)); } }
    }
    return out.join(' ');
  }
  function send(level, tag, args) {
    var text = toText(args);
    if (rt) {
      try { var fn = rt[level] || rt.info; fn.call(rt, '[ERMAO ' + tag + '] ' + text); } catch (e) {}
    }
    // Console output must go through console.log only: console.error is
    // overridden below and forwards to send(), so using it here would recurse.
    try {
      if (typeof console !== 'undefined' && console.log) console.log('[ERMAO ' + tag + '] ' + text);
    } catch (e) {}
    return text;
  }
  var modalShown = false;
  function report(kind, detail) {
    var text = String(detail === undefined || detail === null ? '' : detail).slice(0, 1500);
    send('error', kind, [text]);
    try { if (typeof wx !== 'undefined' && wx.setStorageSync) wx.setStorageSync('ermao_last_error', kind + ': ' + text); } catch (e) {}
    if (modalShown) return;
    modalShown = true;
    try {
      if (typeof wx !== 'undefined' && wx.showModal) {
        wx.showModal({
          title: 'ErMao 启动错误 (' + kind + ')',
          content: text.slice(0, 600) || '无错误文本，请查看实时日志',
          showCancel: false
        });
      }
    } catch (e) {}
  }
  G.__ermaoReportError = report;
  G.__ermaoStage = function (name) { send('info', 'stage', [name]); };
  G.__ermaoLastError = '';

  try { if (typeof wx !== 'undefined' && wx.setEnableDebug) wx.setEnableDebug({ enableDebug: true }); } catch (e) {}

  var sys = {};
  try { sys = (typeof wx !== 'undefined' && wx.getSystemInfoSync) ? wx.getSystemInfoSync() : {}; } catch (e) {}
  send('info', 'sys', [JSON.stringify({
    platform: sys.platform, system: sys.system, version: sys.version, SDKVersion: sys.SDKVersion,
    model: sys.model, brand: sys.brand, pixelRatio: sys.pixelRatio, memorySize: sys.memorySize,
    benchmarkLevel: sys.benchmarkLevel, windowWidth: sys.windowWidth, windowHeight: sys.windowHeight,
    screenWidth: sys.screenWidth, screenHeight: sys.screenHeight
  })]);

  function caps() {
    var c = {};
    try { c.highPerf = (typeof GameGlobal !== 'undefined') ? GameGlobal.isIOSHighPerformanceMode : undefined; } catch (e) {}
    try { c.highPerfPlus = (typeof GameGlobal !== 'undefined') ? GameGlobal.isIOSHighPerformanceModePlus : undefined; } catch (e) {}
    try { c.wx = typeof wx; } catch (e) { c.wx = 'throw'; }
    try { c.canvas = typeof canvas; } catch (e) { c.canvas = 'throw'; }
    try { c.module = typeof Module; } catch (e) { c.module = 'throw'; }
    try { c.moduleCanvas = (typeof Module !== 'undefined' && Module && Module.canvas) ? typeof Module.canvas : 'none'; } catch (e) { c.moduleCanvas = 'throw'; }
    try { c.rect = (typeof Module !== 'undefined' && Module && Module.canvas) ? typeof Module.canvas.getBoundingClientRect : 'none'; } catch (e) { c.rect = 'throw'; }
    try { c.dpr = typeof devicePixelRatio; } catch (e) { c.dpr = 'throw'; }
    try { c.doc = typeof document; } catch (e) { c.doc = 'throw'; }
    try { c.docBody = (typeof document !== 'undefined') ? typeof document.body : 'no-doc'; } catch (e) { c.docBody = 'throw'; }
    try { c.wasm = typeof WebAssembly; } catch (e) { c.wasm = 'throw'; }
    try { c.wxWasm = (typeof WXWebAssembly !== 'undefined') ? 'yes' : 'no'; } catch (e) { c.wxWasm = 'throw'; }
    try { c.raf = typeof requestAnimationFrame; } catch (e) { c.raf = 'throw'; }
    try { c.loader = (typeof WXGameKit !== 'undefined' && WXGameKit.loader) ? 'yes' : 'no'; } catch (e) { c.loader = 'throw'; }
    return c;
  }
  send('info', 'caps.early', [JSON.stringify(caps())]);
  try { setTimeout(function () { send('info', 'caps.start', [JSON.stringify(caps())]); }, 0); } catch (e) {}

  try {
    if (typeof wx !== 'undefined') {
      if (wx.onError) wx.onError(function (res) {
        report('wx.onError', (res && (res.message || res.errMsg)) || JSON.stringify(res));
      });
      if (wx.onUnhandledRejection) wx.onUnhandledRejection(function (res) {
        var r = res && res.reason;
        report('wx.rejection', (r && (r.stack || r.message)) || String(r));
      });
    }
  } catch (e) {}
  try {
    if (G && G.addEventListener) {
      G.addEventListener('error', function (e) {
        report('uncaught', (e && e.error && e.error.stack) || (e && e.message) || 'unknown');
      });
      G.addEventListener('unhandledrejection', function (e) {
        var r = e && e.reason;
        report('rejection', (r && (r.stack || r.message)) || String(r));
      });
    }
  } catch (e) {}
  try {
    if (G) G.onerror = function (message, source, lineno, colno, error) {
      report('onerror', (error && error.stack) || [message, source, lineno, colno].join(' '));
    };
  } catch (e) {}
  try {
    if (typeof console !== 'undefined' && console.error) {
      var origError = console.error;
      console.error = function () {
        try { send('error', 'console.error', arguments); } catch (e) {}
        try { return origError.apply(console, arguments); } catch (e2) {}
      };
    }
  } catch (e) {}

  send('info', 'logger', ['installed']);
})();
"""


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def write(path, text):
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: inject-wechat-log.py <minigame-dir>", file=sys.stderr)
        return 2
    root = sys.argv[1].rstrip("/")
    game_js = root + "/game.js"
    text = read(game_js)

    logger_path = root + "/ermao-logger.js"
    write(logger_path, LOGGER)

    import_line = "import './ermao-logger';"
    if import_line not in text:
        # ES module evaluation order follows import order: this must be the very
        # first import so it runs before wx-game-kit and framework startup.
        text = import_line + "\n" + text

    # The WXGameKit loader catch is the last line of defence when a startup
    # error is swallowed by the plugin's promise chain.
    if "__ermaoReportError('loader.start'" not in text:
        text = re.sub(
            r"(console\.error\(\s*['\"]start fail['\"][^;]*;\s*)",
            r"\1try { if (typeof GameGlobal !== 'undefined' && GameGlobal.__ermaoReportError) "
            r"GameGlobal.__ermaoReportError('loader.start', (err && (err.stack || err.message)) || String(err)); } catch (e) {}\n",
            text,
            count=1,
        )

    write(game_js, text)

    # WXGameKit's crash handler shows the generic abort page; report the reason
    # to the realtime log before that page replaces everything.
    handlers = root + "/wx-game-kit/custom/handlers.js"
    try:
        h = read(handlers)
        if "game.crash" not in h:
            h = h.replace(
                "WXGameKit.status.setOnCrashHandler(() => {\n",
                "WXGameKit.status.setOnCrashHandler(() => {\n"
                "  try { if (GameGlobal.__ermaoReportError) GameGlobal.__ermaoReportError('game.crash', 'WXGameKit crash handler fired'); } catch (e) {}\n",
                1,
            )
            write(handlers, h)
    except FileNotFoundError:
        pass

    print(f"[wechat] first-import realtime logger installed: {logger_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
