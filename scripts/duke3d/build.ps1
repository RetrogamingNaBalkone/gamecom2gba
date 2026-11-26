param([string]$ZigExe = $env:ZIG_EXE, [string]$GbaFix = $env:GBAFIX)
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot)
& "$root/scripts/build.ps1" -OutputDirectory "$root/dist/duke3d" -ZigExe $ZigExe -GbaFix $GbaFix
