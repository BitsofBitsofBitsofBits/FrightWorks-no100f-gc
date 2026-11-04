# Parallel work: one git worktree per task, each with its own branch, Play\ and Claude Code session.
# This is a thin wrapper: the work is done by worktree.py (Python 3, standard library only; its header has the full
# description). Every argument is passed through unchanged and its exit code is returned:
#
#   powershell -ExecutionPolicy Bypass -File tools\debug\worktree.ps1 new -Name installer -Branch feat/installer [-From origin/main] [-Task "goal"] [-Build] [-NoOpen] [-Model m] [-Effort e]
#   powershell -ExecutionPolicy Bypass -File tools\debug\worktree.ps1 list
#   powershell -ExecutionPolicy Bypass -File tools\debug\worktree.ps1 open -Name installer [-Continue] [-Model m] [-Effort e]
#   powershell -ExecutionPolicy Bypass -File tools\debug\worktree.ps1 open -Name main
#   powershell -ExecutionPolicy Bypass -File tools\debug\worktree.ps1 remove -Name installer
#
# -Model / -Effort (new and open) are passed to claude as --model / --effort, only when given, for that one session:
# nothing is remembered, a later open without them uses the defaults. No semicolons in -Model (Windows Terminal splits
# its command line there).
$ErrorActionPreference = 'Stop'

# A real Python 3.8+: not the Microsoft Store stub (a zero-byte python.exe in ...\Microsoft\WindowsApps that opens the
# Store when run), so candidates there are skipped without running them.
function Find-Python {
    $check = 'import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)'
    foreach ($name in 'python', 'python3', 'py') {
        foreach ($cmd in @(Get-Command $name -CommandType Application -All -ErrorAction SilentlyContinue)) {
            if ($cmd.Source -match '\\Microsoft\\WindowsApps\\') { continue }
            $pre = @(if ($name -eq 'py') { '-3' })
            & $cmd.Source @pre -c $check 2>$null
            if ($LASTEXITCODE -eq 0) { return @{ Path = $cmd.Source; Pre = $pre } }
        }
    }
    return $null
}

$py = Find-Python
if (-not $py) {
    [Console]::Error.WriteLine('worktree.ps1: Python 3.8 or newer not found on PATH (the Microsoft Store stub does not count)')
    exit 1
}
# Windows PowerShell 5.1 mangles double quotes in native arguments, so the arguments travel as JSON in a variable
# (worktree.py reads and removes it), exactly as typed.
$env:FRIGHTWORKS_WORKTREE_ARGS = ConvertTo-Json -InputObject @($args | ForEach-Object { [string]$_ }) -Compress
$pre = $py.Pre
& $py.Path @pre (Join-Path $PSScriptRoot 'worktree.py')
$code = $LASTEXITCODE
Remove-Item Env:FRIGHTWORKS_WORKTREE_ARGS -ErrorAction SilentlyContinue
exit $code
