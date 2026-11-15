# Buduje instalator dist\IsoRecorder-<wersja>-setup.exe z gotowej paczki dist\IsoRecorder (najpierw scripts\package.ps1).
# Wymaga Inno Setup 6 (winget install JRSoftware.InnoSetup).
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
if (-not (Test-Path "$root\dist\IsoRecorder\IsoRecorder.exe")) { throw 'Brak paczki dist\IsoRecorder - uruchom najpierw scripts\package.ps1.' }
$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw 'Nie znaleziono Inno Setup 6 (ISCC.exe). Zainstaluj: winget install JRSoftware.InnoSetup' }
$version = (Select-String -Path "$root\CMakeLists.txt" -Pattern 'APP_VERSION_SHORT "([^"]+)"').Matches[0].Groups[1].Value
& $iscc "/DAppVersion=$version" /Qp "$root\installer\IsoRecorder.iss"
if ($LASTEXITCODE) { throw 'Budowanie instalatora nie powiodlo sie.' }
$setup = Get-Item "$root\dist\IsoRecorder-$version-setup.exe"
"Instalator: $($setup.FullName)  ($([math]::Round($setup.Length / 1MB)) MB)"
