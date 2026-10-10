[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$QtBin,
    [Parameter(Mandatory = $true)][string]$MinGWBin,
    [ValidateSet('map.njust', 'map1.njust', 'map2.njust', 'map3.njust')]
    [string]$Map = 'map.njust',
    [ValidateSet(0, 90, 180, 270)][int]$Rotate = 0,
    [ValidateSet('1', '2', '4', '8', 'MAX')][string]$Frequency = '8',
    [string]$Executable,
    [string]$ResultLogFile,
    [switch]$Exam,
    [switch]$Offscreen,
    [switch]$ShowHelp
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $Executable) { $Executable = Join-Path $repoRoot '.build\release\newAOE.exe' }
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw 'Executable not found. Run scripts/build.ps1 first or set -Executable.'
}
$Executable = (Resolve-Path -LiteralPath $Executable).Path
foreach ($runtimeDir in @($QtBin, $MinGWBin)) {
    if (-not (Test-Path -LiteralPath $runtimeDir -PathType Container)) {
        throw "Runtime directory not found: $runtimeDir"
    }
}
foreach ($requiredFile in @('config.json', 'res.rcc', $Map)) {
    if (-not (Test-Path -LiteralPath (Join-Path $repoRoot $requiredFile) -PathType Leaf)) {
        throw "Required runtime file missing: $requiredFile"
    }
}
$gameArgs = @('--map', $Map, '--rotate', "$Rotate", '--freq', $Frequency)
if ($Exam) { $gameArgs += '--exam' }
if ($Offscreen) { $gameArgs += '--offscreen' }
if ($ResultLogFile) { $gameArgs += @('--ResultLogFile', $ResultLogFile) }
if ($ShowHelp) { $gameArgs = @('--help') }
$previousPath = $env:PATH
$previousPlugins = $env:QT_PLUGIN_PATH
Push-Location -LiteralPath $repoRoot
try {
    $env:PATH = "$QtBin;$MinGWBin;$previousPath"
    $env:QT_PLUGIN_PATH = Join-Path (Split-Path -Parent $QtBin) 'plugins'
    # A pipeline also makes PowerShell wait for a Qt Creator GUI-subsystem build.
    & $Executable @gameArgs | Out-Host
    $gameExitCode = $LASTEXITCODE
}
finally {
    $env:PATH = $previousPath
    $env:QT_PLUGIN_PATH = $previousPlugins
    Pop-Location
}
exit $gameExitCode
