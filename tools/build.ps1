# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Sebastian Merz
<#
.SYNOPSIS
  Builds out\ExtraStations.SA.asi (32-bit, as GTA San Andreas needs) with the Visual Studio C++ compiler.

.DESCRIPTION
  Looks for vcvarsall.bat of the newest Visual Studio under Program Files, compiles src\ExtraStations.cpp as a DLL with
  the static runtime and names it .asi. Object files go to build_tmp\ (both folders are ignored by git).
  Pass -VcVars to name vcvarsall.bat yourself.
#>
param([string]$VcVars)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $VcVars) {
    $found = foreach ($base in $env:ProgramFiles, ${env:ProgramFiles(x86)}) {
        Get-ChildItem -Path (Join-Path $base 'Microsoft Visual Studio') -Recurse -Filter vcvarsall.bat -ErrorAction SilentlyContinue
    }
    $VcVars = ($found | Sort-Object FullName -Descending | Select-Object -First 1).FullName
}
if (-not $VcVars -or -not (Test-Path -LiteralPath $VcVars)) { throw 'vcvarsall.bat not found - install Visual Studio with the C++ workload or pass -VcVars' }

New-Item -ItemType Directory -Force -Path (Join-Path $root 'build_tmp'), (Join-Path $root 'out') | Out-Null
$cmd = "call `"$VcVars`" x86 >nul && cd /d `"$root`" && cl /nologo /O2 /EHsc /std:c++17 /LD /MT /Fobuild_tmp\ /Fe:out\ExtraStations.SA.asi src\ExtraStations.cpp /link /DLL"
& cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw 'the build failed' }
Remove-Item -ErrorAction SilentlyContinue (Join-Path $root 'out\ExtraStations.SA.exp'), (Join-Path $root 'out\ExtraStations.SA.lib')
$asi = Get-Item (Join-Path $root 'out\ExtraStations.SA.asi')
Write-Host ("Built {0} ({1} bytes, {2})" -f $asi.FullName, $asi.Length, $asi.LastWriteTime)
