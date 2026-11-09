# Uruchamia build\IsoRecorder.exe z Qt i GStreamerem w PATH. Argumenty sa przekazywane do programu,
# np.:  scripts\run.ps1 --demo 16      scripts\run.ps1 --check-env
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$qt = Get-ChildItem C:\Qt -Directory | ForEach-Object { Get-ChildItem $_.FullName -Directory -Filter 'msvc*_64' } |
    Sort-Object FullName | Select-Object -Last 1 -ExpandProperty FullName
$gst = $env:GSTREAMER_1_0_ROOT_MSVC_X86_64
if (-not $gst) { $gst = [Environment]::GetEnvironmentVariable('GSTREAMER_1_0_ROOT_MSVC_X86_64', 'Machine') }
if (-not $gst) { $gst = [Environment]::GetEnvironmentVariable('GSTREAMER_1_0_ROOT_MSVC_X86_64', 'User') }
if (-not $gst) { $d = "$env:LOCALAPPDATA\Programs\gstreamer\1.0\msvc_x86_64"; if (Test-Path $d) { $gst = $d } }
$env:PATH = "$qt\bin;$($gst.TrimEnd('\'))\bin;$env:PATH"
$p = Start-Process "$root\build\IsoRecorder.exe" -ArgumentList $args -PassThru -Wait -NoNewWindow
exit $p.ExitCode
