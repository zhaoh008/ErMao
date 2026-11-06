# 2D 幸存者 0.6.0：仙侠 UI、分步教学与角色/技能居中

参考：`D:\GameEngine\game\xianxia-design`（暖宣纸 + 青玉 + 朱砂的仙侠 UI 设计稿）。

## 对照口径

- **Before**：`main @ 0cf36d8`（0.4.0 之前的暗色水墨 UI）。
- **After**：本分支 0.6.0。
- 桌面 1600×900，手机竖屏 500×874。旧版截图使用 402×874；本机新版 Chrome 会强制 ≥500px 布局宽度，500×874 才是完整未裁切的竖屏视口，因此 Before/After 均用 500×874 重拍。
- 截图由 Windows Chrome headless（SwiftShader WebGL1）直接抓取 H5 产物，未使用 mockup 或图片编辑。

## Before / After 对照

| 对照 | Before | After | 说明 |
|---|---|---|---|
| 桌面职业选择 | `before-class-1600x900.png` | `after-class-1600x900.png` | 暗色水墨 → 暖宣纸/青玉；职业卡与文字保持完整 |
| 桌面战斗/教学 | `before-gameplay-1600x900.png` | `after-gameplay-1600x900.png` | 旧版默认游侠直接开局；新版先选职业，进入第一步教学 |
| 手机职业选择 | `before-class-500x874.png` | `after-class-500x874.png` | 手机竖屏三行职业卡，宣纸配色 |
| 手机战斗/教学 | `before-gameplay-500x874.png` | `after-gameplay-500x874.png` | 顶栏/雷达/技能槽改为宣纸 + 青玉 |

## 功能截图（After）

| 文件 | 内容 |
|---|---|
| `after-menu-1600x900.png` | 主菜单：宣纸留白、朱砂标题、青玉按钮 |
| `after-tutorial-step-1600x900.png` | 教学第一步：未操作前暂停刷怪/计时，清单高亮“移动” |
| `after-tutorial-skip-1600x900.png` | 教学面板“跳过教学”，直接进入第 2 层 |
| `after-class-first-1600x900.png` | 首次进入：第 1 层教学，无选层器 |
| `after-class-progress-1600x900.png` / `after-class-progress-500x874.png` | 已通关后：历史最高 + 起始层选择 |
| `after-tutorial-done-1600x900.png` | 步骤完成：勾选 + “步骤完成”提示，再自动进入下一步 |
| `after-tutorial-finale-1600x900.png` | 四步完成后的 35 秒怪潮实战与成长方向面板 |
| `after-tutorial-500x874.png` | 手机竖屏分步教学面板（底部，不挡角色） |
| `after-stage-1600x900.png` / `after-stage-500x874.png` | 第 8 层：人物、飞刃圈与技能都以身体中心为圆心 |
| `after-stageclear-1600x900.png` | 层间结算（宣纸法册 + 青玉按钮） |
| `after-victory-1600x900.png` | 通关 20 层结算 |
| `after-levelup-1600x900.png` / `after-settings-1600x900.png` / `after-codex-1600x900.png` / `after-gacha-1600x900.png` | 升级三选一 / 设置 / 图鉴 / 开箱抽奖 |
