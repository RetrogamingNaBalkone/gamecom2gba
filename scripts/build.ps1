param([string]$ZigExe = $env:ZIG_EXE, [string]$GbaFix = $env:GBAFIX)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
function Find-Tool($explicit, $name, $fallback) {
    if ($explicit) { return (Get-Command $explicit -ErrorAction Stop).Source }
    $found = Get-Command $name -ErrorAction SilentlyContinue
    if ($found) { return $found.Source }
    return (Get-Item -LiteralPath $fallback -ErrorAction Stop).FullName
}
function Run($program, [string[]]$arguments) {
    & $program @arguments
    if ($LASTEXITCODE -ne 0) { throw "$program failed ($LASTEXITCODE)" }
}
$zig = Find-Tool $ZigExe zig "$env:LOCALAPPDATA/Programs/Zig/0.13.0/zig.exe"
$fix = Find-Tool $GbaFix gbafix "$env:LOCALAPPDATA/Programs/gba-tools/gbafix.exe"
$csc = "$env:WINDIR/Microsoft.NET/Framework64/v4.0.30319/csc.exe"
$build = [IO.Directory]::CreateDirectory("$root/build").FullName
$stage = [IO.Directory]::CreateDirectory("$build/$([guid]::NewGuid())").FullName
$dist = [IO.Directory]::CreateDirectory("$root/dist").FullName
$previousCache = $env:ZIG_GLOBAL_CACHE_DIR
try {
    $env:ZIG_GLOBAL_CACHE_DIR = "$build/zig-cache"
    Run $zig @('cc','-target','arm-freestanding-eabi','-mcpu=arm7tdmi','-c',"$root/gba/start.S",'-o',"$stage/start.o")
    Run $zig @('cc','-target','thumb-freestanding-eabi','-mcpu=arm7tdmi','-Oz','-ffreestanding','-fno-unwind-tables','-fno-exceptions','-Wall','-Werror','-c',"$root/gba/duke.c",'-o',"$stage/duke.o")
    Run $zig @('ld.lld','--oformat=binary','-T',"$root/gba/rom.ld","$stage/start.o","$stage/duke.o",'-o',"$stage/runtime.bin")
    Run $fix @("$stage/runtime.bin",'-tDUKE-GAMECOM','-cGDUP','-m00')
    $sources = @(Get-ChildItem "$root/converter/*.cs" | ForEach-Object FullName)
    $executable = 'gamecom2gba.exe'
    $stagedExe = Join-Path $stage $executable
    Run $csc (@('/nologo','/optimize+','/reference:System.IO.Compression.dll',"/resource:$stage/runtime.bin,runtime","/out:$stagedExe") + $sources)
    $target = Join-Path $dist $executable
    if ([IO.File]::Exists($target)) {
        $backup = "$stage/previous.exe"
        [IO.File]::Replace($stagedExe, $target, $backup)
    } else { [IO.File]::Move($stagedExe, $target) }
    Write-Output $target
} finally {
    $env:ZIG_GLOBAL_CACHE_DIR = $previousCache
    if ((Split-Path $stage) -eq $build) { Remove-Item -LiteralPath $stage -Force -Recurse }
}
