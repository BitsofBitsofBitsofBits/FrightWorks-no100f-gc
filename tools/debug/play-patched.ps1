# Play a build with debug patches in it, and take them out of the source again when the game closes.
#
#   powershell -ExecutionPolicy Bypass -File tools\debug\play-patched.ps1                   # dev menu (F8)
#   ... play-patched.ps1 -Patch devmenu,postool                                            # several patches
#   ... play-patched.ps1 -SkipTranslate        # faster: only if the last build was from this branch (see AGENTS.md)
#   ... play-patched.ps1 -Keep                 # leave the patches applied (to work on them; re-export afterwards)
#   ... play-patched.ps1 -CleanBuild           # after the game closes, rebuild Play\ without the patches
#   ... play-patched.ps1 -HdTextures           # with the HD texture pack (texture_pack\ScoobyHD, as the release ships it)
#
# Steps: git apply each tools\debug\patches\<name>.patch, build, start Play\FrightWorks Game.exe, wait for it to
# exit, then git apply -R the patches this run applied. A patch that was already applied is left as found. A patch whose
# files were edited while the game ran no longer reverses cleanly: it is left applied and the script says so.
# -HdTextures links texture_pack\ScoobyHD into Play\UserData\texture_replacements and sets [video] texture_replacements =
# true for this run only; both go back when the game closes (only that Config line: settings changed in game stay).
# The human is playing, so sound is on at 5% for the run ([audio] volume = 0.05, muted = false); those two lines go back
# the same way when the game closes.
# Removal also runs on a build failure or Ctrl+C. If the window is closed instead, run with -Remove.
#   ... play-patched.ps1 -Remove [-Patch ...]  # just take the patches out
#   ... play-patched.ps1 -NoBuild [-HdTextures] # play the build already in Play\ (e.g. from texhunt\build-devmenu.ps1)
param(
    [string[]]$Patch = @('devmenu'),
    [switch]$SkipTranslate,
    [switch]$NoBuild,
    [switch]$Keep,
    [switch]$CleanBuild,
    [switch]$HdTextures,
    [switch]$Remove
)
$ErrorActionPreference = 'Stop'
$env:NO100F_DEV_TOOLS = '1'  # dev menu (F8) on for this session's game (built-in dev menu; older builds use the patch)
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path  # this file is tools\debug\play-patched.ps1
. (Join-Path $PSScriptRoot 'kit-common.ps1')  # game-run lock while the game is open
$patchDir = Join-Path $PSScriptRoot 'patches'
$exe = Join-Path $root 'Play\FrightWorks Game.exe'
# powershell -File passes "-Patch a,b" as one string
$names = @($Patch | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })

function Git-Apply([string[]]$GitArgs) {
    & git -C $root apply @GitArgs 2>$null
    return $LASTEXITCODE -eq 0
}

$config = Join-Path $root 'Play\UserData\Config.toml'
$packLink = Join-Path $root 'Play\UserData\texture_replacements\ScoobyHD'

$added = @{}  # what Set-ConfigLine added: section -> the text it put before that section's header; '' = the file

# Sets $Key in [$Section] of Config.toml to $Line ($null = remove the line); returns the line it replaced ($null if
# none). A new line goes at the end of its section; a missing section is added at the end of the file (no Config.toml
# yet = start one). Putting the old lines back takes out again what was added: a section added here once it is empty,
# the file once it is empty (the game then writes its defaults on the next start).
function Set-ConfigLine([string]$Section, [string]$Key, $Line) {
    $bom = $false
    $text = ''
    if (Test-Path -LiteralPath $config) {
        $bytes = [IO.File]::ReadAllBytes($config)
        $bom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
        $text = [IO.File]::ReadAllText($config)
    } elseif ($null -ne $Line) {
        $added[''] = $true
    }
    $nl = if ($text.Contains("`r`n") -or -not $text) { "`r`n" } else { "`n" }
    $old = $null
    $head = [regex]::Match($text, '(?m)^\[' + [regex]::Escape($Section) + '\][ \t]*(\r?\n|\z)')
    if ($head.Success) {
        $start = $head.Index + $head.Length
        $next = [regex]::Match($text.Substring($start), '(?m)^\[')
        $end = if ($next.Success) { $start + $next.Index } else { $text.Length }
        $body = $text.Substring($start, $end - $start)
        $m = [regex]::Match($body, '(?m)^' + [regex]::Escape($Key) + '[ \t]*=[^\r\n]*(\r?\n)?')
        if ($m.Success) {
            $old = $m.Value.TrimEnd("`r", "`n")
            $pre = $body.Substring(0, $m.Index)
            if ($null -ne $Line) {
                $pre += $Line + $m.Value.Substring($old.Length)
            } elseif (-not $m.Groups[1].Success) {
                # it was the last line, with no newline: drop the newline before it (the header's, if it was the first)
                if ($pre) { $pre = $pre -replace '\r?\n\z', '' } else { $start -= $head.Groups[1].Length }
            }
            $body = $pre + $body.Substring($m.Index + $m.Length)
        } elseif ($null -ne $Line) {
            $content = $body.TrimEnd("`r", "`n")
            $body = if ($content) { $content + $nl + $Line + $body.Substring($content.Length) }
                    elseif (-not $head.Groups[1].Value) { $nl + $Line }  # the header is the file's last line, no newline
                    else { $Line + $nl + $body }
        }
        if ($null -eq $Line -and -not $body.Trim() -and $added.ContainsKey($Section)) {
            $before = $text.Substring(0, $head.Index)
            $gap = $added[$Section]
            if ($end -eq $text.Length -and $before.EndsWith($gap)) { $before = $before.Substring(0, $before.Length - $gap.Length) }
            $text = $before + $text.Substring($end)
            $added.Remove($Section)
        } else {
            $text = $text.Substring(0, $start) + $body + $text.Substring($end)
        }
    } elseif ($null -ne $Line) {
        $gap = ''
        if ($text -and -not $text.EndsWith("`n")) { $gap += $nl }
        if ($text) { $gap += $nl }
        $text += $gap + "[$Section]" + $nl + $Line + $nl
        $added[$Section] = $gap
    }
    if (-not $text.Trim() -and $added.ContainsKey('')) {
        Remove-Item -LiteralPath $config -ErrorAction SilentlyContinue
        $added.Remove('')
    } else {
        [IO.File]::WriteAllText($config, $text, (New-Object Text.UTF8Encoding($bom)))
    }
    return $old
}

function Build([switch]$Translate) {
    $buildArgs = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $root 'tools\build-no100f.ps1'), '-SkipTools')
    if (-not $Translate) { $buildArgs += '-SkipTranslate' }
    & powershell @buildArgs | Select-Object -Last 3
    if ($LASTEXITCODE -ne 0) { throw "build failed (exit $LASTEXITCODE)" }
}

$files = foreach ($n in $names) {
    $f = Join-Path $patchDir "$n.patch"
    if (-not (Test-Path $f)) { throw "no patch $f" }
    [pscustomobject]@{ Name = $n; File = $f }
}

if ($Remove) {
    foreach ($p in $files) {
        if (Git-Apply @('-R', '--check', $p.File)) { Git-Apply @('-R', $p.File) | Out-Null; "removed   $($p.Name)" }
        elseif (Git-Apply @('--check', $p.File)) { "not applied $($p.Name)" }
        else { "CHANGED   $($p.Name): its files differ from the patch; fix by hand (git diff)" }
    }
    return
}

if (Get-Process | Where-Object { $_.Path -eq $exe }) { throw "the game is already running from Play\; close it first" }
$applied = New-Object System.Collections.Generic.List[object]
& git -C $root cat-file -e 'HEAD:wiicompiled/runtime/src/no100f_devmenu.cpp' 2>$null
$builtInDevmenu = $LASTEXITCODE -eq 0
$hdOld = $null; $hdSet = $false; $linked = $false; $locked = $false
$soundOld = @{}; $soundSet = $false
try {
    if ($HdTextures) {
        $pack = Join-Path $root 'texture_pack\ScoobyHD'
        if (-not (Test-Path $pack)) { throw "no HD texture pack at $pack" }
        if (-not (Test-Path $packLink)) {
            New-Item -ItemType Directory -Force (Split-Path $packLink) | Out-Null
            New-Item -ItemType Junction -Path $packLink -Target $pack | Out-Null
            $linked = $true
        }
        $hdOld = Set-ConfigLine 'video' 'texture_replacements' 'texture_replacements = true'
        $hdSet = $true
        "hd        texture_pack\ScoobyHD on for this run"
    }
    # The human is playing: sound on at 5% for this run (AGENTS.md rule); the old lines go back when the game closes.
    $soundSet = $true
    $soundOld['volume'] = Set-ConfigLine 'audio' 'volume' 'volume = 0.05'
    $soundOld['muted'] = Set-ConfigLine 'audio' 'muted' 'muted = false'
    "sound     on at 5% for this run"
    foreach ($p in $files) {
        if ($NoBuild) { break }
        # The dev menu is part of the game since it was built in (NO100F_DEV_TOOLS above switches it on).
        if ($p.Name -eq 'devmenu' -and $builtInDevmenu) { "built in  devmenu (dev tools on for this run)"; continue }
        if (Git-Apply @('--check', $p.File)) {
            Git-Apply @($p.File) | Out-Null
            $applied.Add($p)
            "applied   $($p.Name)"
        } elseif (Git-Apply @('-R', '--check', $p.File)) {
            "already   $($p.Name) (applied before this run: left as found)"
        } else {
            throw "$($p.Name) does not apply to this branch (git apply --check $($p.File)); rebase it with git apply -3"
        }
    }

    if ($NoBuild) { "playing   $(Split-Path -Leaf $exe) as built (-NoBuild: no patches applied)" }
    else {
        Build -Translate:(-not $SkipTranslate)
        "playing   $(Split-Path -Leaf $exe) (patches come out when it closes)"
    }
    Enter-GameLock $root "play-patched $($names -join ',')"
    $locked = $true
    $game = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -PassThru
    $game.WaitForExit()
    "closed    exit code $($game.ExitCode)"
} finally {
    if ($locked) { Exit-GameLock $root }
    if ($hdSet) { Set-ConfigLine 'video' 'texture_replacements' $hdOld | Out-Null }
    if ($linked) { [IO.Directory]::Delete($packLink, $false) }  # the junction only, not the pack
    if ($hdSet -or $linked) { "hd        texture pack setting and link put back" }
    if ($soundSet) {
        foreach ($k in @('muted', 'volume')) {
            if ($soundOld.ContainsKey($k)) { Set-ConfigLine 'audio' $k $soundOld[$k] | Out-Null }
        }
        "sound     volume and mute settings put back"
    }
    if ($Keep) {
        if ($applied.Count) { "kept      $($applied.Name -join ', ') (-Keep). Remove later with -Remove" }
    } else {
        for ($i = $applied.Count - 1; $i -ge 0; --$i) {
            $p = $applied[$i]
            if (Git-Apply @('-R', '--check', $p.File)) {
                Git-Apply @('-R', $p.File) | Out-Null
                "removed   $($p.Name)"
            } else {
                "LEFT IN   $($p.Name): its files were edited since it was applied. Re-export it, then run -Remove:"
                "          git diff --output=tools/debug/patches/$($p.Name).patch HEAD -- <its files>  (git add -N new files first)"
            }
        }
    }
}

if ($CleanBuild -and -not $Keep -and $applied.Count) {
    "rebuilding Play\ without the patches"
    Build
}
