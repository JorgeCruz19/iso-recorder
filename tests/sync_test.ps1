# Test wspolnego momentu startu, pauzy i stopu: N kanalow testowych nagrywa S sekund jako grupa.
# Z plikow opisu (.json) czytane sa czasy na wspolnym zegarze kanalow:
#   - wspolna chwila startu musi byc identyczna we wszystkich kanalach,
#   - pierwsza zapisana klatka kazdego kanalu musi lezec nie dalej niz 1 klatke po starcie,
#   - ostatnie zapisane klatki kanalow nie moga sie roznic o wiecej niz 1 klatke,
#   - liczba klatek w plikach (ffprobe) nie moze sie roznic o wiecej niz 1 (z pauza: 2),
#   - przy pauzie: polozenie i dlugosc pauzy w kanalach rozni sie najwyzej o 1 klatke.
# Uzycie:  tests\sync_test.ps1 [-Channels 16] [-Seconds 10] [-PauseAt 0] [-PauseFor 0] [-Exe build\IsoRecorder.exe]
# Kod wyjscia 0 = wszystko poprawne.
param([int]$Channels = 16, [int]$Seconds = 10, [int]$PauseAt = 0, [int]$PauseFor = 0, [string]$Exe)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
if (-not $Exe) { $Exe = "$root\build\IsoRecorder.exe" }
if ($Exe -like "*\build\*") {
    $qt = Get-ChildItem C:\Qt -Directory | ForEach-Object { Get-ChildItem $_.FullName -Directory -Filter 'msvc*_64' } |
        Sort-Object FullName | Select-Object -Last 1 -ExpandProperty FullName
    $gst = $env:GSTREAMER_1_0_ROOT_MSVC_X86_64
    if (-not $gst) { $gst = "$env:LOCALAPPDATA\Programs\gstreamer\1.0\msvc_x86_64" }
    $env:PATH = "$qt\bin;$($gst.TrimEnd('\'))\bin;$env:PATH"
}
$out = Join-Path $env:TEMP "isorec_sync_$(Get-Date -Format 'HHmmss')"
New-Item -ItemType Directory $out | Out-Null
$a = @('--demo', $Channels, '--lang', 'en', '--out', $out, '--rec-test', $Seconds)
if ($PauseAt -gt 0 -and $PauseFor -gt 0) { $a += @('--pause-at', $PauseAt, '--pause-for', $PauseFor) }
$p = Start-Process $Exe -ArgumentList $a -PassThru
if (-not $p.WaitForExit(($Seconds + 60) * 1000)) { $p.Kill(); throw 'Program nie zakonczyl sie w oczekiwanym czasie.' }

$frame = 40.0      # ms, 25 kl./s
$rows = foreach ($sc in Get-ChildItem $out -Filter *.json) {
    $j = Get-Content $sc.FullName -Raw -Encoding utf8 | ConvertFrom-Json
    $f = Join-Path $out $j.parts[0].file
    $n = [int](ffprobe -v error -select_streams v:0 -count_packets -show_entries stream=nb_read_packets -of csv=p=0 $f).Trim(',')
    [pscustomobject]@{ Channel = $j.channel; Clean = $j.clean; Start = $j.startClockNs
        FirstMs = ($j.firstFrameClockNs - $j.startClockNs) / 1e6; LastMs = ($j.lastFrameClockNs - $j.startClockNs) / 1e6; Frames = $n
        PauseAtMs = $(if (@($j.pauses).Count) { $j.pauses[0].atMs } else { $null }); PauseMs = $(if (@($j.pauses).Count) { $j.pauses[0].durationMs } else { $null }) }
}
function Spread($values) { $m = $values | Measure-Object -Minimum -Maximum; [math]::Round($m.Maximum - $m.Minimum, 1) }
$fail = 0
function Check($name, $ok, $detail) { "{0}  {1}  {2}" -f $(if ($ok) { 'ok  ' } else { 'BLAD' }), $name, $detail; if (-not $ok) { $script:fail++ } }

Check 'nagran tyle, ile kanalow, wszystkie poprawnie zamkniete' (@($rows).Count -eq $Channels -and @($rows | Where-Object { -not $_.Clean }).Count -eq 0) "($(@($rows).Count) z $Channels)"
Check 'wspolna chwila startu identyczna we wszystkich kanalach' (@($rows.Start | Select-Object -Unique).Count -eq 1) ''
$fm = $rows.FirstMs | Measure-Object -Minimum -Maximum
Check 'pierwsza klatka w ciagu 1 klatki od startu' ($fm.Minimum -ge 0 -and $fm.Maximum -le $frame + 1) ("({0:N1}..{1:N1} ms po starcie)" -f $fm.Minimum, $fm.Maximum)
Check 'ostatnie klatki kanalow w odleglosci do 1 klatki' ((Spread $rows.LastMs) -le $frame + 1) "(rozrzut $(Spread $rows.LastMs) ms)"
# kazda granica (start, pauza, wznowienie, stop) moze wypasc o klatke inaczej w roznych kanalach: bez pauzy 1, z pauza 2
$frameTol = if ($PauseAt -gt 0 -and $PauseFor -gt 0) { 2 } else { 1 }
Check "liczba klatek w plikach rozni sie najwyzej o $frameTol" ((Spread $rows.Frames) -le $frameTol) "($(($rows.Frames | Measure-Object -Minimum).Minimum)..$(($rows.Frames | Measure-Object -Maximum).Maximum) klatek)"
if ($PauseAt -gt 0 -and $PauseFor -gt 0) {
    $withPause = @($rows | Where-Object { $null -ne $_.PauseAtMs })
    Check 'pauza zapisana w kazdym kanale' ($withPause.Count -eq $Channels) "($($withPause.Count) z $Channels)"
    if ($withPause.Count) {
        Check 'polozenie pauzy w kanalach w odleglosci do 1 klatki' ((Spread $withPause.PauseAtMs) -le $frame + 1) "(rozrzut $(Spread $withPause.PauseAtMs) ms)"
        Check 'dlugosc pauzy w kanalach w odleglosci do 1 klatki' ((Spread $withPause.PauseMs) -le $frame + 1) "(rozrzut $(Spread $withPause.PauseMs) ms)"
    }
}
Remove-Item $out -Recurse -Force
"Wynik: bledow $fail"
exit $fail
