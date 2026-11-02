# 贡献指南

## 分支

| 分支 | 用途 |
|---|---|
| `main` | 可运行、可打包的稳定态；每次提交都过 `build.bat` |
| `feat/<topic>` | 新功能（如 `feat/vulkan-pass`） |
| `fix/<topic>` | 缺陷修复 |
| `docs/<topic>` | 文档/讲义/博客 |
| `ai/<topic>` | AI 生成的改动（单独分支，便于回滚） |

合并前：`git rebase main`（或 merge），保证历史线性可读。

## 提交信息（Conventional Commits）

```
<type>(<scope>): <summary>

<body 可选：为什么、怎么验证>
```

- type：`feat` / `fix` / `docs` / `refactor` / `perf` / `test` / `build` / `chore`
- scope：`renderer` / `editor` / `game` / `audio` / `vulkan` / `docs` / `repo` …
- summary 用中文或英文均可，但同仓库保持一致；
- **AI 参与的提交**在 body 里注明工具与提示词摘要（可追溯）。

## 提交前检查

```bat
build.bat                              :: 必须 Build OK
build\bin\ErMao.exe --frames 30        :: 能启动
pwsh -File tools\docs-check.ps1        :: 文档引用有效
```

内容改动还要：

```bat
build\bin\ErMao.exe --test-serialize --frames 2   :: 序列化往返
build\bin\ErMao.exe --demo2d --frames 120          :: 2D 可运行
build\bin\ErMao.exe --demo3d --frames 120          :: 3D 可运行
```

## 目录约定

- 源码在 `engine/src/<层>/`：`core → rhi → renderer → scene → game → editor → runtime`，禁止反向依赖；
- 资源在 `engine/assets/<类型>/`，外部素材必须登记 `docs/版本/发布与工程.md` 的“素材来源与许可”；
- 文档只有两类：`docs/版本/`（版本文档）与 `docs/教学/`（教学文档，讲义在 `docs/教学/讲义/`）；总纲在 `docs/00-代码总纲.md`；
- `build/`、`dist/`、`.staging/`、`*.log` 不入库。

## PR / 合并清单

- [ ] `build.bat` OK，0 warning（新增 warning 要说明）
- [ ] 相关 demo 能跑（截图或日志贴 PR）
- [ ] `docs-check.ps1` 通过
- [ ] CHANGELOG 的 Unreleased 段落已更新
- [ ] 新增/修改的素材已登记许可
- [ ] 没有提交 `build/`、`dist/`、日志、`imgui.ini`

## 钩子

```bat
git config core.hooksPath .githooks
```

`pre-commit` 会拦截 `build/`、`dist/`、`*.log` 的提交，并跑一遍文档引用检查。
