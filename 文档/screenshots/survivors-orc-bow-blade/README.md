# 2D 幸存者：兽人朝向 / 弓箭散射成长 / 回旋刃圆环 对比截图

- 基线：`d87e8f8`（main，PR #65 合并后的 20 级曲线版本）
- 改动后：本目录所在提交
- 环境：Windows Release，1600×900，`--demo2d` 实机运行，`--frames` 跑帧后由引擎 `--screenshot` 保存帧缓冲

所有截图使用 `--ui-preview` 开发走查页固定状态后拍摄；走查页只设置预览状态（兽人站位、散射等级、回旋刃等级），不改动玩法与渲染代码。基线构建为了拍同样的状态，临时补了相同的走查页（本 PR 新增）。

| 文件 | 场景 | 说明 |
|---|---|---|
| `orc-facing-before-after.png` | 兽人朝向 | 左右两侧兽人同时向玩家追击：基线会按移动方向翻反（背对玩家），改动后与移动方向一致 |
| `before-orc.png` / `after-orc.png` | 兽人朝向全屏 | `--ui-preview hud-orc`：玩家四周固定放 4 只兽人，第 60 帧截图 |
| `bow-growth-before-after.png` | 弓箭散射成长 | 基线一次拾取直接 8 方向；改动后按 2 / 4 / 6 / 8 / 12 逐级成长 |
| `before-bow.png` | 基线散射 | `--ui-preview hud-bow`（旧版为“八向散射”一次性解锁），第 80 帧 |
| `after-bow-lv1.png` … `after-bow-lv5.png` | 新散射各等级 | `--ui-preview hud-bow<N>`，第 80 帧 |
| `blade-ring-before-after.png` | 回旋刃圆环 | 基线与改动后的 1 / 5 级对比：环绕半径、剑体尺寸、圆环轨道、切线朝向 |
| `before-blade-lv1.png` / `after-blade-lv1.png` | 回旋刃 1 级 | `--ui-preview hud-blade1`，第 60 帧 |
| `before-blade-lv5.png` / `after-blade-lv5.png` | 回旋刃 5 级 | `--ui-preview hud-blade5`，第 60 帧 |

## 复现命令

```powershell
# 兽人朝向（第 60 帧，玩家四周固定 4 只兽人）
ErMao.exe --demo2d --ui-preview hud-orc --window-size 1600x900 --frames 60 --screenshot after-orc.png

# 弓箭散射：Lv.1..5 = 2/4/6/8/12 方向（第 80 帧）
ErMao.exe --demo2d --ui-preview hud-bow1 --window-size 1600x900 --frames 80 --screenshot after-bow-lv1.png
ErMao.exe --demo2d --ui-preview hud-bow5 --window-size 1600x900 --frames 80 --screenshot after-bow-lv5.png

# 回旋刃圆环：1 级 / 5 级（第 60 帧）
ErMao.exe --demo2d --ui-preview hud-blade1 --window-size 1600x900 --frames 60 --screenshot after-blade-lv1.png
ErMao.exe --demo2d --ui-preview hud-blade5 --window-size 1600x900 --frames 60 --screenshot after-blade-lv5.png
```
