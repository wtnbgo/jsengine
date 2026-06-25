<#
.SYNOPSIS
    Windows 向けリリース zip を組み立てる。

.DESCRIPTION
    ビルド済みの実行ファイル + 必要 DLL + data フォルダ + README.md + manual.js を
    ステージングフォルダに集めて zip 化する。 リリース構成は zip ルート直下に:

        jsengine-<version>-win64/
          jsengine.exe
          SDL3.dll
          SDL3_image.dll
          README.md
          manual.js
          data/            (title.webm は除外)

    ローカルでも CI (GitHub Actions) でも同じ結果になるよう、 CMake の install/CPack
    ではなくこの自己完結スクリプトでパッケージする (FetchContent 依存の install ルールが
    混入しないようにするため)。

.PARAMETER Version
    パッケージのバージョン文字列 (例 1.2.3)。 省略時は `git describe` から推定、 それも
    無ければ "dev"。 タグ名 "v1.2.3" を渡しても先頭 v は自動で落とす。

.PARAMETER Config
    ビルド構成 (Release / Debug)。 既定 Release。

.PARAMETER BuildDir
    CMake のビルドディレクトリ。 既定 build/x64-windows。 実行ファイルは
    <BuildDir>/<Config>/ にある前提。

.PARAMETER OutDir
    zip の出力先。 既定 dist。

.EXAMPLE
    pwsh tools/package_win.ps1 -Version 1.0.0
#>
[CmdletBinding()]
param(
    [string]$Version = "",
    [string]$Config = "Release",
    [string]$BuildDir = "build/x64-windows",
    [string]$OutDir = "dist"
)

$ErrorActionPreference = "Stop"

# リポジトリルート (このスクリプトの 1 つ上) へ移動して相対パスを安定させる
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

# --- バージョン決定 ---
# 優先順: 明示引数 > VERSION ファイル (単一ソース) > git describe > "dev"
if ([string]::IsNullOrWhiteSpace($Version)) {
    $verFile = Join-Path $repoRoot "VERSION"
    if (Test-Path $verFile) { $Version = (Get-Content $verFile -Raw) }
}
if ([string]::IsNullOrWhiteSpace($Version)) {
    try { $Version = (git describe --tags --always 2>$null) } catch {}
    if ([string]::IsNullOrWhiteSpace($Version)) { $Version = "dev" }
}
$Version = $Version.Trim()
if ($Version.StartsWith("v")) { $Version = $Version.Substring(1) }

$pkgName  = "jsengine-$Version-win64"
$binDir   = Join-Path $repoRoot "$BuildDir/$Config"
$stageDir = Join-Path $repoRoot "$OutDir/$pkgName"
$zipPath  = Join-Path $repoRoot "$OutDir/$pkgName.zip"

Write-Host "=== jsengine Windows package ==="
Write-Host "version : $Version"
Write-Host "config  : $Config"
Write-Host "bin dir : $binDir"
Write-Host "stage   : $stageDir"
Write-Host "zip     : $zipPath"

# --- 必須ファイルの存在チェック ---
$exe = Join-Path $binDir "jsengine.exe"
if (-not (Test-Path $exe)) {
    throw "jsengine.exe が見つかりません: $exe  (先に `make build` / cmake --build を実行してください)"
}

# --- ステージング作り直し ---
if (Test-Path $stageDir) { Remove-Item -Recurse -Force $stageDir }
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null

# --- 実行ファイル + 同梱 DLL ---
Copy-Item $exe $stageDir
# DLL は存在するものだけコピー (静的リンク構成なら無いこともある)
foreach ($dll in @("SDL3.dll", "SDL3_image.dll")) {
    $src = Join-Path $binDir $dll
    if (Test-Path $src) { Copy-Item $src $stageDir; Write-Host "  + $dll" }
    else { Write-Host "  (skip, not found) $dll" }
}

# --- ドキュメント ---
Copy-Item (Join-Path $repoRoot "README.md") $stageDir
Copy-Item (Join-Path $repoRoot "manual.js") $stageDir

# --- data フォルダ (title.webm は除外) ---
$dataDst = Join-Path $stageDir "data"
Copy-Item (Join-Path $repoRoot "data") $dataDst -Recurse
$excluded = Join-Path $dataDst "title.webm"
if (Test-Path $excluded) { Remove-Item -Force $excluded; Write-Host "  - data/title.webm (除外)" }

# --- zip 化 (既存があれば消す) ---
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
Compress-Archive -Path $stageDir -DestinationPath $zipPath -CompressionLevel Optimal

$zipSize = "{0:N1} MB" -f ((Get-Item $zipPath).Length / 1MB)
Write-Host ""
Write-Host "Packaged: $zipPath ($zipSize)"

# CI 用に出力パスを GITHUB_OUTPUT へ (ローカルでは無害)
if ($env:GITHUB_OUTPUT) {
    "zip=$zipPath"     | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
    "name=$pkgName"    | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
    "version=$Version" | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
}
