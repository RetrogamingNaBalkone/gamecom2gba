param([string]$OutputDirectory, [string]$ZigExe = $env:ZIG_EXE, [string]$GbaFix = $env:GBAFIX)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
if (!$OutputDirectory) { $OutputDirectory = "$root/dist" }
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
$dist = [IO.Directory]::CreateDirectory($OutputDirectory).FullName
$previousCache = $env:ZIG_GLOBAL_CACHE_DIR
try {
    $env:ZIG_GLOBAL_CACHE_DIR = "$root/build/zig-cache"
    Run $zig @('cc','-target','arm-freestanding-eabi','-mcpu=arm7tdmi','-c',"$root/gba/duke3d/start.S",'-o',"$stage/start.o")
    Run $zig @('cc','-target','thumb-freestanding-eabi','-mcpu=arm7tdmi','-Oz','-ffreestanding','-fno-unwind-tables','-fno-exceptions','-Wall','-Werror','-c',"$root/gba/duke3d/duke.c",'-o',"$stage/duke.o")
    Run $zig @('ld.lld','--oformat=binary','-T',"$root/gba/duke3d/rom.ld","$stage/start.o","$stage/duke.o",'-o',"$stage/runtime.bin")
    Run $fix @("$stage/runtime.bin",'-tDUKE-GAMECOM','-cGDUP','-m00')
    Run $zig @('cc','-target','arm-freestanding-eabi','-mcpu=arm7tdmi','-c',"$root/gba/resident-evil-2/start.S",'-o',"$stage/re2-start.o")
    $objects = @("$stage/re2-start.o")
    foreach ($source in (Get-ChildItem "$root/gba/resident-evil-2/*.c")) {
        $object = "$stage/re2-$($source.BaseName).o"
        Run $zig @('cc','-target','thumb-freestanding-eabi','-mcpu=arm7tdmi','-Oz','-ffreestanding','-fno-unwind-tables','-fno-exceptions','-Wall','-Werror','-c',$source.FullName,'-o',$object)
        $objects += $object
    }
    Run $zig (@('ld.lld','--oformat=binary','-T',"$root/gba/resident-evil-2/rom.ld") + $objects + @('-o',"$stage/re2.bin"))
    Run $fix @("$stage/re2.bin",'-tRE2-GAMECOM','-cGRE2','-m00')
    Run $zig @('cc','-target','arm-freestanding-eabi','-mcpu=arm7tdmi','-c',"$root/gba/indy-500/start.S",'-o',"$stage/indy-start.o")
    $objects = @("$stage/indy-start.o")
    foreach ($source in @(Get-ChildItem "$root/gba/*.c","$root/gba/indy-500/*.c")) {
        $object = "$stage/indy-$($source.BaseName).o"
        $mode = if (@('sm8500','gamecom','pcm_isr') -contains $source.BaseName) { 'arm-freestanding-eabi' } else { 'thumb-freestanding-eabi' }
        Run $zig @('cc','-target',$mode,'-mcpu=arm7tdmi','-O2','-ffreestanding','-fno-unwind-tables','-fno-exceptions','-Wall','-Werror','-c',$source.FullName,'-o',$object)
        $objects += $object
    }
    Run $zig (@('ld.lld','-T',"$root/gba/indy-500/rom.ld") + $objects + @('-o',"$stage/indy.elf"))
    Run $zig @('objcopy','-O','binary',"$stage/indy.elf","$stage/indy.bin")
    Copy-Item "$stage/indy.elf" ([IO.Directory]::CreateDirectory("$build/indy-500").FullName + "/runtime.elf")
    Run $fix @("$stage/indy.bin",'-tINDY-GAMECOM','-cGIND','-m00')
    $sources = @(Get-ChildItem "$root/converter/*.cs","$root/converter/duke3d/*.cs","$root/converter/resident-evil-2/*.cs","$root/converter/indy-500/*.cs" | ForEach-Object FullName)
    $executable = 'gamecom2gba.exe'
    $stagedExe = Join-Path $stage $executable
    Run $csc (@('/nologo','/optimize+','/reference:System.IO.Compression.dll',"/resource:$stage/runtime.bin,runtime","/resource:$stage/re2.bin,resident-evil-2","/resource:$stage/indy.bin,indy-500","/out:$stagedExe") + $sources)
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
