# 2D 幸存者：Q版水墨国风重构截图

素材来自用户提供的《Q版水墨国风（行侠仗义五千年）》素材包；登记见
`engine/assets/inkwash/LICENSE-INKWASH.txt`。

## 对照口径

- 改造前：`main`（0.2.6）H5/WebGL 构建，旧地牢像素风。
- 改造后：本分支（0.3.0）H5/WebGL 构建，Q版水墨国风。
- 同一场景、同一视口、同一固定步数：桌面 1600×900，`?play=1&frames=300`（5 秒）；
  手机竖屏 402×874，`?play=1&frames=300&mobile=1`。
- 对照图左侧为改造前、右侧为改造后；菜单为同一启动状态，战斗为同一第 300 帧。

## 文件

| 文件 | 内容 |
|---|---|
| `menu-before-after.png` | 桌面主菜单对照（1600×900） |
| `gameplay-before-after.png` | 桌面战斗对照（1600×900，第 300 帧） |
| `portrait-menu-before-after.png` | 手机竖屏主菜单对照（402×874） |
| `portrait-gameplay-before-after.png` | 手机竖屏战斗对照（402×874，第 300 帧） |
| `after-prepare-1600x900.png` | 出战准备与职业立绘 |
| `after-levelup-1600x900.png` | 升级三选一 |
| `after-newskills-1600x900.png` | 陨石/黑洞/棱镜/毒爆特效 |
| `after-poison-1600x900.png` | 回旋刃/霜环/毒雾/火球特效 |
| `after-codex-1600x900.png` | 武学图鉴 |
| `after-gacha-1600x900.png` | 增益宝箱开箱（光爆阶段） |
| `after-pause-1600x900.png` / `after-result-1600x900.png` | 暂停 / 结算 |
| `after-prepare-402x874.png` | 手机竖屏出战准备 |
| `after-newskills-402x874.png` | 手机竖屏满屏技能 |

截图由 Windows Chrome + CDP 对 `build-web` 的 H5 产物直接抓取；未使用 Photoshop 或 mockup。
