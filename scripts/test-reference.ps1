param([string]$Originals, [string]$ZigExe = $env:ZIG_EXE,
      [string]$PythonExe, [string]$Tigerbyte = $env:TIGERBYTE_HOME)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
if (!$Originals) { $Originals = "$root/originals" }
if (!$Tigerbyte) { $Tigerbyte = "$env:LOCALAPPDATA/Programs/Tigerbyte" }
if (!$ZigExe) {
    $command = Get-Command zig -ErrorAction SilentlyContinue
    $ZigExe = if ($command) { $command.Source } else { "$env:LOCALAPPDATA/Programs/Zig/0.13.0/zig.exe" }
}
if (!$PythonExe) { $PythonExe = (Get-Command py).Source }
$build = [IO.Directory]::CreateDirectory("$root/build").FullName
$stage = [IO.Directory]::CreateDirectory("$build/$([guid]::NewGuid())").FullName
$previousCache = $env:ZIG_GLOBAL_CACHE_DIR
try {
    $env:ZIG_GLOBAL_CACHE_DIR = "$build/zig-cache"
    $sources = @(Get-ChildItem "$Tigerbyte/src/cpu/*.c","$Tigerbyte/src/sys/*.c" | ForEach-Object FullName)
    & $ZigExe cc -O2 "-I$Tigerbyte/src" "$root/analysis/reference.c" @sources -o "$stage/reference.exe"
    if ($LASTEXITCODE) { throw 'Reference build failed' }
    & $PythonExe "$PSScriptRoot/test_preview.py" "$Originals/Duke Nukem 3D (USA).zip" --reference "$stage/reference.exe"
    if ($LASTEXITCODE) { throw 'Reference comparison failed' }
} finally {
    $env:ZIG_GLOBAL_CACHE_DIR = $previousCache
    if ((Split-Path $stage) -eq $build) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
