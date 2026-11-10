# Test nagrywania na generatorach testowych: N kanalow nagrywa S sekund (C cykli), potem ffprobe sprawdza kazdy plik.
# Uzycie:  tests\rec_test.ps1 [-Channels 4] [-Seconds 10] [-Cycles 1] [-Exe build\IsoRecorder.exe]
# Kod wyjscia 0 = wszystkie pliki poprawne.
param([int]$Channels = 4, [int]$Seconds = 10, [int]$Cycles = 1, [string]$Exe)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
if (-not $Exe) { $Exe = "$root\build\IsoRecorder.exe" }
if ($Exe -like "*\build\*") {     # build deweloperski potrzebuje Qt i GStreamera w PATH; paczka dist - nie
    $qt = Get-ChildItem C:\Qt -Directory | ForEach-Object { Get-ChildItem $_.FullName -Directory -Filter 'msvc*_64' } |
        Sort-Object FullName | Select-Object -Last 1 -ExpandProperty FullName
    $gst = $env:GSTREAMER_1_0_ROOT_MSVC_X86_64
    if (-not $gst) { $gst = "$env:LOCALAPPDATA\Programs\gstreamer\1.0\msvc_x86_64" }
    $env:PATH = "$qt\bin;$($gst.TrimEnd('\'))\bin;$env:PATH"
}
$out = Join-Path $env:TEMP "isorec_test_$(Get-Date -Format 'HHmmss')"
New-Item -ItemType Directory $out | Out-Null

$p = Start-Process $Exe -ArgumentList '--demo', $Channels, '--lang', 'en', '--out', $out, '--rec-test', $Seconds, '--cycles', $Cycles -PassThru
if (-not $p.WaitForExit(($Seconds + 6) * $Cycles * 1000 + 60000)) { $p.Kill(); throw 'Program nie zakonczyl sie w oczekiwanym czasie.' }

$files = Get-ChildItem $out -Filter *.mkv
$expected = $Channels * $Cycles
$fail = 0
if ($files.Count -ne $expected) { "BLAD: plikow $($files.Count), oczekiwano $expected"; $fail++ }
foreach ($f in $files) {
    $inv = [cultureinfo]::InvariantCulture
    $dur = [double]::Parse((ffprobe -v error -show_entries format=duration -of csv=p=0 $f.FullName), $inv)
    $start = [double]::Parse((ffprobe -v error -show_entries format=start_time -of csv=p=0 $f.FullName), $inv)
    $codecs = (ffprobe -v error -show_entries stream=codec_name -of csv=p=0 $f.FullName) -join '+'
    $first = ffprobe -v error -select_streams v:0 -show_entries frame=key_frame -read_intervals '%+#1' -of csv=p=0 $f.FullName
    # ciaglosc PTS obrazu: najwieksza przerwa miedzy kolejnymi klatkami
    $pts = ffprobe -v error -select_streams v:0 -show_entries packet=pts_time -of csv=p=0 $f.FullName |
        ForEach-Object { [double]::Parse($_.Trim(','), $inv) } | Sort-Object
    $gap = 0.0; for ($i = 1; $i -lt $pts.Count; $i++) { $d = $pts[$i] - $pts[$i - 1]; if ($d -gt $gap) { $gap = $d } }
    $ok = ($dur -ge $Seconds - 0.5) -and ($dur -le $Seconds + 2.5) -and ($start -lt 0.2) -and ($codecs -eq 'h264+aac') -and
          ($first -match '^1') -and ($gap -lt 0.061)
    if (-not $ok) { $fail++ }
    '{0}  {1}  dur={2:N2}s start={3:N3}s {4} keyframe={5} maxgap={6:N3}s' -f $(if ($ok) { 'ok  ' } else { 'BLAD' }), $f.Name, $dur, $start, $codecs, $first.Trim(','), $gap
}
Remove-Item $out -Recurse -Force
"Wynik: $($files.Count - $fail) / $expected poprawnych"
exit $fail
