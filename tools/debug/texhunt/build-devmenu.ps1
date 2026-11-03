# build-devmenu.ps1 -Branch <branch>: build Play\ from that branch with the dev menu, then put the source back
# (patch removed, the branch that was checked out before). Needs a clean tree apart from untracked files.
# Branches with the dev menu built in (no100f_devmenu.cpp tracked) are built as they are: the scripts that start
# the game set NO100F_DEV_TOOLS=1. Older branches get tools\debug\patches\devmenu.patch.
# Used by spots-run.ps1 -Branch and hunt.ps1 -Branch. Works for branches without the debug kit (patch copied first).
#   e.g. main (no PR #22), pr/hd-alpha-cutoff (main + #22), release/1.0.3-beta (no #22), fix/hd-alpha-cutoff (beta + #22)
param([Parameter(Mandatory)][string]$Branch)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path   # repo root
Set-Location $root

if (git status --porcelain --untracked-files=no) { throw 'uncommitted changes: commit or stash them first' }
# The kit is tracked: a branch without it has no tools\debug, so work from a copy of the patch made before the checkout.
$patch = Join-Path ([IO.Path]::GetTempPath()) "devmenu-$PID.patch"
Copy-Item (Join-Path $root 'tools\debug\patches\devmenu.patch') $patch -Force
$before = git branch --show-current
git checkout -q $Branch
if ($LASTEXITCODE -ne 0) { throw "git checkout $Branch failed" }
& git cat-file -e 'HEAD:wiicompiled/runtime/src/no100f_devmenu.cpp' 2>$null
$builtIn = $LASTEXITCODE -eq 0
try {
    if (-not $builtIn) {
        git apply $patch
        if ($LASTEXITCODE -ne 0) { throw "devmenu.patch does not apply on $Branch" }
    }
    try {
        Remove-Item Env:NO100F_*, Env:AX_* -ErrorAction SilentlyContinue
        & powershell -ExecutionPolicy Bypass -File (Join-Path $root 'tools\build-no100f.ps1') -SkipTools *>&1 |
            Select-String -Pattern 'error|DONE|copied' | ForEach-Object { "build     $_" }
        if ($LASTEXITCODE -ne 0) { throw 'build failed' }
    } finally {
        if (-not $builtIn) { git apply -R $patch }
    }
} finally {
    git checkout -q $before
    Remove-Item $patch -ErrorAction SilentlyContinue
    "build     Play\ = $Branch + dev menu; source back on $before"
}
