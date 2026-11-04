# Shared by the thin .ps1 wrappers around the build scripts that are written in Python 3 (standard library only, so they
# also run on Linux: `python3 tools/get-dotnet.py`). Dot-source it:  . (Join-Path $PSScriptRoot 'invoke-python.ps1')
# Must parse in Windows PowerShell 5.1.

# The path of a working Python 3.8+: python, py, python3 on PATH, in that order. A command that doesn't run is skipped,
# which is what rules out the Microsoft Store stub (WindowsApps\python.exe: it only prints a hint or opens the Store) and
# a Python 2 or a broken launcher. $null when there is none.
function Find-Python {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        foreach ($name in 'python', 'py', 'python3') {
            foreach ($command in @(Get-Command $name -CommandType Application -All -ErrorAction SilentlyContinue)) {
                try {
                    & $command.Source -c 'import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)' 2>&1 | Out-Null
                    if ($LASTEXITCODE -eq 0) { return $command.Source }
                } catch { }
            }
        }
    } finally { $ErrorActionPreference = $previous }
    return $null
}

# A path argument for Python: resolved against PowerShell's current location, without a trailing separator (Windows
# PowerShell 5.1 would pass "C:\a b\" so that the closing quote is escaped).
function Resolve-PathArgument([string]$Path) {
    $full = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
    if ($full.Length -gt 3) { $full = $full.TrimEnd('\', '/') }
    return $full
}

# Runs a script of this repository with Python (isolated mode, UTF-8) and turns its output back into what the .ps1 used
# to produce: "WARNING: x" lines become warnings, other lines Write-Host (the information stream, so a caller that
# captures the output doesn't get them), "ERROR: x" lines are kept for the exception thrown when the exit code isn't 0.
# With -LastLineIsResult the last line goes to the output stream instead (get-dotnet.ps1: the path of dotnet).
function Invoke-PythonScript([string]$Script, [string[]]$Arguments = @(), [switch]$LastLineIsResult) {
    $python = Find-Python
    if (-not $python) { throw "Python 3 is required to run $(Split-Path -Leaf $Script) and wasn't found on PATH (python, py or python3; the Microsoft Store stub doesn't count). Install it from https://www.python.org/downloads/ ." }
    $outputEncoding = $null
    try { $outputEncoding = [Console]::OutputEncoding; [Console]::OutputEncoding = New-Object Text.UTF8Encoding($false) } catch { $outputEncoding = $null }
    $errors = New-Object Collections.Generic.List[string]
    $held = $null
    try {
        # -I: neither the current folder nor PYTHON* variables decide what gets imported; -X utf8: output is UTF-8.
        & $python -I -X utf8 $Script @Arguments | ForEach-Object {
            $line = "$_"
            if ($line.StartsWith('WARNING: ')) { Write-Warning $line.Substring(9) }
            elseif ($line.StartsWith('ERROR: ')) { $errors.Add($line.Substring(7)) }
            elseif ($LastLineIsResult) { if ($null -ne $held) { Write-Host $held }; $held = $line }
            else { Write-Host $line }
        }
        $code = $LASTEXITCODE
    } finally {
        if ($outputEncoding) { try { [Console]::OutputEncoding = $outputEncoding } catch { } }
    }
    if ($code -ne 0) {
        if ($null -ne $held) { Write-Host $held }
        if ($errors.Count -gt 0) { throw $errors[$errors.Count - 1] }
        throw "$(Split-Path -Leaf $Script) failed with exit code $code."
    }
    if ($null -ne $held) { $held }
}
