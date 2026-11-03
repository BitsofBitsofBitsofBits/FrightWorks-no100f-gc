# Parallel work: one git worktree per task, each with its own branch, Play\ and Claude Code session.
#
#   powershell -ExecutionPolicy Bypass -File tools\debug\worktree.ps1 new -Name installer -Branch feat/installer [-From origin/main] [-Task "goal"] [-Build] [-NoOpen]
#   powershell -ExecutionPolicy Bypass -File tools\debug\worktree.ps1 list
#   powershell -ExecutionPolicy Bypass -File tools\debug\worktree.ps1 open -Name installer [-Continue]
#   powershell -ExecutionPolicy Bypass -File tools\debug\worktree.ps1 open -Name main
#   powershell -ExecutionPolicy Bypass -File tools\debug\worktree.ps1 remove -Name installer
#
# Worktrees live beside the main checkout, in <its parent>\FrightWorks-worktrees\<name> (-Base to change). Each gets:
#  - its own copy of everything git tracks, on its own branch (new from -From, default origin/main; or an existing branch);
#  - its own build output (Play\, wiicompiled\native-build, generated, launcher\publish, dist) and autotest\: tests in one
#    worktree never touch another's Play\ or the user's settings in the main checkout's Play\;
#  - links (junctions) to the main checkout's big read-only inputs: Disc\, texture_pack\, toolchain\, game\,
#    wiicompiled\Dependencies and the folders in tools\debug\local (Dolphin copies, icon packs), never user-setup;
#  - AGENTS.md: a link to the main checkout's (one set of rules); CLAUDE.md loads it and TASK.md, this task's own notes.
# Builds can run side by side. Game runs take turns (kit-common.ps1, game-run lock).
#
# Sessions: new opens a Windows Terminal tab in the worktree running `claude -n <name>` (-NoOpen: don't). With -Task,
# the goal goes into TASK.md and the session starts on it right away. open starts the session again later: a new
# conversation that picks the task up from TASK.md (-Continue: claude -c, the folder's last conversation).
# `open -Name main` opens the main checkout's session: the coordinating one, which creates and removes worktrees and
# can message the others by name (session name = worktree name).
# -Build runs the first full build there (translator tools, translate, compile).
# remove refuses while the worktree has uncommitted or untracked work, a program running from it (it first stops the
# dotnet build servers a launcher or translator build leaves running there) or its Claude session. It unlinks the
# junctions first (so nothing shared is deleted), then removes the folder. The branch is kept: delete it yourself when
# it is merged.
[CmdletBinding()]
param(
    [Parameter(Position = 0, Mandatory)][ValidateSet('new', 'list', 'open', 'remove')][string]$Command,
    [string]$Name,
    [string]$Branch,
    [string]$From = 'origin/main',
    [string]$Base,
    [string]$Task,
    [switch]$Build,
    [switch]$NoOpen,
    [switch]$Continue
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0

# The main checkout (also when this runs from inside a worktree): the parent of the shared .git folder.
$here = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$common = (& git -C $here rev-parse --path-format=absolute --git-common-dir).Trim()
if ($LASTEXITCODE -ne 0) { throw 'not inside a git checkout' }
$main = (Resolve-Path (Split-Path ($common -replace '/', '\') -Parent)).Path
if (-not $Base) { $Base = Join-Path (Split-Path $main -Parent) 'FrightWorks-worktrees' }

function Invoke-Git { & git -C $main @args; if ($LASTEXITCODE -ne 0) { throw "git $($args -join ' ') failed" } }

# UTF-8 without a BOM (Windows PowerShell's -Encoding utf8 writes one; Claude Code reads CLAUDE.md's @imports per line).
function Write-Text([string]$file, [string]$text) { [IO.File]::WriteAllText($file, $text, (New-Object Text.UTF8Encoding($false))) }

function Get-Worktrees {
    $list = @(); $cur = $null
    foreach ($line in (& git -C $main worktree list --porcelain)) {
        if ($line -like 'worktree *') { if ($cur) { $list += $cur }; $cur = [ordered]@{ Path = ($line.Substring(9) -replace '/', '\'); Branch = ''; Head = '' } }
        elseif ($line -like 'HEAD *') { $cur.Head = $line.Substring(5, 7) }
        elseif ($line -like 'branch *') { $cur.Branch = $line.Substring(7) -replace '^refs/heads/', '' }
        elseif ($line -eq 'detached') { $cur.Branch = '(detached)' }
    }
    if ($cur) { $list += $cur }
    $list | ForEach-Object { [pscustomobject]$_ }
}

function Get-WorktreePath {
    if (-not $Name) { throw "-Name is required for $Command" }
    if ($Name -notmatch '^[A-Za-z0-9._-]+$') { throw "-Name '$Name': use letters, digits, . _ - only (it becomes a folder name)" }
    if ($Name -eq 'main') {
        if ($Command -eq 'open') { return $main }
        throw "-Name main is the main checkout's session; pick another name"
    }
    Join-Path $Base $Name
}

function Open-Session([string]$path, [string]$title, [string]$prompt, [bool]$continue) {
    $claude = @('claude')
    if ($continue) { $claude += '-c' }
    $claude += @('-n', $title)
    if ($prompt) { $claude += $prompt }
    $wt = Get-Command wt -ErrorAction SilentlyContinue
    if (-not $wt) { Write-Host "open: Windows Terminal (wt) not found; run: cd `"$path`"; $($claude -join ' ')"; return }
    # -w 0: a new tab in the current Windows Terminal window (a new window if none).
    & $wt.Source -w 0 new-tab --title $title -d $path @claude
    Write-Host "open: Windows Terminal tab '$title' running $(if ($continue) { 'claude -c (continuing)' } else { 'claude' }) in $path"
}

# Reparse points (junctions, symlinks) under $dir, without walking into them.
function Get-Links([string]$dir) {
    $stack = New-Object System.Collections.Generic.Stack[string]; $stack.Push($dir)
    while ($stack.Count) {
        $d = $stack.Pop()
        foreach ($e in [IO.Directory]::EnumerateFileSystemEntries($d)) {
            $attr = [IO.File]::GetAttributes($e)
            if ($attr -band [IO.FileAttributes]::ReparsePoint) { $e }
            elseif ($attr -band [IO.FileAttributes]::Directory) { $stack.Push($e) }
        }
    }
}

function Get-RunningFrom([string]$path) {
    $prefix = $path.TrimEnd('\') + '\'
    Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        $_.ExecutablePath -and $_.ExecutablePath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) }
}

switch ($Command) {
    'list' {
        $lock = $null
        . (Join-Path $PSScriptRoot 'kit-common.ps1'); $lock = Get-GameLock
        foreach ($w in Get-Worktrees) {
            $dirty = @(& git -C $w.Path status --porcelain 2>$null).Count
            $tag = if ($w.Path -eq $main) { 'main checkout' } else { Split-Path $w.Path -Leaf }
            $state = if ($dirty) { "$dirty changed/untracked" } else { 'clean' }
            $game = if ($lock -and [string]::Equals([string]$lock.root, $w.Path, [StringComparison]::OrdinalIgnoreCase)) { "  [game lock: $($lock.why)]" } else { '' }
            '{0,-18} {1,-34} {2}  {3}{4}' -f $tag, $w.Branch, $w.Head, $state, $game
            if ($w.Path -ne $main) { "                   $($w.Path)" }
        }
    }
    'open' {
        $path = Get-WorktreePath
        if (-not (Test-Path -LiteralPath $path)) { throw "no worktree $path" }
        $prompt = if ($Continue -or $Name -eq 'main') { '' } else { 'Read TASK.md and carry on with the task from its Status.' }
        Open-Session $path $Name $prompt ([bool]$Continue)
    }
    'new' {
        $path = Get-WorktreePath
        if (-not $Branch) { throw '-Branch is required (e.g. feat/installer)' }
        if (Test-Path -LiteralPath $path) { throw "$path already exists" }
        New-Item -ItemType Directory -Force $Base | Out-Null
        Invoke-Git fetch -q origin
        & git -C $main show-ref --verify --quiet "refs/heads/$Branch"
        if ($LASTEXITCODE -eq 0) {
            Write-Host "new: existing branch $Branch"
            Invoke-Git worktree add $path $Branch
        } else {
            Write-Host "new: branch $Branch from $From"
            Invoke-Git worktree add --no-track -b $Branch $path $From
        }

        $shared = @('Disc', 'texture_pack', 'toolchain', 'game', 'wiicompiled\Dependencies')
        $local = Join-Path $main 'tools\debug\local'
        if (Test-Path -LiteralPath $local) {
            $shared += Get-ChildItem -LiteralPath $local -Directory | Where-Object { $_.Name -ne 'user-setup' -and -not ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) } |
                ForEach-Object { "tools\debug\local\$($_.Name)" }
        }
        foreach ($rel in $shared) {
            $target = Join-Path $main $rel
            if (-not (Test-Path -LiteralPath $target)) { continue }
            $link = Join-Path $path $rel
            if (Test-Path -LiteralPath $link) { Write-Host "new: $rel exists in the branch, not linked"; continue }
            New-Item -ItemType Directory -Force (Split-Path $link) | Out-Null
            New-Item -ItemType Junction -Path $link -Target $target | Out-Null
            Write-Host "new: linked $rel"
        }

        # Agent instructions: AGENTS.md = the main checkout's (local file, not in git); CLAUDE.md loads it + TASK.md.
        $agents = Join-Path $main 'AGENTS.md'
        if (Test-Path -LiteralPath $agents) {
            # mklink: unprivileged with Windows Developer Mode (New-Item in Windows PowerShell needs admin for this).
            & cmd /c mklink "$(Join-Path $path 'AGENTS.md')" "$agents" | Out-Null
            if ($LASTEXITCODE -ne 0) { Write-Text (Join-Path $path 'AGENTS.md') "Read $agents (the shared agent notes).`r`n" }
        }
        Write-Text (Join-Path $path 'CLAUDE.md') "@AGENTS.md`r`n@TASK.md`r`n"
        $head = (& git -C $path rev-parse --short HEAD).Trim()
        $goal = if ($Task) { "$($Task.Trim())`r`n" } else { '' }
        Write-Text (Join-Path $path 'TASK.md') @"
# Task: $Name

Worktree of $main on branch ``$Branch`` (from $From at ``$head``, $(Get-Date -Format 'yyyy-MM-dd')).
Notes for this task go here, not in AGENTS.md (AGENTS.md is shared by every worktree: general rules only).
This worktree has its own Play\ and autotest\; Disc\, texture_pack\, toolchain\ and tools\debug\local\* are links to
the main checkout's (read only: never delete or rewrite them from here). Game runs take turns (game-run lock).

This session is named ``$Name``. The main checkout's session (named ``main``) creates and removes worktrees and may
message you (SendMessage). When the goal is done, or you are stuck on something only the human can decide, say so here
and send ``main`` a one-line status if ListAgents shows it. Keep Status below current: a session reopened later
(worktree.ps1 open) starts from this file, not from this conversation.

## Goal
$goal
## Status
"@
        # TASK.md and CLAUDE.md stay out of git (info/exclude is shared by all worktrees).
        $exclude = Join-Path ($common -replace '/', '\') 'info\exclude'
        $lines = @(Get-Content -LiteralPath $exclude -ErrorAction SilentlyContinue)
        foreach ($pattern in '/TASK.md', '/CLAUDE.md') {
            if ($lines -notcontains $pattern) { Add-Content -LiteralPath $exclude -Value $pattern }
        }

        if ($Build) {
            Write-Host 'new: first full build (translator tools, translate, compile)...'
            & powershell -ExecutionPolicy Bypass -File (Join-Path $path 'tools\build-no100f.ps1') | Select-Object -Last 3
            if ($LASTEXITCODE -ne 0) { throw "build failed (exit $LASTEXITCODE); see $path\build-logs" }
        } else {
            Write-Host 'new: not built yet; first build there: tools\build-no100f.ps1 (no -SkipTools / -SkipTranslate)'
        }
        Write-Host "new: $path ready"
        if (-not $NoOpen) {
            $prompt = if ($Task) { 'Start on the goal in TASK.md.' } else { '' }
            Open-Session $path $Name $prompt $false
        }
    }
    'remove' {
        $path = Get-WorktreePath
        $w = Get-Worktrees | Where-Object { $_.Path -eq $path }
        if (-not $w) { throw "$path is not a worktree of $main" }
        $status = @(& git -C $path status --porcelain)
        if ($status.Count) { throw "remove: $path has uncommitted or untracked work:`n$($status -join "`n")" }
        # dotnet build servers (MSBuild node, C# compiler) outlive a launcher or translator build and run from the
        # worktree's toolchain link; they hold no work, so stop them the supported way.
        $dotnet = Join-Path $path 'toolchain\dotnet\dotnet.exe'
        if (@(Get-RunningFrom $path | Where-Object { $_.Name -eq 'dotnet.exe' }).Count -and (Test-Path -LiteralPath $dotnet)) {
            Write-Host 'remove: stopping the dotnet build servers running from it'
            & $dotnet build-server shutdown | Out-Null
            for ($i = 0; $i -lt 20 -and @(Get-RunningFrom $path | Where-Object { $_.Name -eq 'dotnet.exe' }).Count; $i++) { Start-Sleep -Milliseconds 500 }
        }
        $running = @(Get-RunningFrom $path)
        if ($running.Count) { throw "remove: still running from it: $(($running | ForEach-Object { "$($_.Name) ($($_.ProcessId))" }) -join ', ')" }
        # Its Claude session (claude.exe lives elsewhere; new/open start it as `claude ... -n <name>`): removing the folder
        # under it breaks that session. Close its tab first.
        $session = @(Get-CimInstance Win32_Process -Filter "Name = 'claude.exe'" -ErrorAction SilentlyContinue | Where-Object {
            $_.CommandLine -match "(^|\s)(-n|--name)\s+`"?$([regex]::Escape($Name))`"?(\s|$)" })
        if ($session.Count) { throw "remove: its Claude session is still running (claude.exe $($session[0].ProcessId)); close the '$Name' tab first" }
        $ahead = (& git -C $main rev-list --count "origin/main..$($w.Branch)" 2>$null)
        foreach ($link in @(Get-Links $path)) {
            if ([IO.Directory]::Exists($link)) { [IO.Directory]::Delete($link, $false) } else { [IO.File]::Delete($link) }  # the link only
        }
        if (@(Get-Links $path).Count) { throw 'remove: links still present; nothing deleted' }
        Invoke-Git worktree remove --force $path
        Write-Host "remove: $path removed; branch $($w.Branch) kept ($ahead commit(s) not in origin/main)"
    }
}
