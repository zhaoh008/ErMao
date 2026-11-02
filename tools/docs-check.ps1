# 文档引用检查：确保 docs 里提到的源文件都真实存在
# 用法：powershell -NoProfile -File tools\docs-check.ps1
# 退出码：0 = 全部存在；1 = 有失效引用
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$searchDirs = @(
    (Join-Path $root "engine"),
    (Join-Path $root "engine\src"),
    (Join-Path $root "engine\shaders"),
    (Join-Path $root "third_party")
)
$docFiles = @()
$docFiles += Get-ChildItem -Path (Join-Path $root "docs") -Filter *.md -File
$docFiles += Get-ChildItem -Path (Join-Path $root "docs\lectures") -Filter *.md -File -ErrorAction SilentlyContinue

# 只匹配本工程源码引用；hlsl 放在 h 前面，避免匹配到 "PBR.h" 这种前缀
$pattern = '[A-Za-z0-9_\-]+/[A-Za-z0-9_\-/]+\.(hlsl|cpp|ps1|h)(?![A-Za-z0-9])'
$skipLine = '已删除|历史版本|计划|如果要做|落地步骤|新增 |预计 '
$skipRef = 'ParticleSystem|BehaviorTree|GpuCull'
$missing = @()

foreach ($doc in $docFiles) {
    $lines = Get-Content $doc.FullName
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        if ($line -match $skipLine) { continue }
        foreach ($m in [regex]::Matches($line, $pattern)) {
            $ref = $m.Value
            if ($ref -match '^(https?|docs|build|dist)') { continue }
            if ($ref -match $skipRef) { continue }
            $found = $false
            foreach ($dir in $searchDirs) {
                if (Test-Path (Join-Path $dir $ref)) { $found = $true; break }
            }
            if (-not $found -and (Test-Path (Join-Path $root $ref))) { $found = $true }
            if (-not $found) {
                $missing += [pscustomobject]@{ File = $doc.Name; Line = $i + 1; Ref = $ref }
            }
        }
    }
}

if ($missing.Count -eq 0) {
    Write-Host ("文档引用检查通过：{0} 篇文档" -f $docFiles.Count) -ForegroundColor Green
    exit 0
}

Write-Host ("发现 {0} 处失效引用：" -f $missing.Count) -ForegroundColor Yellow
$missing | ForEach-Object { Write-Host ("  {0}:{1}  {2}" -f $_.File, $_.Line, $_.Ref) }
exit 1
