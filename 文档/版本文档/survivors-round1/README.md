# 2D 幸存者平衡对照数据（第 1 轮）

对应设计文档：[2D幸存者数值策划-第1轮.md](../2D幸存者数值策划-第1轮.md)。

## 文件

| 文件 | 场景 | 代码版本 |
|---|---|---|
| `before-stand.csv` | 游侠，站桩 360s | 基线 `main @ 34d6c83` |
| `before-kite.csv` | 游侠，脚本走位 360s | 基线 `main @ 34d6c83` |
| `after-stand.csv` | 游侠，站桩 300s（实际 138s 死亡） | 本分支 |
| `after-kite.csv` | 游侠，脚本走位 600s（实际 217s 死亡） | 本分支 |

生成命令（Windows Release，固定 1/60 步长与固定种子）：

```bat
ErMao.exe --balance-test stand 300 data\after-stand.csv
ErMao.exe --balance-test kite 600 data\after-kite.csv
```

## 列

`t,hp,max_hp,level,kills,skills,skill0..skill5,aspd,dmg,range,spread,upgrades,skill_pickups,damage_taken,healing,enemies,in_contact,chests,chest_dist`

- `skill0` 是箭矢四条强化的合计等级（不是 6 级上限内的等级）；`skill1..5` 是雷霆/回旋刃/霜环/毒雾/火球。
- `upgrades` 是实际拾取的强化次数（不含技能解锁与血瓶）；`skill_pickups` 是实际捡起的新技能数。
- `chests` 是地图补给箱拾取数；`chest_dist` 是当秒最近补给箱距离（-1 表示场上没有）。
- `in_contact` 是距离玩家 1.8 单位内的敌人数，用来区分“贴身前被杀”和“贴身没伤害”。

## 结果摘要

见设计文档 §9.2–§9.5。关键结论：站桩从 360s 不死变为 138s 死亡；走位 217s 死亡；毕业从 68s/120s 推到 3:37 仍未出现；3 分钟后敌人成长超过普通构筑 DPS，是本轮未通过验收的主因。

> 基线两份 CSV 生成于加入 `chests` 列之前，最后一列是 `chest_dist`，没有 `chests` 列；本分支两份含 `chests`。
