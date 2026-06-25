<#
.SYNOPSIS
    プロジェクトのバージョンを一括更新する (単一ソース VERSION + vcpkg.json)。

.DESCRIPTION
    バージョンの単一ソースは リポジトリルートの `VERSION` ファイル。 CMakeLists.txt は
    configure 時に VERSION を読むので触らない。 vcpkg.json の "version" は VERSION と
    揃える必要があるため、 このスクリプトが両方をまとめて書き換える。

    実行後の流れ (リリースする場合):
        git add VERSION vcpkg.json
        git commit -m "Bump version to x.y.z"
        git tag vx.y.z
        git push origin main --tags        # → GitHub Actions が Release を作成

.PARAMETER Version
    新しいバージョン (semver: MAJOR.MINOR.PATCH、 例 0.2.0)。 先頭 v は自動で落とす。

.PARAMETER Tag
    指定すると VERSION 更新後に `git tag v<version>` まで作成する (push はしない)。

.EXAMPLE
    pwsh tools/bump_version.ps1 -Version 0.2.0
.EXAMPLE
    pwsh tools/bump_version.ps1 -Version 0.2.0 -Tag
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [switch]$Tag
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

# 先頭 v を許容して落とす
$Version = $Version.Trim()
if ($Version.StartsWith("v")) { $Version = $Version.Substring(1) }

# semver (X.Y.Z、 任意で -prerelease) の検証。 CMake project() VERSION は数値 3 連のみ
# 受け付けるので、 prerelease 付きは VERSION ファイル運用上は許すが警告する。
if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?$') {
    throw "バージョン形式が不正: '$Version' (期待: MAJOR.MINOR.PATCH、 例 0.2.0)"
}
if ($Version -match '-') {
    Write-Warning "prerelease 付きバージョンは CMake project() VERSION では数値部のみ使われます。"
}

# --- VERSION ファイル ---
$verFile = Join-Path $repoRoot "VERSION"
Set-Content -Path $verFile -Value $Version -NoNewline -Encoding utf8
Add-Content -Path $verFile -Value "`n" -NoNewline -Encoding utf8
Write-Host "VERSION        -> $Version"

# --- vcpkg.json の version (数値部のみ。 vcpkg は semver-prerelease を別フィールドで扱うため) ---
$vcpkgPath = Join-Path $repoRoot "vcpkg.json"
$vcpkgNumeric = ($Version -split '-')[0]
$lines = Get-Content $vcpkgPath
$updated = $false
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^\s*"version"\s*:') {
        $lines[$i] = ($lines[$i] -replace '("version"\s*:\s*")[^"]*(")', "`${1}$vcpkgNumeric`${2}")
        $updated = $true
        break
    }
}
if (-not $updated) { throw 'vcpkg.json に "version" フィールドが見つかりません' }
Set-Content -Path $vcpkgPath -Value $lines -Encoding utf8
Write-Host "vcpkg.json     -> $vcpkgNumeric"

Write-Host ""
Write-Host "次の手順:"
Write-Host "  git add VERSION vcpkg.json"
Write-Host "  git commit -m `"Bump version to $Version`""
Write-Host "  git tag v$Version && git push origin main --tags"

if ($Tag) {
    & git tag "v$Version"
    Write-Host ""
    Write-Host "git tag v$Version を作成しました (push は未実行)。"
}
