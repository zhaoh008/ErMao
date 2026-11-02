# 在 CLion 里打开 ErMao（CMake 配置）

> 现象：CLion 直接用默认 CMake profile 配置，报
> `No CMAKE_CXX_COMPILER could be found` 或
> `ErMao 需要 MSVC（Visual Studio 2022 x64），当前编译器是 GNU/Clang`。
>
> 原因只有一个：**CLion 选的工具链不是 MSVC**（默认常是 bundled MinGW），或者没有工具链。
> 本引擎用了 D3D11 / WIC / XAudio2 / HLSL，必须用 Visual Studio 2022 的 MSVC。

---

## 1. 最快配置（推荐，两步）

### 1.1 选对工具链

`File → Settings → Build, Execution, Deployment → Toolchains`

- 添加/启用 **Visual Studio**（自动检测 `Visual Studio 2022`，Architecture 选 `x64`）；
- 把 **MinGW / Cygwin / WSL** 取消勾选或删掉默认项；
- 点 `Apply`。

### 1.2 用仓库自带的 CMake 预设

`Settings → Build, Execution, Deployment → CMake`：

- 删除当前的 `Debug` profile；
- 点 `+` → 选择 **`vs2022`（Visual Studio 2022 x64，推荐）**；
- Build directory 会自动是 `build`；
- `Apply` → `Reload CMake Project`。

或者不用预设，手动设置：

| 项 | 值 |
|---|---|
| Generator | `Visual Studio 17 2022`（推荐）或 `Ninja` |
| Architecture | `x64` |
| Build directory | `build`（或 `cmake-build-debug` 均可） |
| CMake options | 留空即可 |

> 预设文件：仓库根目录 `CMakePresets.json`（`vs2022` / `ninja-msvc-debug` / `ninja-msvc-release`）。

---

## 2. 运行/调试配置

`Run → Edit Configurations → ErMao`：

- **Working directory**：`$CMakeCurrentBuildDir$/bin`（资源 `assets/`、`shaders/` 在 exe 旁边）；
- Target：`ErMao`；
- 想直接进游戏，可在 Program arguments 填 `--demo2d` 或 `--demo3d`。

否则会出现“能启动但资源加载失败/黑屏”，因为工作目录不在 exe 旁边。

---

## 3. 为什么不能用 MinGW

| 依赖 | 说明 |
|---|---|
| D3D11 / DXGI / D3DCompiler | Windows SDK 的 MSVC 导入库；MinGW 的 `libd3d11.a` 版本/宏不保证一致 |
| WIC（截图/贴图解码） | `windowscodecs` COM 接口，MSVC 头文件路径不同 |
| XAudio2 | `xaudio2.h` + `xaudio2.lib` |
| HLSL | 运行时 `D3DCompile`，必须是 D3DCompiler |
| `/utf-8`、`#pragma comment(lib)` | MSVC 专属 |

所以 CMake 在检测到非 MSVC 时会**直接给出可操作的错误**，而不是让你面对 100 个编译错误。

---

## 4. 常见报错对照

| CLion 报错 | 原因 | 处理 |
|---|---|---|
| `No CMAKE_CXX_COMPILER could be found` | Toolchain 没选 / 不在 PATH | 选 Visual Studio 工具链；或从 “x64 Native Tools Command Prompt” 启动 CLion |
| `ErMao 需要 MSVC ... 当前编译器是 GNU` | Toolchain 是 MinGW | 换成 Visual Studio；删掉 `cmake-build-*` 后 Reload |
| `CMAKE_CXX_COMPILER not set`（Ninja） | Ninja 需要 MSVC 环境 | 用 `vs2022` 预设；或 Toolchain 选 Visual Studio 后用 Ninja |
| `C1083: 无法打开包括文件`（Vulkan） | 旧版本代码 + 没装 SDK | 更新到 v0.8.1+（已用 `ERMAO_HAS_VULKAN` 隔离） |
| `LNK1104: 无法打开 ErMao.exe` | 上一次程序还在跑 | 结束 `ErMao.exe` 进程再构建 |
| 能运行但黑屏/无资源 | 工作目录不对 | Run Configuration 的 Working directory 设为 `.../bin` |
| Debug 构建很慢 | `/MP` 已对 VS 生成器启用；Ninja 用 jobserver | 正常 |

---

## 5. 命令行等价

```bat
:: 预设（推荐）
cmake --preset vs2022
cmake --build --preset vs2022-debug

:: 或者用仓库脚本
setup.bat
build.bat Debug
```

Ninja + MSVC 必须在已加载 MSVC 环境的终端里：

```bat
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake --preset ninja-msvc-debug
cmake --build --preset ninja-msvc-debug
```

---

## 6. 验证清单

- [ ] CLion 的 Toolchain 是 Visual Studio 2022（x64）
- [ ] CMake profile 用 `vs2022` 预设或 Generator=`Visual Studio 17 2022`
- [ ] Reload 后输出里有 `Vulkan SDK: ...` 或 `未安装：跳过`（两者都正常）
- [ ] 构建产物在 `build/bin/ErMao.exe`
- [ ] Run Configuration 工作目录是 `.../bin`
- [ ] 按 `--demo2d` 能进 2D 幸存者，`--demo3d` 能进 3D 幸存者

---

## 7. 附：CLion 的 CMake 命令为什么带 `--debugger`

```
cmake.exe --debugger --debugger-pipe=\\.\pipe\cmake-debug-... -S ... -B ... -G Ninja ...
```

`--debugger` 是 CLion 为了 CMake 脚本断点调试加的包装参数，**不是错误原因**。
真正失败的是后面的 `-G Ninja` 找不到编译器（或找到了 MinGW）。按本文第 1 节换工具链即可。
