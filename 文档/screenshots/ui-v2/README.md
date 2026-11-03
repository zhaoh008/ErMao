# 2D 幸存者 UI 重构截图

- `before-*`：改动前基线（`dff054e`），1600×900；`before-pause/settings/result` 通过真实按键/读档进入。
- `after-*`：本次分支，1600×900；`after-*-1280` 为 1280×720 缩放检查。
- `after-hud-tooltip`：点击技能槽后的说明面板。
- 截图使用实际 Windows Release 构建；`--ui-preview` 只负责进入对应页面，页面里的时间/击杀/等级使用走查数据，正常游戏仍读取真实存档与本局统计。
