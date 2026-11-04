# Populates buildtools\artifacts\dependencies (or -Destination): the pinned, unpatched upstream source trees (plus the
# prebuilt Dawn package and generated C++/WinRT headers) that the native build uses with FETCHCONTENT_FULLY_DISCONNECTED=ON.
# Every pin is the one aurora-main's own CMake declares; they are asserted against those files so the two cannot drift
# apart silently.
# The work is done by Prepare-Dependencies.py (Python 3, standard library only).
[CmdletBinding()]
param(
    [string]$Destination,
    # Windows metadata source for cppwinrt.exe: 'local' (this machine's WinMetadata), 'sdk', or an
    # installed SDK version such as 10.0.26100.0.
    [string]$CppWinRtInput = 'local'
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\..\tools\invoke-python.ps1')
$arguments = @('--cppwinrt-input', $CppWinRtInput)
if ($Destination) { $arguments += '--destination', (Resolve-PathArgument $Destination) }
Invoke-PythonScript (Join-Path $PSScriptRoot 'Prepare-Dependencies.py') $arguments
