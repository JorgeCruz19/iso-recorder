# Test "growing file" (MP4 / MOV zapisywane we fragmentach, indeks na poczatku).
#   A. w trakcie nagrywania plik jest czytelny (ffprobe) i jego dlugosc ROSNIE; po zakonczeniu ma komplet klatek i dekoduje sie
#      bez bledow - dla H.264 w MP4, H.265 w MOV, ProRes w MOV
#   B. split co 3 s i pauza: ciaglosc czasu, komplet klatek
#   C. zapis podwojny: obie kopie czytelne w trakcie i po zakonczeniu
#   D. program zabity w trakcie: informacja, czy plik MP4 jest czytelny (bywa nie - znane ograniczenie); Recovery nie obiecuje naprawy
#   E. zwykly MP4 (bez growing) w trakcie zapisu jest nieczytelny - kontrola, ze test cos mierzy
# Uzycie:  tests\growing_test.ps1 [-Exe sciezka]
param([string]$Exe = '')
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot
if (-not $Exe) { $Exe = "$root\build\IsoRecorder.exe" }
$gstBin = "$env:LOCALAPPDATA\Programs\gstreamer\1.0\msvc_x86_64\bin"
$saved = $env:PATH
$errors = 0
function Check([string]$what, [bool]$ok, [string]$got = '') { if ($ok) { "ok    $what  $got" } else { "BLAD  $what  $got"; $script:errors++ } }
function Duration([string]$file) {
    $l = @(cmd /c "ffprobe -v error -show_entries format=duration -of csv=p=0 `"$file`" 2>&1")
    if ($l.Count -and "$($l[0])" -match '^[\d.]+$') { [double]::Parse("$($l[0])", [cultureinfo]::InvariantCulture) } else { -1 }
}
function Frames([string]$file) {
    $l = @(cmd /c "ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames -of csv=p=0 `"$file`" 2>&1")
    if ($l.Count -and "$($l[0])" -match '^\d+') { [int]$Matches[0] } else { 0 }
}
function DecodeErrors([string]$file) { @(cmd /c "ffmpeg -v error -i `"$file`" -fps_mode passthrough -f null - 2>&1" | Where-Object { $_ -notmatch 'non monotonically' }).Count }
function Start-App([string[]]$extra, [string]$out, [string]$profile = 'growtest') {
    if ($Exe -like '*\build\*') { $env:PATH = "C:\Qt\6.10.3\msvc2022_64\bin;$gstBin;$saved" }
    $env:ISOREC_PROFILE = $profile
    $p = Start-Process $Exe -ArgumentList (@('--demo', 1, '--lang', 'en', '--out', "`"$out`"", '--growing', '--dump-log', "`"$out\log.txt`"") + $extra) -PassThru
    $env:PATH = $saved; $env:ISOREC_PROFILE = $null
    $p
}
function NewOut { $d = Join-Path $env:TEMP "isorec_grow_$(Get-Random)"; New-Item -ItemType Directory $d | Out-Null; $d }
function Wait-Exit($p, [int]$limit) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while (-not $p.HasExited -and $sw.Elapsed.TotalSeconds -lt $limit) { Start-Sleep -Milliseconds 300 }
    if (-not $p.HasExited) { $p.Kill(); Check 'program zakonczyl sie sam' $false }
}
function Sidecar([string]$dir) { Get-ChildItem $dir -Filter *.json | Select-Object -First 1 | ForEach-Object { Get-Content $_.FullName -Raw -Encoding utf8 | ConvertFrom-Json } }

'--- A. plik czytelny i rosnacy w trakcie nagrywania (12 s)'
foreach ($pc in @(@('h264', 'mp4', 1, 'mp4'), @('h265', 'mov', 1, 'mov'), @('prores', 'mov', 1, 'mov'))) {
    $codec, $cont = $pc[0], $pc[1]
    $out = NewOut
    $p = Start-App @('--codec', $codec, '--container', $cont, '--rec-delay', 5, '--rec-test', 12) $out
    Start-Sleep 13; $f = Get-ChildItem $out -Filter "*.$cont" | Select-Object -First 1
    # 3 odczyty co 0,7 s w dwoch momentach: odczyt w chwili zapisu fragmentu bywa chybiony, wiec wymagamy 2 z 3
    $s1 = @(1..3 | ForEach-Object { if ($f) { Duration $f.FullName }; Start-Sleep -Milliseconds 700 })
    Start-Sleep 2
    $s2 = @(1..3 | ForEach-Object { if ($f) { Duration $f.FullName }; Start-Sleep -Milliseconds 700 })
    $d1 = ($s1 | Measure-Object -Maximum).Maximum; $d2 = ($s2 | Measure-Object -Maximum).Maximum
    $okReads = @($s1 + $s2 | Where-Object { $_ -gt 0 }).Count
    Wait-Exit $p 60
    $f = Get-ChildItem $out -Filter "*.$cont" | Select-Object -First 1
    $fr = Frames $f.FullName; $dec = DecodeErrors $f.FullName; $j = Sidecar $out
    Check ("{0,-6} w {1}: w trakcie czytelny i rosnie" -f $codec, $cont) ($okReads -ge 5 -and $d1 -gt 0 -and $d2 -gt $d1 + 1.0) ("({0}/6 odczytow udanych; {1:N1} s -> {2:N1} s)" -f $okReads, $d1, $d2)
    Check ("{0,-6} w {1}: po zakonczeniu 300 klatek, bez bledow, opis growing" -f $codec, $cont) ($j.clean -and $j.growing -and $fr -ge 296 -and $fr -le 304 -and $dec -eq 0) "($fr klatek, bledow $dec)"
    Remove-Item $out -Recurse -Force
}

'--- B. split co 3 s oraz pauza (H.264 w MP4)'
$out = NewOut
$p = Start-App @('--codec', 'h264', '--container', 'mp4', '--split-every', 3, '--rec-test', 10) $out; Wait-Exit $p 60
$j = Sidecar $out; $frames = 0; $dec = 0; foreach ($part in $j.parts) { $f = Join-Path $out $part.file; $frames += Frames $f; $dec += DecodeErrors $f }
Check 'split: 3-4 czesci, razem 10 s, bez bledow' ($j.clean -and $j.parts.Count -ge 3 -and $j.parts.Count -le 4 -and $frames -ge 246 -and $frames -le 252 -and $dec -eq 0) "($($j.parts.Count) czesci, $frames klatek)"
Remove-Item $out -Recurse -Force
$out = NewOut
$p = Start-App @('--codec', 'h264', '--container', 'mp4', '--pause-at', 3, '--pause-for', 3, '--rec-test', 10) $out; Wait-Exit $p 60
$j = Sidecar $out; $f = Join-Path $out $j.parts[0].file; $fr = Frames $f
Check 'pauza 3 s: jeden plik, 7 s materialu, jedna pauza' ($j.clean -and $j.parts.Count -eq 1 -and @($j.pauses).Count -eq 1 -and $fr -ge 171 -and $fr -le 179 -and (DecodeErrors $f) -eq 0) "($fr klatek)"
Remove-Item $out -Recurse -Force

'--- C. zapis podwojny (MP4)'
$out = NewOut; $out2 = Join-Path $env:TEMP "isorec_grow2_$(Get-Random)"; New-Item -ItemType Directory $out2 | Out-Null
$p = Start-App @('--codec', 'h264', '--container', 'mp4', '--dir2', "`"$out2`"", '--dir2-mode', 'mirror', '--rec-delay', 5, '--rec-test', 12) $out
Start-Sleep 12; $a = Get-ChildItem $out -Filter *.mp4 | Select-Object -First 1; $b = Get-ChildItem $out2 -Filter *.mp4 | Select-Object -First 1
$da = if ($a) { Duration $a.FullName } else { -1 }; $db = if ($b) { Duration $b.FullName } else { -1 }
Wait-Exit $p 60
$a = Get-ChildItem $out -Filter *.mp4 | Select-Object -First 1; $b = Get-ChildItem $out2 -Filter *.mp4 | Select-Object -First 1
Check 'obie kopie czytelne w trakcie nagrywania' ($da -gt 0 -and $db -gt 0) ("(glowna {0:N1} s, druga {1:N1} s)" -f $da, $db)
Check 'obie kopie maja te same klatki po zakonczeniu' ((Frames $a.FullName) -eq (Frames $b.FullName) -and (Frames $a.FullName) -ge 296) "($(Frames $a.FullName) i $(Frames $b.FullName))"
Remove-Item $out, $out2 -Recurse -Force

'--- D. program zabity w trakcie nagrywania MP4 (growing), potem odzyskiwanie'
$out = NewOut; $profile = "growrec$(Get-Random)"
$p = Start-App @('--codec', 'h264', '--container', 'mp4', '--rec-delay', 5, '--rec-test', 60) $out $profile
Start-Sleep 21; $p.Kill(); Start-Sleep 2
$f = Get-ChildItem $out -Filter *.mp4 | Select-Object -First 1
$dk = Duration $f.FullName; $frk = Frames $f.FullName
# Informacja, nie warunek: rosnacy MP4 po zabiciu programu bywa nieczytelny (ok. 3 z 8 prob) - patrz CHANGELOG 3.4
"info  zabity program: plik MP4 {0} ({1:N1} s, $frk klatek)" -f $(if ($dk -gt 0) { 'czytelny' } else { 'NIECZYTELNY (znane ograniczenie)' }), $dk
if ($Exe -like '*\build\*') { $env:PATH = "C:\Qt\6.10.3\msvc2022_64\bin;$gstBin;$saved" }
$env:ISOREC_PROFILE = $profile
$r = Start-Process $Exe -ArgumentList @('--demo', 1, '--lang', 'en', '--recover', '--dump-log', "`"$out\log2.txt`"") -PassThru -RedirectStandardOutput "$out\rec.txt"
$env:PATH = $saved; $env:ISOREC_PROFILE = $null
Wait-Exit $r 60
$j = Sidecar $out
Check 'Recovery: nie obiecuje naprawy rosnacego MP4 (zglasza stracony)' ($j.recovered -and $j.recovered.repaired -eq 0 -and $j.recovered.lost -ge 1) ("(" + (($j.recovered | ConvertTo-Json -Compress)) + ")")
Remove-Item $out -Recurse -Force
Get-ChildItem "$env:LOCALAPPDATA" -Directory -Filter "*$profile*" -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue

'--- E. kontrola: zwykly MP4 (bez growing) w trakcie nagrywania jest nieczytelny'
$out = NewOut
if ($Exe -like '*\build\*') { $env:PATH = "C:\Qt\6.10.3\msvc2022_64\bin;$gstBin;$saved" }
$env:ISOREC_PROFILE = 'growtest'
$p = Start-Process $Exe -ArgumentList @('--demo', 1, '--lang', 'en', '--out', "`"$out`"", '--container', 'mp4', '--rec-delay', 5, '--rec-test', 12) -PassThru
$env:PATH = $saved; $env:ISOREC_PROFILE = $null
Start-Sleep 12; $f = Get-ChildItem $out -Filter *.mp4 | Select-Object -First 1; $d = if ($f) { Duration $f.FullName } else { -1 }
Wait-Exit $p 60
Check 'bez growing plik w trakcie nagrywania nie daje sie otworzyc' ($d -le 0) "(dlugosc $d)"
Remove-Item $out -Recurse -Force
"Wynik: bledow $errors"
exit $errors
