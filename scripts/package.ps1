# Buduje program i sklada samodzielna paczke w dist\IsoRecorder\ (zastepuje poprzednia).
# Paczka zawiera EXE, biblioteki Qt i caly GStreamer - uruchamia sie dwuklikiem, bez instalowania czegokolwiek.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$dist = "$root\dist\IsoRecorder"

& "$PSScriptRoot\build.ps1"
if ($LASTEXITCODE) { throw 'Budowanie nie powiodlo sie.' }

$qt = Get-ChildItem C:\Qt -Directory | ForEach-Object { Get-ChildItem $_.FullName -Directory -Filter 'msvc*_64' } |
    Sort-Object FullName | Select-Object -Last 1 -ExpandProperty FullName
$gst = $env:GSTREAMER_1_0_ROOT_MSVC_X86_64
if (-not $gst) { $gst = [Environment]::GetEnvironmentVariable('GSTREAMER_1_0_ROOT_MSVC_X86_64', 'Machine') }
if (-not $gst) { $gst = [Environment]::GetEnvironmentVariable('GSTREAMER_1_0_ROOT_MSVC_X86_64', 'User') }
if (-not $gst) { $gst = "$env:LOCALAPPDATA\Programs\gstreamer\1.0\msvc_x86_64" }
$gst = $gst.TrimEnd('\')
if (-not (Test-Path "$gst\bin\gstreamer-1.0-0.dll")) { throw "Nie znaleziono GStreamera w $gst" }

if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Force $dist, "$dist\gstreamer-1.0", "$dist\gio\modules" | Out-Null

Copy-Item "$root\build\IsoRecorder.exe" $dist
& "$qt\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw --compiler-runtime "$dist\IsoRecorder.exe" | Out-Null
if ($LASTEXITCODE) { throw 'windeployqt nie powiodl sie.' }

Copy-Item "$gst\bin\*.dll" $dist
Copy-Item "$gst\lib\gstreamer-1.0\*.dll" "$dist\gstreamer-1.0"
$scanner = Get-ChildItem $gst -Recurse -Filter 'gst-plugin-scanner.exe' | Select-Object -First 1
if ($scanner) { Copy-Item $scanner.FullName $dist }
if (Test-Path "$gst\lib\gio\modules") { Copy-Item "$gst\lib\gio\modules\*.dll" "$dist\gio\modules" }

$version = (Select-String -Path "$root\CMakeLists.txt" -Pattern 'APP_VERSION_SHORT "([^"]+)"').Matches[0].Groups[1].Value
Set-Content "$dist\WERSJA.txt" "ISO Recorder $version`r`nZbudowano: $(Get-Date -Format 'yyyy-MM-dd HH:mm')" -Encoding utf8
$size = (Get-ChildItem $dist -Recurse -File | Measure-Object Length -Sum).Sum
"Paczka: $dist  (wersja $version, $([math]::Round($size / 1MB)) MB)"
