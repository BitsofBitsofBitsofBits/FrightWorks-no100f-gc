# loop-capture.ps1 -Label A [-Boot h001] [-Seconds 45] [-Hd] [-Cmd "scene H001" -CmdAt 20]: boot the scene in the current
# Play\ build with AUDIO_RAW_DUMP on, record, stop, then run loopcheck.py on the dump (last 20 s).
# -Hd: HD texture pack on (link + config). -Cmd: dev menu command (needs the dev menu build) sent
# -CmdAt seconds after the scene loaded. Test config from manual-session prepare; your settings, zz-manual and the
# texture link are put back afterwards (manual-session restore).
param([string]$Label = 'A', [string]$Boot = 'h001', [int]$Seconds = 45, [switch]$Hd, [string]$Cmd = '', [int]$CmdAt = 20)
$ErrorActionPreference = 'Stop'
$env:NO100F_DEV_TOOLS = '1'  # dev menu (F8) on for this session's game (built-in dev menu; older builds use the patch)
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path   # repo root
Set-Location $root
$play = Join-Path $root 'Play'
$ms = Join-Path $root 'tools\debug\manual-session.ps1'
$cfg = Join-Path $play 'UserData\Config.toml'
$link = Join-Path $play 'UserData\texture_replacements\ScoobyHD'
$out = Join-Path $root 'autotest\audio-loop'
New-Item -ItemType Directory -Force $out | Out-Null
$dump = Join-Path $out "$Label.raw"
Remove-Item $dump -ErrorAction SilentlyContinue
Get-ChildItem Env: | Where-Object { $_.Name -like 'NO100F_*' -or $_.Name -like 'AX_*' } | ForEach-Object { Remove-Item "Env:$($_.Name)" }
$start = [DateTime]::Now
$linked = $false  # only a link this script made is removed (builds stage the HD pack there as a real folder)
try {
    & powershell -ExecutionPolicy Bypass -File $ms stop-tests | Out-Null
    & powershell -ExecutionPolicy Bypass -File $ms stop | Out-Null  # closes a running game (and restores a leftover copy)
    & powershell -ExecutionPolicy Bypass -File $ms prepare -Boot $Boot | Out-Null  # saves your settings, writes boot mod + Config
    if ($LASTEXITCODE -ne 0) { throw 'manual-session prepare failed' }
    if ($Hd) {
        $text = [IO.File]::ReadAllText($cfg) -replace '(?m)^texture_replacements\s*=[^\r\n]*\r?\n', ''
        [IO.File]::WriteAllText($cfg, [regex]::Replace($text, '(?m)^\[video\][ \t]*\r?\n', "[video]`r`ntexture_replacements = true`r`n"))
        if (-not (Test-Path $link)) {
            New-Item -ItemType Junction -Path $link -Target (Join-Path $root 'texture_pack\ScoobyHD') | Out-Null
            $linked = $true
        }
    }
    Remove-Item (Join-Path $play 'devmenu.txt'), (Join-Path $play 'devmenu.log') -ErrorAction SilentlyContinue
    $env:AUDIO_RAW_DUMP = $dump
    Start-Process -FilePath (Join-Path $play 'FrightWorks Game.exe') -WorkingDirectory $play | Out-Null
    # wait for the boot scene to load
    $t = [DateTime]::Now
    while (([DateTime]::Now - $t).TotalSeconds -lt 120) {
        $c = Get-ChildItem (Join-Path $play 'UserData\Logs') -Directory | Sort-Object CreationTime -Descending | Select-Object -First 1 | ForEach-Object { Join-Path $_.FullName 'console.log' }
        if ($c -and (Test-Path $c) -and (Get-Item (Split-Path $c)).CreationTime -gt $start -and (Select-String -Path $c -Pattern "$Boot.HIP" -SimpleMatch -Quiet)) { break }
        Start-Sleep -Milliseconds 500
    }
    if ($Cmd) {
        Start-Sleep $CmdAt
        Set-Content -Path (Join-Path $play 'devmenu.txt') -Value $Cmd
        Start-Sleep ($Seconds - $CmdAt)
    } else {
        Start-Sleep $Seconds
    }
} finally {
    Get-Process 'FrightWorks Game' -ErrorAction SilentlyContinue | Stop-Process -Force
    Remove-Item Env:AUDIO_RAW_DUMP -ErrorAction SilentlyContinue
    Start-Sleep 1
    if ($linked) { [IO.Directory]::Delete($link, $false) }  # the junction only
    & powershell -ExecutionPolicy Bypass -File $ms restore | Out-Null  # your settings back, zz-manual removed
    Remove-Item (Join-Path $play 'devmenu.txt') -ErrorAction SilentlyContinue
}
if (Test-Path (Join-Path $play 'devmenu.log')) { Get-Content (Join-Path $play 'devmenu.log') }
$len = (Get-Item $dump).Length / 128000
python (Join-Path $PSScriptRoot 'loopcheck.py') $dump --skip ([math]::Max(0, [math]::Floor($len - 20))) --len 20
