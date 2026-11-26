param([string]$Originals, [string]$ZigExe = $env:ZIG_EXE,
      [string]$PythonExe, [string]$Tigerbyte = $env:TIGERBYTE_HOME)
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot)
if (!$Originals) { $Originals = "$root/originals" }
if (!$Tigerbyte) { $Tigerbyte = "$env:LOCALAPPDATA/Programs/Tigerbyte" }
if (!$ZigExe) {
    $command = Get-Command zig -ErrorAction SilentlyContinue
    $ZigExe = if ($command) { $command.Source } else { "$env:LOCALAPPDATA/Programs/Zig/0.13.0/zig.exe" }
}
if (!$PythonExe) { $PythonExe = (Get-Command py).Source }
$build = [IO.Directory]::CreateDirectory("$root/build/duke3d").FullName
$stage = [IO.Directory]::CreateDirectory("$build/$([guid]::NewGuid())").FullName
$previousCache = $env:ZIG_GLOBAL_CACHE_DIR
try {
    $env:ZIG_GLOBAL_CACHE_DIR = "$root/build/zig-cache"
    $sources = @(Get-ChildItem "$Tigerbyte/src/cpu/*.c","$Tigerbyte/src/sys/*.c" | ForEach-Object FullName)
    & $ZigExe cc -O2 "-I$Tigerbyte/src" "$root/analysis/duke3d/reference.c" @sources -o "$stage/reference.exe"
    if ($LASTEXITCODE) { throw 'Reference build failed' }
    $cartDir = Join-Path $Originals 'duke3d'
    if (!(Test-Path -LiteralPath $cartDir)) { $cartDir = $Originals }
    & $PythonExe "$PSScriptRoot/test_preview.py" "$cartDir/Duke Nukem 3D (USA).zip" --bios-dir "$Originals" --reference "$stage/reference.exe"
    if ($LASTEXITCODE) { throw 'Reference comparison failed' }
} finally {
    $env:ZIG_GLOBAL_CACHE_DIR = $previousCache
    if ((Split-Path $stage) -eq $build) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
