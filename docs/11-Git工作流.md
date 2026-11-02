# Git 工作流与仓库管理

> 目标：任何一次改动都能追溯、任何一个版本都能检出、任何人都能一键复现。
> 仓库当前是**本地 Git 仓库**（无远程）；加上远程后按本文推送即可。

---

## 1. 现状

```bat
git log --oneline        :: 24+ 次提交
git tag                  :: v0.2.0 ~ v0.8.0
git branch               :: main（稳定线）
```

- 所有源码、资源、讲义、博客规划都在版本控制里；
- `build/`、`dist/`、`.staging/`、日志、`imgui.ini`、AI 脚本输出**不入库**（见 `.gitignore`）；
- 行尾统一由 `.gitattributes` 约束：源码/文档 LF，`.bat/.ps1` CRLF，图片/音频/SPIR-V 二进制。

---

## 2. 分支模型

| 分支 | 规则 |
|---|---|
| `main` | 随时可 `build.bat` 成功、可打包；不直接在上面写大功能 |
| `feat/*` | 新功能，从 `main` 切出，合并前 rebase |
| `fix/*` | 修复 |
| `docs/*` | 讲义/文档/博客 |
| `ai/*` | AI 生成的改动，单独分支便于整体回滚 |

```bat
git switch main
git pull
git switch -c feat/vulkan-opaque
:: 开发...
git add -A
git commit -m "feat(vulkan): migrate opaque pass to RHI"
git switch main
git merge --no-ff feat/vulkan-opaque
git tag -a v0.9.0 -m "Vulkan: opaque pass"
```

---

## 3. 提交信息规范

```
<type>(<scope>): <summary>
```

| type | 用途 | 例子 |
|---|---|---|
| `feat` | 新功能 | `feat(audio): XAudio2 WAV playback` |
| `fix` | 修复 | `fix(2d): missing ImGui EndFrame` |
| `docs` | 文档 | `docs(lectures): math references per lecture` |
| `refactor` | 重构（不改行为） | `refactor(rhi): split device interface` |
| `perf` | 性能 | `perf(renderer): batch sprites by texture` |
| `build` | 构建/CMake | `build: add vulkan-1 link dir` |
| `chore` | 杂项 | `chore(repo): add gitattributes` |

AI 参与的提交，在 body 里写清楚：

```
feat(ai): headless script interface

Tool: DeepSeek Harness
Prompt summary: 给引擎加无头命令接口，支持建场景/绑资产/打包
Verified: build OK; script 17/17; validate ok
```

---

## 4. 标签与发布

每个可运行状态打一个**附注标签**：

```bat
git tag -a v0.8.0 -m "AI 优先接口 + 博客规划"
git tag -n                 :: 查看
git push origin --tags
```

已经打好的里程碑：

| 标签 | 内容 |
|---|---|
| `v0.2.0` | ImGui 编辑器 + 第三人称 |
| `v0.3.0` | 刷怪 Demo、Gizmo、打包 |
| `v0.4.0` | 讲义 + HTML 文档站 |
| `v0.5.0` | 2D 幸存者 + 游戏包 |
| `v0.6.0` | 3D 幸存者 + 外部 OBJ |
| `v0.7.0` | 音频 + Vulkan 阶段一 |
| `v0.8.0` | AI 脚本接口 + 博客规划 |

发布时把 `dist/` 里的包作为 release attachment（dist 不进 Git）。

---

## 5. 远程仓库（GitHub / Gitee）

仓库目前没有 remote。选一个平台建空仓库（**不要**勾选初始化 README），然后：

```bat
:: GitHub
git remote add origin https://github.com/<你的账号>/ErMao.git

:: 或 Gitee
git remote add origin https://gitee.com/<你的账号>/ErMao.git

git push -u origin main
git push origin --tags
```

或用脚本：

```bat
pwsh -File tools\git-remote.ps1 -Url https://github.com/you/ErMao.git -Push
```

认证：HTTPS 需要 Personal Access Token（GitHub 已不支持密码）；SSH 则先配好 key。

### 大文件提醒

- 当前仓库约 90 MB，最大单文件是 21.9 MB 的 `ErMao-学习文档.html`；
- GitHub 单文件上限 100 MB、仓库建议 <1 GB，目前没问题；
- 以后加高清贴图/视频，用 **Git LFS**：`git lfs track "*.psd" "*.mp4"`。

---

## 6. 钩子（提交前自检）

```bat
git config core.hooksPath .githooks
```

`pre-commit` 做两件事：

1. 拒绝 `build/`、`dist/`、`*.log` 入库；
2. 跑 `tools/docs-check.ps1`，文档引用了不存在的 `.cpp/.h/.hlsl` 就拦下。

---

## 7. CI（GitHub Actions）

`.github/workflows/build.yml`：

- Windows runner，CMake 配置 + Release 构建；
- 跑 `docs-check.ps1`；
- 失败即红，防止「本机能跑、提交就坏」。

> Vulkan 相关代码在 CI 上没有 SDK 时自动跳过（CMake 里用 `VULKAN_SDK` 判断），
> 所以 CI 不强制装 Vulkan SDK。

---

## 8. 常见操作

```bat
:: 撤销未提交的改动
git restore .

:: 撤销上一次提交（保留改动）
git reset --soft HEAD~1

:: 查看某文件历史
git log -p -- engine/src/renderer/Renderer.cpp

:: 找回误删文件
git checkout v0.7.0 -- engine/src/platform/Audio.cpp

:: 比较两个版本
git diff v0.7.0..v0.8.0 --stat

:: 二分定位坏提交
git bisect start
git bisect bad
git bisect good v0.7.0
```

---

## 9. 与 AI 协作的约定

- AI 的每次改动都要能**单独回滚**：优先一个逻辑一个 commit；
- AI 生成的脚本/报告（`agent-report.json` 等）不入库，只入库**命令脚本**（`agent/examples/*.json`）；
- 文档改动后跑 `python tools/build_docs_site.py` 重新生成 HTML，再一起提交；
- 提交前让 AI 贴出 `build.bat` 与关键 demo 的日志摘要。
