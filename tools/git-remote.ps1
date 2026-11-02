param(
    [string]$Url = "",
    [switch]$Push
)
# 配置远程仓库并推送。用法：
#   pwsh -File tools\git-remote.ps1 -Url https://github.com/you/ErMao.git -Push
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)

if ($Url) {
    $existing = git remote 2>$null
    if ($existing -contains "origin") { git remote remove origin }
    git remote add origin $Url
    Write-Host "[git] origin -> $Url"
}

$remotes = git remote -v
if (-not $remotes) {
    Write-Host "[git] 还没有远程仓库。先建一个空仓库，再运行："
    Write-Host "  pwsh -File tools\git-remote.ps1 -Url https://github.com/<you>/ErMao.git -Push"
    exit 0
}
Write-Host $remotes

if ($Push) {
    Write-Host "[git] 推送 main 与标签..."
    git push -u origin main
    git push origin --tags
    Write-Host "[git] 完成"
}
