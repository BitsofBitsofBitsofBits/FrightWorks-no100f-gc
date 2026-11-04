# Returns the path of a dotnet.exe with the .NET 10 SDK: the portable one in toolchain\dotnet, installed there on first
# use with Microsoft's dotnet-install script (nothing is installed system-wide). A toolchain\dotnet from before the move
# to .NET 10 gets the new SDK beside its old one. The last line of the output is the path.
# The work is done by get-dotnet.py (Python 3, standard library only; on Linux run that directly).
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'invoke-python.ps1')
Invoke-PythonScript (Join-Path $PSScriptRoot 'get-dotnet.py') -LastLineIsResult
