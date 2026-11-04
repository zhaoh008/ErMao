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
| `after-endgame.png` | 10 分钟终局（开发走查） | 5 个 Lv.20 技能、满屏怪、单次 525 伤害数字；9 分钟后怪速/数量提升 |
| `after-newskills.png` | 新技能终局（开发走查） | 陨石、黑洞、棱镜光束、毒爆 + 箭矢的满屏特效；技能图标与 Lv.20 等级 |
| `after-poison.png` | 毒雾特效（开发走查） | 半透明柔和毒雾，不再出现 Kenney 黑底烟雾贴图造成的黑方块 |
| `before-effects.png` | 战斗特效 | 旧版只有圆环 / 折线等程序化图形，伤害数字带负号 |
| `after-effects.png` | 战斗特效 | 新版火焰爆炸、火球弹体、雷霆与奥术命中贴图；伤害数字为正数 |
| `after-codex.png` | 技能图鉴 | 说明改为“职业给 1 个基础技能，击杀最多再掉落 4 个（共 5 个）” |

截图由 `--ui-preview <prepare|hud|hud5|codex>` 与 `--demo2d --frames N` 生成，
`--window-size 1600x900`。`hud5` 是开发走查页，直接注入 5 个已拥有技能用于验证 HUD 上限。
