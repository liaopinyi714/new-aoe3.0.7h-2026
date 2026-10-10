[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$QtBin,
    [Parameter(Mandatory = $true)][string]$MinGWBin,
    [ValidateRange(1, 32)][int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
$qmakeExe = Join-Path $QtBin 'qmake.exe'
$makeExe = Join-Path $MinGWBin 'mingw32-make.exe'
foreach ($toolPath in @($qmakeExe, $makeExe, (Join-Path $MinGWBin 'g++.exe'))) {
    if (-not (Test-Path -LiteralPath $toolPath -PathType Leaf)) {
        throw "Required build tool not found: $toolPath"
    }
}
$buildDir = Join-Path $repoRoot '.build'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
$previousPath = $env:PATH
Push-Location -LiteralPath $buildDir
try {
    $env:PATH = "$QtBin;$MinGWBin;$previousPath"
    & $qmakeExe (Join-Path $repoRoot 'newAOE.pro') 'CONFIG+=release' 'CONFIG-=debug' 'CONFIG+=console'
    if ($LASTEXITCODE -ne 0) { throw "qmake failed with exit code $LASTEXITCODE" }
    & $makeExe "-j$Jobs" 'release'
    if ($LASTEXITCODE -ne 0) { throw "Release build failed with exit code $LASTEXITCODE" }
    $builtExe = Join-Path $buildDir 'release\newAOE.exe'
    if (-not (Test-Path -LiteralPath $builtExe -PathType Leaf)) {
        throw "Build completed without expected executable: $builtExe"
    }
    Write-Output "Release executable: $builtExe"
}
finally {
    $env:PATH = $previousPath
    Pop-Location
}
