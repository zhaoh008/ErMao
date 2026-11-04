# 2D 幸存者平衡 / 掉落 / 特效走查截图

基线：`e7e7cfb`（PR #62 合并后的 UI v2 版本）。
改动后：本目录所在的提交（Windows Release，1600×900，`--demo2d` / `--ui-preview` 实机截图）。

| 文件 | 场景 | 说明 |
|---|---|---|
| `before-prepare.png` | 出战准备 | 旧版 6 选 4 技能槽 |
| `after-prepare.png` | 出战准备 | 新版职业选择：游侠 / 剑士 / 法师决定初始基础技能 |
| `before-hud.png` | 开局 HUD | 旧版开局直接拥有 6 个技能、底部 6 槽 |
| `after-hud-one-skill.png` | 开局 HUD | 新版只拥有职业基础技能（1 槽）；地上是“新技能：雷霆”掉落 |
| `after-hud-skill-drop-stays.png` | 同一局稍后 | 技能掉落仍停在原地，证明不会自动吸附；同时可见正数伤害数字 |
| `after-five-skills.png` | 满技能 HUD | 基础技能 + 4 个掉落技能，共 5 槽；霜环 / 回旋刃特效 |
| `before-effects.png` | 战斗特效 | 旧版只有圆环 / 折线等程序化图形，伤害数字带负号 |
| `after-effects.png` | 战斗特效 | 新版火焰爆炸、火球弹体、雷霆与奥术命中贴图；伤害数字为正数 |
| `after-codex.png` | 技能图鉴 | 说明改为“职业给 1 个基础技能，击杀最多再掉落 4 个（共 5 个）” |

截图由 `--ui-preview <prepare|hud|hud5|codex>` 与 `--demo2d --frames N` 生成，
`--window-size 1600x900`。`hud5` 是开发走查页，直接注入 5 个已拥有技能用于验证 HUD 上限。


## 第 1 轮数值调整（2026-11-04）

同一场景：游侠、1600×900、`--balance-test kite 55`、同一固定种子；基线为 `main @ 34d6c83`，改动后为本分支。

| 文件 | 版本 | 说明 |
|---|---|---|
| `round1-hud-before.png` | 基线 | 右上角无小地图；技能为旧版直接升级 |
| `round1-hud-after.png` | 本分支 | 右上小地图（补给箱标记 + 最近补给距离 7.5）、技能槽充能进度、左上补给提示 |
