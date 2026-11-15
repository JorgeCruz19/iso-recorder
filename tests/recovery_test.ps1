# Test odzyskiwania nagran po awarii: program jest zabijany w trakcie nagrania (jak przy zawieszeniu albo zaniku zasilania),
# a potem uruchamiany z --recover. Sprawdzane: przerwane nagranie jest w dzienniku, po naprawie pliki MKV maja dlugosc
# i komplet klatek, oryginaly zostaja jako *.crashed.mkv, plik opisu ma wpis "recovered", dziennik jest pusty.
# -Container ts: pliki nie wymagaja naprawy;  -Container mp4: pliki sa nie do odzyskania (zgloszone jako stracone).
# Uzycie:  tests\recovery_test.ps1 [-Container mkv|ts|mp4] [-Exe sciezka]
param([string]$Container = 'mkv', [string]$Exe = '')
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot
if (-not $Exe) { $Exe = "$root\build\IsoRecorder.exe" }
$gstBin = "$env:LOCALAPPDATA\Programs\gstreamer\1.0\msvc_x86_64\bin"
$saved = $env:PATH
$errors = 0
function Check([string]$what, [bool]$ok, [string]$got = '') {
    if ($ok) { "ok    $what  $got" } else { "BLAD  $what  $got"; $script:errors++ }
}
$profile = "recovery$(Get-Random)"
$out = Join-Path $env:TEMP "isorec_$profile"
New-Item -ItemType Directory $out | Out-Null
function Start-App([string[]]$appArgs, [string]$stdout = '') {
    if ($Exe -like '*\build\*') { $env:PATH = "C:\Qt\6.10.3\msvc2022_64\bin;$gstBin;$saved" }
    $env:ISOREC_PROFILE = $profile
    $p = if ($stdout) { Start-Process $Exe -ArgumentList $appArgs -PassThru -RedirectStandardOutput $stdout } else { Start-Process $Exe -ArgumentList $appArgs -PassThru }
    $env:PATH = $saved; $env:ISOREC_PROFILE = $null
    $p
}
function Duration([string]$file) {
    $l = @(cmd /c "ffprobe -v error -show_entries format=duration -of csv=p=0 `"$file`" 2>&1")
    if ($l.Count -and "$($l[0])" -match '^[\d.]+$') { [double]::Parse("$($l[0])", [cultureinfo]::InvariantCulture) } else { -1 }
}
function Frames([string]$file) {
    $l = @(cmd /c "ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames -of csv=p=0 `"$file`" 2>&1")
    if ($l.Count -and "$($l[0])" -match '^\d+') { [int]$Matches[0] } else { 0 }
}

# nagranie 2 kanalow z podzialem co 4 s; program zabity po 22 s (nagranie rusza ok. 6-7 s po starcie procesu)
$app = Start-App @('--demo', 2, '--lang', 'en', '--out', "`"$out`"", '--container', $Container, '--split-every', 4, '--rec-test', 60)
Start-Sleep 22
$app.Kill()
Start-Sleep 2
$files = @(Get-ChildItem $out -Filter "*.$Container" | Sort-Object Name)
$sidecars = @(Get-ChildItem $out -Filter *.json)
$inProgress = @($sidecars | ForEach-Object { Get-Content $_.FullName -Raw -Encoding utf8 | ConvertFrom-Json } | Where-Object { $_.inProgress })
Check 'po zabiciu programu sa pliki i opisy "w toku"' ($files.Count -ge 4 -and $inProgress.Count -eq 2) "(plikow $($files.Count), opisow w toku $($inProgress.Count))"
if ($Container -eq 'mkv') {
    $noDur = @($files | Where-Object { (Duration $_.FullName) -le 0 }).Count
    Check 'ostatnie czesci MKV nie maja dlugosci (przerwane)' ($noDur -ge 2) "($noDur plikow bez dlugosci)"
}
$framesBefore = 0; foreach ($f in $files) { $framesBefore += Frames $f.FullName }

$o = "$out\recover_out.txt"
$app = Start-App @('--demo', 1, '--lang', 'en', '--recover', '--dump-log', "`"$out\log.txt`"") $o
$sw = [Diagnostics.Stopwatch]::StartNew()
while (-not $app.HasExited -and $sw.Elapsed.TotalSeconds -lt 120) { Start-Sleep -Milliseconds 300 }
if (-not $app.HasExited) { $app.Kill(); Check 'naprawa zakonczyla sie sama' $false }
$line = (Get-Content $o -ErrorAction SilentlyContinue) -join ' '
Check 'dziennik: 2 przerwane nagrania, po naprawie 0' ($line -match 'recovered=2 left=0') "($line)"
$log = Get-Content "$out\log.txt" -Encoding utf8 -ErrorAction SilentlyContinue
$summary = ($log | Select-String 'Interrupted recordings').Line
$files = @(Get-ChildItem $out -Filter "*.$Container" | Where-Object { $_.Name -notlike '*.crashed.*' } | Sort-Object Name)
$rec = @($sidecars | ForEach-Object { Get-Content $_.FullName -Raw -Encoding utf8 | ConvertFrom-Json })
if ($Container -eq 'mkv') {
    $bad = @($files | Where-Object { (Duration $_.FullName) -le 0 }).Count
    $framesAfter = 0; foreach ($f in $files) { $framesAfter += Frames $f.FullName }
    $dec = 0; foreach ($f in $files) { $dec += @(cmd /c "ffmpeg -v error -i `"$($f.FullName)`" -f null - 2>&1").Count }
    Check 'po naprawie kazdy plik MKV ma dlugosc' ($bad -eq 0) "($bad bez dlugosci)"
    Check 'liczba klatek nie zmalala o wiecej niz 1 s na kanal' ($framesAfter -ge $framesBefore - 50 -and $framesAfter -ge 600) "($framesBefore -> $framesAfter)"
    Check 'naprawione pliki dekoduja sie bez bledow' ($dec -eq 0) "($dec)"
    # naprawiana jest tylko czesc przerwana (ostatnia) kazdego nagrania - czesci zamkniete przez split sa kompletne
    Check 'oryginaly przerwanych czesci zachowane jako *.crashed.mkv' (@(Get-ChildItem $out -Filter '*.crashed.mkv').Count -eq 2) "($(@(Get-ChildItem $out -Filter '*.crashed.mkv').Count))"
    Check 'opis: recovered, bez plikow straconych' (@($rec | Where-Object { $_.recovered -and $_.recovered.lost -eq 0 -and $_.recovered.repaired -eq 1 -and $_.recovered.intact -ge 2 -and -not $_.inProgress }).Count -eq 2)
} elseif ($Container -eq 'ts') {
    Check 'MPEG-TS: nic do naprawy, pliki czytelne' (@($rec | Where-Object { $_.recovered -and $_.recovered.intact -ge 2 -and $_.recovered.lost -eq 0 }).Count -eq 2 -and @($files | Where-Object { (Frames $_.FullName) -lt 20 }).Count -eq 0)
} else {
    Check 'MP4: przerwana czesc nie do odzyskania, czesci zamkniete przez split cale' (@($rec | Where-Object { $_.recovered -and $_.recovered.lost -eq 1 -and $_.recovered.intact -ge 2 }).Count -eq 2)
}
Check 'log: podsumowanie naprawy' ([bool]$summary) "($summary)"
Remove-Item $out -Recurse -Force
# sprzatanie profilu testowego (dziennik i autozapis)
Get-ChildItem "$env:LOCALAPPDATA" -Directory -Filter "*$profile*" -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
"Wynik: bledow $errors"
exit $errors
