# 截取 ErMao 窗口并保存为 PNG
# 用法: powershell -File tools\screenshot.ps1 -Out docs\screenshots\shot.png [-Wait 3]
param(
    [string]$Out = "docs\screenshots\shot.png",
    [int]$Wait = 3,
    [string]$ProcessName = "ErMao",
    [switch]$KeepRunning
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) {
    Write-Error "进程 $ProcessName 未运行"
    exit 1
}
$h = $proc.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Write-Error "窗口句柄为空"; exit 1 }

[void][W]::ShowWindow($h, 9)  # SW_RESTORE
[void][W]::SetForegroundWindow($h)
Start-Sleep -Seconds $Wait

$r = New-Object W+RECT
[void][W]::GetWindowRect($h, [ref]$r)
$w = $r.Right - $r.Left
$hh = $r.Bottom - $r.Top
if ($w -le 0 -or $hh -le 0) { Write-Error "窗口尺寸异常"; exit 1 }

$bmp = New-Object System.Drawing.Bitmap $w, $hh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
$dir = Split-Path -Parent $Out
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
$full = [System.IO.Path]::GetFullPath($Out)
$bmp.Save($full, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Output "saved: $full ($w x $hh)"

if (-not $KeepRunning) {
    Stop-Process -Id $proc.Id -Force
    Write-Output "process stopped"
}
