# Shared by the debug kit's scripts (dot-source it: . "$PSScriptRoot\kit-common.ps1", adjusting the path).
#
# Several checkouts (git worktrees, see worktree.ps1) can run side by side, each with its own Play\. These helpers keep
# a script to its own checkout, and let only one checkout run the game for a test at a time.
#
#   Get-CheckoutGames <root>    game processes started from inside <root> (its Play\, autotest\ copies, ...)
#   Stop-CheckoutGames <root>   kills those, nothing else: other worktrees' games and the user's own game live on
#   Enter-GameLock <root> <why> waits until no other checkout holds the game-run lock, then takes it for <root>
#   Exit-GameLock <root>        gives it back (only if <root> holds it)
#   Get-GameLock                the current holder, or $null
#
# The lock is one file per Windows user: %LOCALAPPDATA%\FrightWorks-kit\game-run.lock (JSON: root, why, since).
# Builds don't take it; only game runs do (manual-session prepare/start take it, stop/restore give it back; play-patched
# holds it while its game is open). Two games at once fight over the screen (window captures) and the GPU (timing and
# audio measurements). A lock is stale, and taken over, when its checkout has no game running and it is older than
# 3 minutes (a script that died between prepare and launch). The user's own game (launcher, or Play\ by hand) never
# takes the lock and is never waited for.

function Get-CheckoutGames([string]$Root) {
    $prefix = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\') + '\'
    Get-CimInstance Win32_Process -Filter "Name = 'FrightWorks Game.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) }
}

function Stop-CheckoutGames([string]$Root) {
    foreach ($p in @(Get-CheckoutGames $Root)) {
        Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue
    }
}

function Get-GameLockPath {
    Join-Path $env:LOCALAPPDATA 'FrightWorks-kit\game-run.lock'
}

function Get-GameLock {
    $path = Get-GameLockPath
    try { return (Get-Content -LiteralPath $path -Raw -ErrorAction Stop | ConvertFrom-Json) } catch { return $null }
}

function Test-GameLockStale($Lock) {
    if (-not $Lock -or -not $Lock.root -or -not (Test-Path -LiteralPath $Lock.root)) { return $true }
    if (@(Get-CheckoutGames $Lock.root).Count -gt 0) { return $false }
    $since = [datetime]::MinValue
    if (-not [datetime]::TryParse([string]$Lock.since, [ref]$since)) { return $true }
    return ((Get-Date) - $since).TotalMinutes -gt 3
}

function Enter-GameLock([string]$Root, [string]$Why, [int]$WaitMinutes = 60) {
    $Root = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\')
    $path = Get-GameLockPath
    [IO.Directory]::CreateDirectory((Split-Path $path)) | Out-Null
    $deadline = (Get-Date).AddMinutes($WaitMinutes)
    $told = $false
    while ($true) {
        $lock = Get-GameLock
        if ($lock -and [string]::Equals([string]$lock.root, $Root, [StringComparison]::OrdinalIgnoreCase)) {
            $lock.since = (Get-Date).ToString('o'); $lock.why = $Why  # ours already: refresh it
            ($lock | ConvertTo-Json -Compress) | Set-Content -LiteralPath $path -Encoding utf8
            return
        }
        if ($lock -and (Test-GameLockStale $lock)) {
            Write-Host "game lock: taking over a stale lock from $($lock.root) ($($lock.why))"
            Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
            $lock = $null
        }
        if (-not $lock) {
            $json = @{ root = $Root; why = $Why; since = (Get-Date).ToString('o') } | ConvertTo-Json -Compress
            try {
                # CreateNew: of two checkouts asking at the same moment, one wins and the other waits.
                $stream = [IO.File]::Open($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
                try { $bytes = [Text.Encoding]::UTF8.GetBytes($json); $stream.Write($bytes, 0, $bytes.Length) }
                finally { $stream.Dispose() }
                return
            } catch [IO.IOException] { continue }
        }
        if ((Get-Date) -gt $deadline) {
            throw "game lock: still held by $($lock.root) ($($lock.why), since $($lock.since)) after $WaitMinutes min"
        }
        if (-not $told) {
            Write-Host "game lock: $($lock.root) is running the game ($($lock.why)); waiting (up to $WaitMinutes min)"
            $told = $true
        }
        Start-Sleep -Seconds 5
    }
}

function Exit-GameLock([string]$Root) {
    $Root = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\')
    $lock = Get-GameLock
    if ($lock -and [string]::Equals([string]$lock.root, $Root, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath (Get-GameLockPath) -Force -ErrorAction SilentlyContinue
    }
}
