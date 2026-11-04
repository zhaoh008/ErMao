# 2D 幸存者：增益宝箱图标与开箱抽奖（合并版）

## 对照场景

统一使用 Windows Release、1600×900、无编辑器、固定走查数据：

```bat
ErMao.exe --level survivors --noeditor --play --ui-preview chest --frames 90 --window-size 1600x900 --screenshot chest.png
```

- **Before**：`main @ d87e8f85`（PR #65 已合并的数值/宝箱版本）。为了在同一位置截图，基线只额外加了 `--ui-preview chest` 的截图布点（在玩家右前方固定放一个史诗宝箱），不改渲染与玩法。旧版宝箱是旋转的 `pickup_proc` 菱形，没有稀有度光环/标签。
- **After**：本 PR 同一场景、同一帧、同一窗口。宝箱改为 Kenney Tiny Dungeon 图标，并增加稀有度光环与顶部标签；拾取后冻结世界，播放开箱抽奖动画。

> 说明：PR #66 是更早的数值分支，已被 `main` 上的 #65 取代并与当前 `main` 冲突。本合并 PR 保留 `main` 的数值/宝箱系统，只把 #67 的图标与抽奖表现移植过来；没有把 #66 的旧数值改动强行合回 `main`。

## 抽奖页

```bat
ErMao.exe --level survivors --noeditor --play --ui-preview gacha --frames 90 --window-size 1600x900 --screenshot gacha.png
ErMao.exe --level survivors --noeditor --play --ui-preview gacha-burst --frames 90 --window-size 1600x900 --screenshot gacha-burst.png
```

- `after-gacha-burst.png`：抖动结束、光爆/冲击波/开箱序列帧的阶段。
- `after-gacha-reveal.png`：翻牌结果，含稀有度、奖励图标、效果与收取提示。

实际流程中，动画总时长 3 秒，世界与游戏计时在动画期间冻结；翻牌后可点击/空格提前收取，动画结束才继续战斗并给 0.8 秒无敌。增益在拾取时已经结算，抽奖负责展示结果。

## 素材来源

- 地图图标：Kenney **Tiny Dungeon**（CC0），第 7 行第 7 列；许可见 `engine/assets/sprites/LICENSE-kenney-tiny-dungeon.txt`。
- 开箱 4 帧：OpenGameArt **Chest - Opening Animation 16x16**（作者 Natural_Privateer，CC0）；许可与处理见 `engine/assets/sprites/LICENSE-chest.txt`。
- 光爆序列帧复用仓库已有的 OpenGameArt **Explosion Sheet**（CC0，见 `engine/assets/vfx/LICENSE-VFX.txt`）。
