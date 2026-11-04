# Downloads the pinned portable build tools (llvm-mingw, CMake, Ninja; Windows x86_64) into -Destination (default
# buildtools\artifacts\portable-tools), checking each archive's SHA-256.
# The work is done by Prepare-PortableTools.py (Python 3, standard library only).
[CmdletBinding()]
param([string]$Destination)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\..\tools\invoke-python.ps1')
$arguments = @()
if ($Destination) { $arguments += '--destination', (Resolve-PathArgument $Destination) }
Invoke-PythonScript (Join-Path $PSScriptRoot 'Prepare-PortableTools.py') $arguments
