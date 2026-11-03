# Puts the files a game folder needs besides the program into its UserData folder: the built-in HD texture pack
# (texture_pack\ScoobyHD, kept out of git, downloaded when missing) and the button prompt icons
# (tools\make-button-icons.py).
#   powershell -ExecutionPolicy Bypass -File tools\stage-userdata.ps1 -UserData <folder> [-Required]
# Used by build-no100f.ps1 (Play\UserData, after every build) and package-scooby.ps1 (the release). Only what changed
# is copied or rebuilt. -Required (packaging) stops on anything missing; without it a missing piece is a warning, so a
# test build still finishes.
[CmdletBinding()] param([Parameter(Mandatory)][string]$UserData, [switch]$Required)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Fail([string]$Text) {
    if ($Required) { throw $Text }
    Write-Warning $Text
}

New-Item -ItemType Directory -Force $UserData | Out-Null

# 1. HD texture pack. texture_pack\ScoobyHD (git-ignored) = dogerki's pack, downloaded from the Internet Archive the
# first time, plus tools\hd-pack-additions\ScoobyHD on top. UserData gets a mirror of it (other packs in
# texture_replacements are left alone).
$packSource = Join-Path $root 'texture_pack\ScoobyHD'
$packUrl = 'https://archive.org/download/dolphin-scooby-doo-night-of-100-frights-hd-texture-pack/' +
           'Dolphin%20Scooby%20Doo%20Night%20of%20100%20Frights%20HD%20Texture%20Pack.rar'
$packSha1 = '1E55E7F802BD0E5AD00538FB21D17A66417F723B'
if (-not (Test-Path -LiteralPath $packSource)) {
    $rar = Join-Path $root 'texture_pack\ScoobyHD.rar'
    $unpack = Join-Path $root 'texture_pack\ScoobyHD.unpack'
    try {
        New-Item -ItemType Directory -Force (Split-Path $rar) | Out-Null
        if (-not ((Test-Path -LiteralPath $rar) -and (Get-FileHash -LiteralPath $rar -Algorithm SHA1).Hash -eq $packSha1)) {
            Write-Host "HD pack: downloading dogerki's HD texture pack (604 MB) from archive.org..."
            curl.exe -fL --retry 3 -o $rar $packUrl
            if ($LASTEXITCODE -ne 0) { throw "download failed (curl exit code $LASTEXITCODE)" }
            if ((Get-FileHash -LiteralPath $rar -Algorithm SHA1).Hash -ne $packSha1) { throw "the download's SHA-1 doesn't match" }
        }
        Remove-Item -Recurse -Force $unpack -ErrorAction SilentlyContinue
        New-Item -ItemType Directory -Force $unpack | Out-Null
        # RAR5: Windows' tar (libarchive) reads it; 7-Zip if that fails (older Windows 10).
        & "$env:SystemRoot\System32\tar.exe" -xf $rar -C $unpack
        if ($LASTEXITCODE -ne 0) {
            $sevenZip = @(Get-Command 7z -CommandType Application -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source) +
                        @("$env:ProgramFiles\7-Zip\7z.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
            if (-not $sevenZip) { throw "can't unpack the RAR: Windows tar failed and 7-Zip isn't installed" }
            & $sevenZip x -y "-o$unpack" $rar | Out-Null
            if ($LASTEXITCODE -ne 0) { throw "7-Zip failed to unpack the RAR" }
        }
        # The textures sit in one game-ID folder (GIHP78).
        $folder = Get-ChildItem -LiteralPath $unpack -Directory | Select-Object -First 1
        if (-not $folder -or -not (Get-ChildItem -LiteralPath $folder.FullName -Filter *.png | Select-Object -First 1)) {
            throw "no texture folder in the RAR"
        }
        Move-Item -LiteralPath $folder.FullName $packSource
        Remove-Item -Force $rar
        Write-Host "HD pack: unpacked to $packSource"
    } catch {
        Fail "HD pack: $($_.Exception.Message). Download it by hand from $packUrl and put the PNGs in $packSource"
    } finally {
        Remove-Item -Recurse -Force $unpack -ErrorAction SilentlyContinue
    }
}
if (Test-Path -LiteralPath $packSource) {
    robocopy (Join-Path $PSScriptRoot 'hd-pack-additions\ScoobyHD') $packSource /E /NJH /NJS /NFL /NDL /NP /R:1 /W:1 | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "copying the HD pack additions failed (robocopy exit code $LASTEXITCODE)" }
}
$pack = Join-Path $UserData 'texture_replacements\ScoobyHD'
if (Test-Path -LiteralPath $packSource) {
    New-Item -ItemType Directory -Force $pack | Out-Null
    robocopy $packSource $pack /MIR /NJH /NJS /NFL /NDL /NP /R:1 /W:1 | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "copying the HD pack failed (robocopy exit code $LASTEXITCODE)" }
    $global:LASTEXITCODE = 0
    Write-Host "HD pack: $pack ($((Get-ChildItem -LiteralPath $pack -File).Count) files)"
} else {
    Fail "texture_pack\ScoobyHD is missing (the built-in HD texture pack): $pack not updated"
}

# 2. Button prompt icons, drawn from tools\button-art over the HD pack's layout. Rebuilt when the tool or the art is
# newer than the icons (or the folder has the old layout).
$icons = Join-Path $UserData 'button_icons'
$layout = Join-Path $icons 'layout.txt'
$tool = Join-Path $root 'tools\make-button-icons.py'
$inputs = @(Get-Item -LiteralPath $tool) + @(Get-ChildItem -LiteralPath (Join-Path $root 'tools\button-art') -Recurse -File)
$newest = ($inputs | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1).LastWriteTimeUtc
if ((Test-Path -LiteralPath $layout) -and (Get-Item -LiteralPath $layout).LastWriteTimeUtc -ge $newest) {
    Write-Host "Button icons: $icons (up to date)"
    exit 0
}
$python = Get-Command python, py -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not (Get-Command magick -CommandType Application -ErrorAction SilentlyContinue)) {
    # ImageMagick's installer doesn't always put itself on PATH.
    $im = Get-ChildItem "$env:ProgramFiles\ImageMagick-*" -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    if ($im) { $env:PATH = "$($im.FullName);$env:PATH" }
}
if (-not $python -or -not (Get-Command magick -CommandType Application -ErrorAction SilentlyContinue)) {
    Fail "button icons need Python and ImageMagick (magick) on PATH: $icons not updated"
    exit 0
}
if (-not (Test-Path -LiteralPath $packSource)) { Fail "button icons need texture_pack\ScoobyHD: $icons not updated"; exit 0 }
Remove-Item -Recurse -Force $icons -ErrorAction SilentlyContinue
& $python.Source $tool --out $icons | Out-Null
if ($LASTEXITCODE -ne 0) { throw "make-button-icons.py failed" }
Write-Host "Button icons: $icons (rebuilt)"
