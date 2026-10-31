# ErMao Engine - 环境准备脚本（其他电脑一键跑起来）
#
# 用法：
#   setup.bat                 检查依赖 -> 安装缺失的依赖 -> 构建 -> 冒烟测试
#   setup.bat -WithVulkan     顺便安装 Vulkan SDK（默认不装，D3D11 也能跑）
#   setup.bat -NoBuild        只检查/安装依赖，不构建
#
# 说明：
#   * 只依赖 Windows 自带组件（PowerShell 5.1+、winget 可选）；
#   * 缺失的依赖优先用 winget 安装；没有 winget 时，CMake 会下载便携版到 .tools\cmake；
#   * Visual Studio 的 C++ 工作负载需要管理员权限，脚本会提示以管理员重跑。
param(
    [switch]$WithVulkan,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$LogPath = Join-Path $Root "setup.log"
try { Start-Transcript -Path $LogPath -Append | Out-Null } catch { }

function Write-Step($text)  { Write-Host "`n==> $text" -ForegroundColor Cyan }
function Write-Ok($text)    { Write-Host "  [OK]   $text" -ForegroundColor Green }
function Write-Warn2($text) { Write-Host "  [WARN] $text" -ForegroundColor Yellow }
function Write-Err($text)   { Write-Host "  [FAIL] $text" -ForegroundColor Red }

function Test-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Find-VS {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $path = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        if ($path) { return $path.Trim() }
    }
    foreach ($base in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        foreach ($edition in @("Enterprise", "Professional", "Community", "BuildTools")) {
            $candidate = Join-Path $base "Microsoft Visual Studio\2022\$edition"
            if (Test-Path (Join-Path $candidate "VC\Auxiliary\Build\vcvars64.bat")) { return $candidate }
        }
    }
    return $null
}

function Find-CMake {
    $cmd = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $local = Join-Path $Root ".tools\cmake\bin\cmake.exe"
    if (Test-Path $local) { return $local }
    $vs = Find-VS
    if ($vs) {
        $bundled = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if (Test-Path $bundled) { return $bundled }
    }
    return $null
}

function Get-Winget {
    $cmd = Get-Command winget.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

function Install-WingetPackage($id, $name, $extraArgs) {
    $winget = Get-Winget
    if (-not $winget) { return $false }
    Write-Host "  正在安装 $name（winget $id）..." -ForegroundColor Gray
    $wingetArgs = @("install", "--id", $id, "-e", "--accept-source-agreements", "--accept-package-agreements", "--silent")
    if ($extraArgs) { $wingetArgs += $extraArgs }
    & $winget @wingetArgs
    return ($LASTEXITCODE -eq 0)
}

function Install-PortableCMake {
    $dest = Join-Path $Root ".tools"
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    $version = "3.30.5"
    $zip = Join-Path $dest "cmake.zip"
    $url = "https://github.com/Kitware/CMake/releases/download/v$version/cmake-$version-windows-x86_64.zip"
    Write-Host "  下载 CMake $version（便携版）..." -ForegroundColor Gray
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing -TimeoutSec 300
        Expand-Archive -Path $zip -DestinationPath $dest -Force
        if (-not (Test-Path (Join-Path $dest "cmake\bin\cmake.exe"))) {
            $inner = Get-ChildItem $dest -Directory | Where-Object { $_.Name -like "cmake-*" } | Select-Object -First 1
            if ($inner) { Rename-Item $inner.FullName (Join-Path $dest "cmake") }
        }
        Remove-Item $zip -Force -ErrorAction SilentlyContinue
        return (Test-Path (Join-Path $dest "cmake\bin\cmake.exe"))
    } catch {
        Write-Warn2 "下载失败：$($_.Exception.Message)"
        return $false
    }
}

Write-Host "==============================================" -ForegroundColor Cyan
Write-Host " ErMao Engine 环境准备" -ForegroundColor Cyan
Write-Host " 工作目录: $Root" -ForegroundColor Cyan
Write-Host "==============================================" -ForegroundColor Cyan

$problems = New-Object System.Collections.Generic.List[string]

# ---------------------------------------------------------------- 0. 系统
Write-Step "0/6 检查系统"
if ([Environment]::OSVersion.Version.Major -lt 10) {
    Write-Err "需要 Windows 10/11"
    $problems.Add("Windows 版本过低")
}
if (-not [Environment]::Is64BitOperatingSystem) {
    Write-Err "需要 64 位系统"
    $problems.Add("非 64 位系统")
}
Write-Ok "$([Environment]::OSVersion.VersionString) / 64-bit"

# ---------------------------------------------------------------- 1. Git（可选）
Write-Step "1/6 检查 Git（可选，用于版本管理）"
$git = Get-Command git.exe -ErrorAction SilentlyContinue
if ($git) {
    Write-Ok "git $(& git --version)"
} else {
    Write-Warn2 "未安装 Git（不影响构建；如需版本管理请安装 Git.Git）"
}

# ---------------------------------------------------------------- 2. Visual Studio + C++ 工作负载
Write-Step "2/6 检查 Visual Studio 2022 C++ 工具链"
$vs = Find-VS
if (-not $vs) {
    Write-Warn2 "未找到带 C++ 工作负载的 Visual Studio 2022"
    if (-not (Test-Admin)) {
        Write-Err "安装需要管理员权限。请右键 setup.bat -> 以管理员身份运行"
        $problems.Add("缺少 VS C++ 工作负载且当前不是管理员")
    } else {
        $ok = Install-WingetPackage "Microsoft.VisualStudio.2022.BuildTools" "VS 2022 Build Tools + C++ 工作负载" `
            @("--override", "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended")
        if (-not $ok) { $problems.Add("VS Build Tools 安装失败，请手动安装") }
        $vs = Find-VS
    }
}
if ($vs) { Write-Ok "Visual Studio: $vs" }

# ---------------------------------------------------------------- 3. Windows SDK
Write-Step "3/6 检查 Windows 10/11 SDK"
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\Include"
if (Test-Path $sdkRoot) {
    $sdk = Get-ChildItem $sdkRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1
    Write-Ok "Windows SDK: $($sdk.Name)"
} else {
    Write-Warn2 "未找到 Windows SDK（VS 的 C++ 工作负载通常自带；缺失时重装 VS 工作负载）"
    $problems.Add("缺少 Windows SDK")
}

# ---------------------------------------------------------------- 4. CMake
Write-Step "4/6 检查 CMake"
$cmake = Find-CMake
if (-not $cmake) {
    $ok = Install-WingetPackage "Kitware.CMake" "CMake" $null
    if (-not $ok) { $ok = Install-PortableCMake }
    $cmake = Find-CMake
}
if ($cmake) {
    $cmakeVersion = (& $cmake --version | Select-Object -First 1)
    Write-Ok "$cmakeVersion ($cmake)"
} else {
    Write-Err "CMake 安装失败；请手动安装 https://cmake.org/download/"
    $problems.Add("缺少 CMake")
}

# ---------------------------------------------------------------- 5. Vulkan（可选）
Write-Step "5/6 检查 Vulkan SDK（可选）"
$vulkan = $env:VULKAN_SDK
if ($vulkan -and (Test-Path (Join-Path $vulkan "Include\vulkan\vulkan.h"))) {
    Write-Ok "Vulkan SDK: $vulkan"
} elseif ($WithVulkan) {
    if (-not (Test-Admin)) {
        Write-Warn2 "安装 Vulkan SDK 需要管理员权限，已跳过"
    } else {
        $ok = Install-WingetPackage "KhronosGroup.VulkanSDK" "Vulkan SDK" $null
        if ($ok) { Write-Ok "Vulkan SDK 已安装（新开终端后生效）" } else { Write-Warn2 "Vulkan SDK 安装失败，可稍后手动安装" }
    }
} else {
    Write-Warn2 "未安装 Vulkan SDK：将跳过 Vulkan 后端，D3D11 正常构建（如需要加 -WithVulkan）"
}

# ---------------------------------------------------------------- 6. 构建 + 冒烟
if ($NoBuild) {
    Write-Step "6/6 跳过构建（-NoBuild）"
} elseif ($problems.Count -gt 0) {
    Write-Step "6/6 跳过构建（依赖未满足）"
} else {
    Write-Step "6/6 构建 Release 并冒烟测试"
    Push-Location $Root
    try {
        & $cmake -S . -B build -G "Visual Studio 17 2022" -A x64
        if ($LASTEXITCODE -ne 0) { throw "CMake 配置失败" }
        & $cmake --build build --config Release -- /m /nologo /v:m
        if ($LASTEXITCODE -ne 0) { throw "编译失败" }
        $exe = Join-Path $Root "build\bin\ErMao.exe"
        if (-not (Test-Path $exe)) { throw "没有生成 build\bin\ErMao.exe" }
        Write-Ok "构建成功: $exe"
        & $exe --frames 30 --noeditor
        if ($LASTEXITCODE -ne 0) { throw "冒烟测试失败（exit $LASTEXITCODE）" }
        Write-Ok "冒烟测试通过（30 帧）"
    } catch {
        Write-Err $_.Exception.Message
        $problems.Add("构建失败，详见 setup.log")
    } finally {
        Pop-Location
    }
}

# ---------------------------------------------------------------- 总结
Write-Host "`n==============================================" -ForegroundColor Cyan
if ($problems.Count -eq 0) {
    Write-Host " 完成：现在可以双击 run.bat 或 build\bin\ErMao.exe" -ForegroundColor Green
} else {
    Write-Host " 还有问题需要处理：" -ForegroundColor Yellow
    foreach ($p in $problems) { Write-Host "  - $p" -ForegroundColor Yellow }
    Write-Host " 日志: $LogPath" -ForegroundColor Gray
}
Write-Host "==============================================" -ForegroundColor Cyan
try { Stop-Transcript | Out-Null } catch { }

if ($problems.Count -gt 0) { exit 1 }
exit 0
