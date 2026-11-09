# Buduje IsoRecorder (Release) do katalogu build\.
# Wymaga: VS Build Tools (C++ + CMake), Qt 6 w C:\Qt\<wersja>\msvc*_64, GStreamer MSVC x64 (runtime + development).
param([string]$QtDir, [string]$GstRoot, [string]$Config = 'Release')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot

if (-not $QtDir) {
    $QtDir = Get-ChildItem C:\Qt -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Get-ChildItem $_.FullName -Directory -Filter 'msvc*_64' } |
        Sort-Object FullName | Select-Object -Last 1 -ExpandProperty FullName
}
if (-not $QtDir) { throw 'Nie znaleziono Qt (C:\Qt\<wersja>\msvc*_64). Podaj -QtDir.' }
if (-not $GstRoot) {
    $GstRoot = $env:GSTREAMER_1_0_ROOT_MSVC_X86_64
    if (-not $GstRoot) { $GstRoot = [Environment]::GetEnvironmentVariable('GSTREAMER_1_0_ROOT_MSVC_X86_64', 'Machine') }
    if (-not $GstRoot) { $GstRoot = [Environment]::GetEnvironmentVariable('GSTREAMER_1_0_ROOT_MSVC_X86_64', 'User') }
    if (-not $GstRoot) { $d = "$env:LOCALAPPDATA\Programs\gstreamer\1.0\msvc_x86_64"; if (Test-Path $d) { $GstRoot = $d } }
}
if (-not $GstRoot) { throw 'Nie znaleziono GStreamera. Podaj -GstRoot.' }

$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

cmake -S $root -B "$root\build" -G Ninja "-DCMAKE_BUILD_TYPE=$Config" "-DCMAKE_PREFIX_PATH=$QtDir" "-DGST_ROOT=$GstRoot"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build "$root\build"
exit $LASTEXITCODE
