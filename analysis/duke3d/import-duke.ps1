param([Parameter(Mandatory)][string]$Cartridge, [string]$GhidraHome = $env:GHIDRA_HOME,
      [ValidateSet('Startup','Actors','Menu')][string]$View='Startup')
$ErrorActionPreference = 'Stop'
if (!$GhidraHome) { $GhidraHome = "$env:LOCALAPPDATA/Programs/Ghidra/ghidra_12.1.2_PUBLIC" }
$root = Split-Path (Split-Path $PSScriptRoot)
$projects = [IO.Directory]::CreateDirectory("$root/build/duke3d/ghidra").FullName
$stage = [IO.Directory]::CreateDirectory("$root/build/duke3d/$([guid]::NewGuid())").FullName
try {
    $source = (Get-Item -LiteralPath $Cartridge).FullName
    $expected = 'FAF6912C5094477FA83A19864F43EB2ACD5929166D8B9267689D6661F5CEB5BD'
    if ([IO.Path]::GetExtension($source) -eq '.zip') {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zip = [IO.Compression.ZipFile]::OpenRead($source)
        try {
            foreach ($entry in $zip.Entries) {
                if ($entry.Length -ne 2097152) { continue }
                $stream = $entry.Open()
                try { $hash = (Get-FileHash -InputStream $stream -Algorithm SHA256).Hash }
                finally { $stream.Dispose() }
                if ($hash -eq $expected) {
                    $source = "$stage/cart.bin"
                    [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$source)
                    break
                }
            }
        } finally { $zip.Dispose() }
    }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $expected) { throw 'Unknown ROM' }
    $cart = [IO.File]::ReadAllBytes($source)
    $banks = [byte[]]::new(3 * 8192)
    $project="Duke$View"
    $scriptName=@{Startup='DukeEntry.java'; Actors='DukeActors.java'; Menu='DukeMenu.java'}[$View]
    $menuBank=@{Startup=0x21; Actors=0x22; Menu=0x23}[$View]
    $pages=@(0x20,$menuBank,0x26)
    for ($i=0; $i -lt $pages.Count; $i++) {
        [Array]::Copy($cart, $pages[$i] * 8192, $banks, $i * 8192, 8192)
    }
    $binary = "$stage/$project.bin"
    [IO.File]::WriteAllBytes($binary,$banks)
    $headless = "$GhidraHome/support/analyzeHeadless.bat"
    if (Test-Path "$projects/$project.gpr") {
        & $headless $projects $project -process "$project.bin" -noanalysis -scriptPath "$PSScriptRoot/ghidra" -postScript $scriptName
    } else {
        & $headless $projects $project -import $binary -processor SM8500:BE:16:cart -loader BinaryLoader -loader-baseAddr 4000 -noanalysis -scriptPath "$PSScriptRoot/ghidra" -postScript $scriptName
    }
    if ($LASTEXITCODE) { throw "Ghidra returned $LASTEXITCODE" }
} finally {
    if ((Split-Path $stage) -eq (Get-Item "$root/build/duke3d").FullName) {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}
