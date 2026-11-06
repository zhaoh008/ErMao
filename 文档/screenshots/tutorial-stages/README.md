> 0.5.0 的仙侠 UI、分步教学与角色/技能居中截图见 [../xianxia-ui/README.md](../xianxia-ui/README.md)。

# 2D 幸存者：职业选择、教学关与 20 层关卡

对应 2D 幸存者 0.4.0。玩法说明见 [2D幸存者-20层与教学.md](../../版本文档/2D幸存者-20层与教学.md)。

## 对照口径

- **Before**：`main @ 0cf36d8`（0.3.1），wx-transformer/H5 同源构建。
- **After**：本分支（0.4.0）。
- 同一场景、同一视口、同一固定步数：
  - 桌面 1600×900：`?ui-preview=class&frames=2`（选人）、`?play=1&frames=300`（战斗第 5 秒）。
  - 手机竖屏 500×874：同上加 `&mobile=1`。旧截图使用 402×874；本机 Chrome 新版会强制 ≥500px 布局宽度，500×874 才是完整未裁切的竖屏视口，因此 Before/After 均以 500×874 重拍。
- 截图由 Windows Chrome headless（SwiftShader WebGL1）直接抓取 H5 产物，未使用 mockup 或图片编辑。

## Before / After 对照

| 对照 | Before | After | 说明 |
|---|---|---|---|
| 桌面出战准备 / 职业选择 | `before-class-1600x900.png` | `after-class-1600x900.png` | 旧版默认游侠、三张介绍卡文字溢出/被裁；新版先选职业、三张职业卡（立绘/定位/成长）排版完整 |
| 桌面战斗 | `before-gameplay-1600x900.png` | `after-gameplay-1600x900.png` | 旧版玩家为行走序列帧；新版为法师立绘静态图，HUD 多出“第 1/20 层 + 教学进度” |
| 手机竖屏选人 | `before-class-500x874.png` | `after-class-500x874.png` | 旧版三卡横向挤压、右侧介绍被截；新版三行卡片、字号放大 |
| 手机竖屏战斗 | `before-gameplay-500x874.png` | `after-gameplay-500x874.png` | 旧版默认游侠直接开局；新版先选职业，进入教学关 |

## 功能截图（After）

| 文件 | 内容 |
|---|---|
| `after-class-selected-1600x900.png` | 选中“法师”后，开始按钮启用 |
| `after-click-start-1600x900.png` | 点击“开始教学”后进入第 1 层教学关（真实 UI 点击，非跳转参数） |
| `after-tutorial-1600x900.png` | 教学关：左侧分步提示 + 基础武学/雷霆成长方向 |
| `after-tutorial-500x874.png` | 手机竖屏教学关（顶部层数条、底部教学面板、5 技能槽） |
| `after-stage-1600x900.png` | 第 8 层：HUD 显示随机通关条件“击杀 63/120” |
| `after-stageclear-1600x900.png` | 层间结算：本层条件、总战绩、下一层随机条件、进入下一层 |
| `after-victory-1600x900.png` | 通关 20 层后的胜利结算 |
| `after-tutorialcomplete-1600x900.png` | 固定步 3400 帧（约 56.7 秒）实跑：教学 55 秒达成，自动进入层间结算，下一层随机为“拾取 40 枚经验” |
