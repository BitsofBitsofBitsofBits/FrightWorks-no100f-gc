# Assembles dist\FrightWorks: one exe on top (the launcher, named "FrightWorks.exe"), README.txt, and
# data\ with the game's runtime files. No disc data.
#   powershell -ExecutionPolicy Bypass -File tools\package-scooby.ps1 [-Build] [-Zip]
# -Build first rebuilds the game (build-no100f.ps1 -SkipTools) and the launcher.
# -Zip also writes dist\FrightWorks.zip from a clean copy (never the player's disc, saves or settings).
# Updating dist in place only replaces program files: data\Disc and data\UserData (saves, Config.toml, mods) are kept,
# so a disc set up there by the launcher survives a repackage. An old Play\ layout is moved into data\ first.
[CmdletBinding()] param([switch]$Build, [switch]$Zip)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$launcher = "$root\launcher\publish\FrightWorks.exe"
if ($Build) { & "$PSScriptRoot\build-no100f.ps1" -SkipTools; & "$PSScriptRoot\build-launcher.ps1" }
elseif (-not (Test-Path -LiteralPath $launcher)) { & "$PSScriptRoot\build-launcher.ps1" }

# Program files only: what a new download replaces.
function Stage([string]$to) {
    New-Item -ItemType Directory -Force "$to\data\UserData\mods" | Out-Null
    Copy-Item -LiteralPath $launcher -Destination "$to\FrightWorks.exe" -Force
    Copy-Item -LiteralPath "$PSScriptRoot\package-README.txt" -Destination "$to\README.txt" -Force
    foreach ($f in 'FrightWorks Game.exe', '*.dll', 'dsp_coef.bin', 'initial_pipeline_cache.db', 'portable.txt') {
        if (Test-Path "$root\Play\$f") { Copy-Item "$root\Play\$f" "$to\data" -Force }
    }
    # Built-in HD texture pack (texture_pack\ScoobyHD, kept out of git) and the button prompt icons: the same files
    # every Play\ build gets.
    & "$PSScriptRoot\stage-userdata.ps1" -UserData "$to\data\UserData" -Required
}

$dist = "$root\dist\FrightWorks"
New-Item -ItemType Directory -Force $dist | Out-Null
# Old layout (Play\ + Disc\ + "...Launcher.exe"): keep the player's UserData and disc, drop the rest.
if (Test-Path "$dist\Play") {
    New-Item -ItemType Directory -Force "$dist\data" | Out-Null
    if ((Test-Path "$dist\Play\UserData") -and -not (Test-Path "$dist\data\UserData")) { Move-Item "$dist\Play\UserData" "$dist\data\UserData" }
    if ((Test-Path "$dist\Disc\DATA") -and -not (Test-Path "$dist\data\Disc\DATA")) {
        New-Item -ItemType Directory -Force "$dist\data\Disc" | Out-Null; Move-Item "$dist\Disc\DATA" "$dist\data\Disc\DATA"
    }
    # The launcher wrote dvd_root relative to Play\UserData; data\Disc sits one level closer.
    $cfg = "$dist\data\UserData\Config.toml"
    if (Test-Path $cfg) { (Get-Content $cfg) -replace "^dvd_root = '../../Disc/DATA'", "dvd_root = '../Disc/DATA'" | Set-Content $cfg }
    Remove-Item -Recurse -Force "$dist\Play", "$dist\Disc" -ErrorAction SilentlyContinue
    Remove-Item -Force "$dist\Scooby-Doo Refrightened Launcher.exe" -ErrorAction SilentlyContinue
}
Stage $dist
$size = [math]::Round((Get-ChildItem -LiteralPath $dist -Recurse -File | ? FullName -notmatch '\\data\\(Disc|UserData)\\' | Measure-Object Length -Sum).Sum / 1MB)
Write-Host "DONE: $dist ($size MB of program files; data\Disc and data\UserData left as they were)"
if ($Zip) {
    $clean = Join-Path $env:TEMP "FrightWorks-zip\FrightWorks"
    Remove-Item -Recurse -Force (Split-Path $clean) -ErrorAction SilentlyContinue
    Stage $clean
    # Entries with forward slashes (Compress-Archive in Windows PowerShell 5.1 writes backslashes, which some
    # unzip tools reject).
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $zipPath = "$root\dist\FrightWorks.zip"
    Remove-Item -LiteralPath $zipPath -ErrorAction SilentlyContinue
    $archive = [IO.Compression.ZipFile]::Open($zipPath, 'Create')
    try {
        $base = Split-Path $clean
        foreach ($f in Get-ChildItem -LiteralPath $clean -Recurse -File) {
            $name = $f.FullName.Substring($base.Length + 1).Replace('\', '/')
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $f.FullName, $name, 'Optimal') | Out-Null
        }
        $archive.CreateEntry('FrightWorks/data/UserData/mods/') | Out-Null  # keep the empty mods folder
    } finally { $archive.Dispose() }
    Remove-Item -Recurse -Force (Split-Path $clean)
    Write-Host "DONE: $root\dist\FrightWorks.zip"
}
