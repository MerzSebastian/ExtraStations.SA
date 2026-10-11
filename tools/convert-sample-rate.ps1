# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Sebastian Merz
<#
.SYNOPSIS
  Converts the audio files of a station folder to one sample rate, so the plugin has nothing to convert while playing.

.DESCRIPTION
  Copies the whole folder to "<Folder>-<Rate>" and converts every audio file whose sample rate differs from -Rate
  (ffmpeg with the high quality "soxr" resampler). Files with the right rate are copied untouched. .ogg files are
  encoded again as Ogg Vorbis (quality -Quality), .wav stay .wav, mp3 files are encoded as Ogg Vorbis.
  Needs ffmpeg and ffprobe in the PATH.

  Better than converting a file that was already compressed: convert from the original (lossless) source and encode once.
  See docs/SAMPLE_RATES.md.

.EXAMPLE
  .\convert-sample-rate.ps1 -Folder "C:\...\scripts\ExtraStations.SA\MyStation"
  .\convert-sample-rate.ps1 -Folder .\MyStation -Rate 48000 -Quality 7
#>
param(
    [Parameter(Mandatory = $true)][string]$Folder,
    [int]$Rate = 44100,
    [double]$Quality = 6
)

$ErrorActionPreference = 'Stop'
foreach ($tool in 'ffmpeg', 'ffprobe') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "$tool was not found in the PATH" }
}
$source = (Resolve-Path -LiteralPath $Folder).Path.TrimEnd('\')
$target = "$source-$Rate"
if (Test-Path -LiteralPath $target) { throw "$target exists already - remove it or choose another rate" }

$extensions = '.ogg', '.mp3', '.wav', '.wma', '.m4a', '.aac', '.flac', '.wmv'
$converted = 0; $copied = 0
Push-Location -LiteralPath $source
Get-ChildItem -Recurse -File | ForEach-Object {
    $relative = (Resolve-Path -LiteralPath $_.FullName -Relative).Substring(2)   # relative to the station folder
    $out = Join-Path $target $relative
    New-Item -ItemType Directory -Force -Path (Split-Path $out) | Out-Null
    if ($extensions -notcontains $_.Extension.ToLower()) { Copy-Item -LiteralPath $_.FullName -Destination $out; return }

    $current = & ffprobe -v error -select_streams a:0 -show_entries stream=sample_rate -of csv=p=0 -- $_.FullName
    if ([int]$current -eq $Rate) { Copy-Item -LiteralPath $_.FullName -Destination $out; $copied++; return }

    $codec = if ($_.Extension.ToLower() -eq '.wav') { @('-c:a', 'pcm_s16le') } else { @('-c:a', 'libvorbis', '-q:a', "$Quality") }
    if ($_.Extension.ToLower() -notin '.wav', '.ogg') { $out = [IO.Path]::ChangeExtension($out, '.ogg') }
    & ffmpeg -v error -y -i $_.FullName -vn -af 'aresample=resampler=soxr:precision=28' -ar $Rate -ac 2 @codec $out
    if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed for $($_.FullName)" }
    Write-Host ("{0,6} Hz -> {1} Hz  {2}" -f $current, $Rate, $relative)
    $converted++
}
Pop-Location
Write-Host "Done: $converted converted, $copied already at $Rate Hz. Result: $target"
