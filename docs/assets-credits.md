# 素材来源与许可（Assets Credits）

> 规则：**每个进入仓库的第三方素材都要在这里登记一行**。
> 字段：文件 / 来源 URL / 作者 / 许可 / 是否改动。

## 2D 素材

| 文件 | 来源 | 作者 | 许可 | 改动 |
|---|---|---|---|---|
| `engine/assets/sprites/soldier_idle.png` 等 4 个 Soldier 贴图 | 用户提供的 `Tiny RPG Character Asset Pack 01 v2.0 - Free Soldier & Orc` | 素材包原作者（包内未附许可文件） | 免费素材，学习/演示使用；商用前请向原作者确认 | 仅重命名 |
| `engine/assets/sprites/orc_idle.png` 等 3 个 Orc 贴图 | 同上 | 同上 | 同上 | 仅重命名 |
| `engine/assets/sprites/arrow.png` | 同上 | 同上 | 同上 | 仅重命名 |
| `engine/assets/sprites/kenney_tilemap.png` | https://kenney.nl/assets/tiny-dungeon | Kenney | **CC0 1.0** | 未改动（`LICENSE-kenney-tiny-dungeon.txt` 随附） |


## 3D 素材

| 文件 | 来源 | 作者 | 许可 | 改动 |
|---|---|---|---|---|
| `models/kenney/character-a..f.obj` | https://kenney.nl/assets/blocky-characters | Kenney | **CC0** | 未改动 |
| `models/kenney/character-{skeleton,zombie,vampire,ghost}.obj` | https://kenney.nl/assets/graveyard-kit | Kenney | **CC0** | 未改动 |
| `models/kenney/{cross,cross-wood,column-large,crypt-a,coffin,candle,altar-stone}.obj` | 同上 | Kenney | **CC0** | 未改动 |
| `textures/kenney/colormap.png` | 同上 | Kenney | **CC0** | 未改动 |
| `particles/{flame_01,flame_02,fire_01,spark_01,spark_02,smoke_01,smoke_02,star_01,light_01}.png` | https://kenney.nl/assets/particle-pack | Kenney | **CC0** | 未改动 |
## 程序化资源（无第三方版权）

| 资源 | 生成位置 | 说明 |
|---|---|---|
| `gem_proc` | `game/Survivors.cpp` | 代码绘制的 12×12 菱形宝石 |
| `floor_proc` | `game/Survivors.cpp` | 代码绘制的 32×32 石砖地面 |
| 立方体/球/圆柱/胶囊/圆环/平面 | `asset/AssetManager.cpp` | 程序化网格 |
| `grid`/`checker`/`noise`/`white` | `asset/AssetManager.cpp` | 程序化贴图 |

## 字体

| 文件 | 来源 | 许可 |
|---|---|---|
| `C:/Windows/Fonts/msyh.ttc`（运行时加载，不入库） | Microsoft Windows 系统字体 | 随 Windows 授权，仅本机使用 |

## 第三方库

| 库 | 版本 | 许可 | 位置 |
|---|---|---|---|
| Dear ImGui | 1.93.1 WIP (docking, commit 83ccc61) | MIT | `third_party/imgui/`（见 `LICENSE.txt`） |
