#!/usr/bin/env python3
"""Inject a wx.getRealtimeLogManager error reporter into wx-transformer's game.js.

Enabled with WECHAT_DEBUG_LOG=1 by package-wechat.sh. The logger is installed
after the ES module imports but before WXGameKit.loader.start(), so it catches
runtime errors on real iOS devices where vConsole is unavailable.
"""
import re
import sys

MARKER = "/* ERMAO_WECHAT_LOG */"

LOGGER = r"""
/* ERMAO_WECHAT_LOG */
(function () {
  var rt = null;
  try {
    if (typeof wx !== 'undefined' && wx.getRealtimeLogManager) {
      rt = wx.getRealtimeLogManager();
      try { rt.setFilterMsg('ermao'); } catch (e) {}
      try { rt.addFilterMsg('wechat'); } catch (e) {}
    }
  } catch (e) {}
  var origLog = (typeof console !== 'undefined' && console.log) ? console.log : function () {};
  var origError = (typeof console !== 'undefined' && console.error) ? console.error : function () {};
  function send(level, tag, args) {
    var text = '';
    try {
      text = Array.prototype.map.call(args, function (a) {
        if (typeof a === 'string') return a;
        try { return JSON.stringify(a); } catch (e) { return String(a); }
      }).join(' ');
    } catch (e) { text = '[unserializable]'; }
    if (rt) {
      try { (rt[level] || rt.info).call(rt, '[ERMAO ' + tag + '] ' + text); } catch (e) {}
    }
    try {
      if (level === 'error') origError('[ERMAO ' + tag + '] ' + text);
      else origLog('[ERMAO ' + tag + '] ' + text);
    } catch (e) {}
  }
  try {
    if (typeof wx !== 'undefined') {
      if (wx.getSystemInfoSync) send('info', 'sys', [JSON.stringify(wx.getSystemInfoSync())]);
      if (wx.getLaunchOptionsSync) send('info', 'launch', [JSON.stringify(wx.getLaunchOptionsSync())]);
    }
  } catch (e) {}
  var g = (typeof GameGlobal !== 'undefined') ? GameGlobal : ((typeof window !== 'undefined') ? window : null);
  if (g && g.addEventListener) {
    g.addEventListener('error', function (e) {
      send('error', 'uncaught', [e.message, e.filename, e.lineno, e.colno, e.error && e.error.stack]);
    });
    g.addEventListener('unhandledrejection', function (e) {
      send('error', 'rejection', [e.reason && (e.reason.stack || e.reason.message || e.reason)]);
    });
  }
  if (g) {
    g.onerror = function (message, source, lineno, colno, error) {
      send('error', 'onerror', [message, source, lineno, colno, error && error.stack]);
    };
  }
  if (typeof console !== 'undefined') {
    console.error = function () {
      send('error', 'console', arguments);
      return origError.apply(console, arguments);
    };
  }
  send('info', 'logger', ['installed']);
})();
"""


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: inject-wechat-log.py <minigame-dir>", file=sys.stderr)
        return 2
    path = sys.argv[1].rstrip("/") + "/game.js"
    text = open(path, encoding="utf-8").read()
    if MARKER in text:
        print(f"[wechat] realtime logger already present: {path}")
        return 0
    # Insert after the ES module imports, before the WXGameKit loader starts.
    match = re.search(r"^import\s+commFrameWork\s+from\s+['\"]\./framework['\"];\s*$", text, re.M)
    if not match:
        print("[wechat] ERROR: cannot find commFrameWork import in " + path, file=sys.stderr)
        return 1
    idx = match.end()
    patched = text[:idx] + "\n" + LOGGER + "\n" + text[idx:]
    open(path, "w", encoding="utf-8").write(patched)
    print(f"[wechat] injected wx.getRealtimeLogManager error reporter: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
