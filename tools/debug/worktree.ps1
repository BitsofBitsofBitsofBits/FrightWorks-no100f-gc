# Parallel work: one git worktree per task, each with its own branch, Play\ and Claude Code session.
#
#   powershell -ExecutionPolicy Bypass -File tools\debug\worktree.ps1 new -Name installer -Branch feat/installer [-From origin/main] [-Build] [-NoOpen]
#   powershell -ExecutionPolicy Bypass -File tools\debug\worktree.ps1 list
#   powershell -ExecutionPolicy Bypass -File tools\debug\worktree.ps1 open -Name installer
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
# new opens a Windows Terminal tab in the worktree running `claude -n <name>` (-NoOpen: don't); open does that again
# later. -Build runs the first full build there (about 10 min; translator tools, translate, compile).
# remove refuses while the worktree has uncommitted or untracked work or a program running from it. It unlinks the
# junctions first (so nothing shared is deleted), then removes the folder. The branch is kept: delete it yourself when
# it is merged.
[CmdletBinding()]
param(
    [Parameter(Position = 0, Mandatory)][ValidateSet('new', 'list', 'open', 'remove')][string]$Command,
    [string]$Name,
    [string]$Branch,
    [string]$From = 'origin/main',
    [string]$Base,
    [switch]$Build,
    [switch]$NoOpen
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
    Join-Path $Base $Name
}

function Open-Session([string]$path, [string]$title) {
    $wt = Get-Command wt -ErrorAction SilentlyContinue
    if (-not $wt) { Write-Host "open: Windows Terminal (wt) not found; run: cd `"$path`"; claude -n $title"; return }
    # -w 0: a new tab in the current Windows Terminal window (a new window if none).
    & $wt.Source -w 0 new-tab --title $title -d $path claude -n $title
    Write-Host "open: Windows Terminal tab '$title' running claude in $path"
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
        Open-Session $path $Name
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
        Write-Text (Join-Path $path 'TASK.md') @"
# Task: $Name

Worktree of $main on branch ``$Branch`` (from $From at ``$head``, $(Get-Date -Format 'yyyy-MM-dd')).
Notes for this task go here, not in AGENTS.md (AGENTS.md is shared by every worktree: general rules only).
This worktree has its own Play\ and autotest\; Disc\, texture_pack\, toolchain\ and tools\debug\local\* are links to
the main checkout's (read only: never delete or rewrite them from here). Game runs take turns (game-run lock).

## Goal

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
        if (-not $NoOpen) { Open-Session $path $Name }
    }
    'remove' {
        $path = Get-WorktreePath
        $w = Get-Worktrees | Where-Object { $_.Path -eq $path }
        if (-not $w) { throw "$path is not a worktree of $main" }
        $status = @(& git -C $path status --porcelain)
        if ($status.Count) { throw "remove: $path has uncommitted or untracked work:`n$($status -join "`n")" }
        $prefix = $path.TrimEnd('\') + '\'
        $running = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
            $_.ExecutablePath -and $_.ExecutablePath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) })
        if ($running) { throw "remove: still running from it: $(($running | ForEach-Object { "$($_.Name) ($($_.ProcessId))" }) -join ', ')" }
        $ahead = (& git -C $main rev-list --count "origin/main..$($w.Branch)" 2>$null)
        foreach ($link in @(Get-Links $path)) {
            if ([IO.Directory]::Exists($link)) { [IO.Directory]::Delete($link, $false) } else { [IO.File]::Delete($link) }  # the link only
        }
        if (@(Get-Links $path).Count) { throw 'remove: links still present; nothing deleted' }
        Invoke-Git worktree remove --force $path
        Write-Host "remove: $path removed; branch $($w.Branch) kept ($ahead commit(s) not in origin/main)"
    }
}
