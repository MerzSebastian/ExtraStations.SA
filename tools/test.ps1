# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Sebastian Merz
<#
.SYNOPSIS
  Compiles and runs every unit test in tests\ (plain console programs without dependencies).

.DESCRIPTION
  Looks for vcvarsall.bat of the newest Visual Studio under Program Files, like build.ps1. The tests do not touch the
  game, so they are built for the machine's own architecture. Exits with an error if a test fails.
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

$out = Join-Path $root 'build_tmp'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$failed = @()
foreach ($test in Get-ChildItem (Join-Path $root 'tests') -Filter '*_test.cpp') {
    $exe = Join-Path $out ($test.BaseName + '.exe')
    $cmd = "call `"$VcVars`" x64 >nul && cd /d `"$root`" && cl /nologo /O2 /EHsc /std:c++17 /Fo`"$out\\`" /Fe:`"$exe`" `"$($test.FullName)`" >nul && `"$exe`""
    & cmd /c $cmd | Out-Null
    if ($LASTEXITCODE -eq 0) { Write-Host ("passed  " + $test.BaseName) } else { Write-Host ("FAILED  " + $test.BaseName); $failed += $test.BaseName }
}
if ($failed) { throw ("tests failed: " + ($failed -join ', ')) }
Write-Host 'all tests passed'
