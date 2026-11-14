# Test drugiej lokalizacji zapisu.
#   A. zapis podwojny: te same czesci w obu folderach, te same liczby klatek, kopia pliku opisu
#   B. awaria drugiego folderu w trakcie (kopia dostaje niemozliwa sciezke): nagranie glowne trwa i konczy sie poprawnie
#   C. folder zapasowy: w trakcie nagrania "konczy sie" miejsce -> nastepna czesc w folderze zapasowym, bez utraty klatek
#   D. folder zapasowy od startu: miejsca brakuje juz przy starcie -> cale nagranie w folderze zapasowym
#   E. zapis podwojny do tego samego folderu co glowny: nagranie pojedyncze, z komunikatem
# Uzycie:  tests\dir2_test.ps1 [-Exe sciezka]
param([string]$Exe = '')
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot
if (-not $Exe) { $Exe = "$root\build\IsoRecorder.exe" }
$gstBin = "$env:LOCALAPPDATA\Programs\gstreamer\1.0\msvc_x86_64\bin"
$saved = $env:PATH
$errors = 0
function Check([string]$what, [bool]$ok, [string]$got = '') {
    if ($ok) { "ok    $what  $got" } else { "BLAD  $what  $got"; $script:errors++ }
}
function Run([string[]]$extra, [int]$seconds = 10) {
    $base = Join-Path $env:TEMP "isorec_dir2_$(Get-Random)"
    New-Item -ItemType Directory "$base\main", "$base\second" | Out-Null
    if ($Exe -like '*\build\*') { $env:PATH = "C:\Qt\6.10.3\msvc2022_64\bin;$gstBin;$saved" }
    $env:ISOREC_PROFILE = 'dir2test'
    $a = @('--demo', 1, '--lang', 'en', '--out', "`"$base\main`"", '--dir2', "`"$base\second`"", '--rec-test', $seconds, '--dump-log', "`"$base\log.txt`"") + $extra
    $p = Start-Process $Exe -ArgumentList $a -PassThru
    $env:PATH = $saved; $env:ISOREC_PROFILE = $null
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while (-not $p.HasExited -and $sw.Elapsed.TotalSeconds -lt ($seconds + 50)) { Start-Sleep -Milliseconds 300 }
    if (-not $p.HasExited) { $p.Kill(); Check 'program zakonczyl sie sam' $false }
    $base
}
function Frames([string]$file) {
    $lines = @(cmd /c "ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames -of csv=p=0 `"$file`" 2>&1")
    if ($lines.Count -and "$($lines[0])" -match '^\d+') { [int]$Matches[0] } else { 0 }
}
function DecodeErrors([string]$file) { @(cmd /c "ffmpeg -v error -i `"$file`" -fps_mode passthrough -f null - 2>&1" | Where-Object { $_ -notmatch 'non monotonically' }).Count }
function Sidecar([string]$dir) { Get-ChildItem $dir -Filter *.json | Select-Object -First 1 | ForEach-Object { Get-Content $_.FullName -Raw -Encoding utf8 | ConvertFrom-Json } }
function Log([string]$base) { Get-Content "$base\log.txt" -Encoding utf8 -ErrorAction SilentlyContinue }

'--- A. zapis podwojny, split co 3 s'
$b = Run @('--dir2-mode', 'mirror', '--split-every', 3)
$m = @(Get-ChildItem "$b\main" -Filter *.mkv | Sort-Object Name); $s = @(Get-ChildItem "$b\second" -Filter *.mkv | Sort-Object Name)
$fm = @($m | ForEach-Object { Frames $_.FullName }); $fs = @($s | ForEach-Object { Frames $_.FullName })
$j = Sidecar "$b\main"
Check 'tyle samo czesci w obu folderach (3-4), te same nazwy' ($m.Count -ge 3 -and $m.Count -le 4 -and $m.Count -eq $s.Count -and -not (Compare-Object $m.Name $s.Name)) "($($m.Count) i $($s.Count))"
Check 'te same liczby klatek w kazdej czesci, razem 10 s' ((($fm -join ',') -eq ($fs -join ',')) -and (($fm | Measure-Object -Sum).Sum -ge 246) -and (($fm | Measure-Object -Sum).Sum -le 252)) "($($fm -join ',') | $($fs -join ','))"
$dec = 0; foreach ($f in $s) { $dec += DecodeErrors $f.FullName }
Check 'kopia dekoduje sie bez bledow' ($dec -eq 0) "($dec)"
Check 'opis nagrania w obu folderach, bez awarii drugiego' ($j.clean -and $j.secondFolder -and -not $j.secondFolderFailed -and (Test-Path (Join-Path "$b\second" (Get-ChildItem "$b\main" -Filter *.json)[0].Name)))
Remove-Item $b -Recurse -Force

'--- B. awaria drugiego folderu w 4. sekundzie nagrania'
$b = Run @('--dir2-mode', 'mirror', '--mirror-break-at', 7)
$j = Sidecar "$b\main"
$frames = 0; foreach ($p in $j.parts) { $frames += Frames (Join-Path "$b\main" $p.file) }
Check 'nagranie glowne pelne i zamkniete poprawnie' ($j.clean -and $frames -ge 246 -and $frames -le 252) "($frames klatek, czesci $($j.parts.Count))"
Check 'opis: drugi folder zawiodl' ($j.secondFolderFailed -eq $true)
Check 'log: komunikat o awarii drugiego folderu' (@(Log $b | Select-String 'second folder failed').Count -eq 1)
$dec = 0; foreach ($p in $j.parts) { $dec += DecodeErrors (Join-Path "$b\main" $p.file) }
Check 'pliki glowne dekoduja sie bez bledow' ($dec -eq 0) "($dec)"
Remove-Item $b -Recurse -Force

'--- C. folder zapasowy: miejsce "konczy sie" w 4. sekundzie nagrania'
$b = Run @('--dir2-mode', 'spare', '--spare-mb', 2048, '--fake-free-at', 7, 100)
$j = Sidecar "$b\main"
$m = @(Get-ChildItem "$b\main" -Filter *.mkv); $s = @(Get-ChildItem "$b\second" -Filter *.mkv)
$frames = 0; foreach ($f in ($m + $s)) { $frames += Frames $f.FullName }
Check 'pierwsza czesc w folderze glownym, druga w zapasowym' ($m.Count -eq 1 -and $s.Count -eq 1 -and $j.parts.Count -eq 2) "(glowny $($m.Count), zapasowy $($s.Count))"
Check 'komplet klatek z obu czesci (10 s), nagranie zamkniete poprawnie' ($j.clean -and $frames -ge 246 -and $frames -le 252) "($frames klatek)"
Check 'log: przejscie do folderu zapasowego' (@(Log $b | Select-String 'next file part goes to the spare folder').Count -eq 1)
Remove-Item $b -Recurse -Force

'--- D. folder zapasowy od startu (prog wyzszy niz wolne miejsce)'
$b = Run @('--dir2-mode', 'spare', '--spare-mb', 900000000) 5
$m = @(Get-ChildItem "$b\main" -Filter *.mkv); $s = @(Get-ChildItem "$b\second" -Filter *.mkv)
Check 'nagranie w folderze zapasowym, glowny pusty' ($m.Count -eq 0 -and $s.Count -eq 1 -and (Frames $s[0].FullName) -ge 121) "(glowny $($m.Count), zapasowy $($s.Count))"
Remove-Item $b -Recurse -Force

'--- E. zapis podwojny do tego samego folderu'
$b = Join-Path $env:TEMP "isorec_dir2_$(Get-Random)"; New-Item -ItemType Directory "$b\main" | Out-Null
if ($Exe -like '*\build\*') { $env:PATH = "C:\Qt\6.10.3\msvc2022_64\bin;$gstBin;$saved" }
$env:ISOREC_PROFILE = 'dir2test'
$p = Start-Process $Exe -ArgumentList @('--demo', 1, '--lang', 'en', '--out', "`"$b\main`"", '--dir2', "`"$b\main`"", '--dir2-mode', 'mirror', '--rec-test', 5, '--dump-log', "`"$b\log.txt`"") -PassThru
$env:PATH = $saved; $env:ISOREC_PROFILE = $null
$sw = [Diagnostics.Stopwatch]::StartNew(); while (-not $p.HasExited -and $sw.Elapsed.TotalSeconds -lt 50) { Start-Sleep -Milliseconds 300 }
if (-not $p.HasExited) { $p.Kill() }
Check 'jedno nagranie, komunikat o drugim folderze' ((@(Get-ChildItem "$b\main" -Filter *.mkv).Count -eq 1) -and (@(Log $b | Select-String 'cannot be used').Count -eq 1))
Remove-Item $b -Recurse -Force
"Wynik: bledow $errors"
exit $errors
