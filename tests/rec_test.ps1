# Test nagrywania na generatorach testowych: N kanalow nagrywa S sekund (C cykli), potem ffprobe sprawdza kazdy plik.
# Uzycie:  tests\rec_test.ps1 [-Channels 4] [-Seconds 10] [-Cycles 1] [-SplitEvery 0] [-AutoSplit 0] [-Container mkv|ts]
#                             [-PauseAt 0] [-PauseFor 0] [-PauseCount 1] [-Exe build\IsoRecorder.exe]
# -SplitEvery S : reczny split wszystkich kanalow co S sekund;  -AutoSplit S : auto-split co S sekund.
# -PauseAt S -PauseFor P [-PauseCount N] : N pauz po P sekund, kazda po S sekundach nagrywania (Seconds liczy tez pauzy).
#   Sprawdzane: czas materialu = Seconds - N*P, brak przerw i cofniec w znacznikach obrazu i dzwieku,
#   koniec obrazu i dzwieku w tym samym miejscu, liczba pauz w pliku opisu.
# -LiveCycle S [-LiveType srt|ndi] : w trakcie nagrania wyjscie na zywo jest co S sekund wlaczane i wylaczane -
#   nagranie musi pozostac ciagle (te same kontrole co zwykle).
# -Source screen : kanaly biora obraz z ekranu zamiast z generatora;  -Audio loopback|device|none : zrodlo dzwieku z WASAPI.
# Kod wyjscia 0 = wszystko poprawne.
param([int]$Channels = 4, [int]$Seconds = 10, [int]$Cycles = 1, [int]$SplitEvery = 0, [int]$AutoSplit = 0,
      [string]$Container = 'mkv', [int]$PauseAt = 0, [int]$PauseFor = 0, [int]$PauseCount = 1, [int]$LiveCycle = 0, [string]$LiveType = 'srt', [string]$Source = '', [string]$Audio = '', [string]$Exe)
$ErrorActionPreference = 'Stop'
$inv = [cultureinfo]::InvariantCulture
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

$a = @('--demo', $Channels, '--lang', 'en', '--out', $out, '--rec-test', $Seconds, '--cycles', $Cycles, '--container', $Container)
if ($SplitEvery -gt 0) { $a += @('--split-every', $SplitEvery) }
if ($AutoSplit -gt 0) { $a += @('--auto-split', $AutoSplit) }
if ($LiveCycle -gt 0) { $a += @('--live', $LiveType, 9750, '--live-cycle', $LiveCycle) }
if ($Source) { $a += @('--input', $Source) }
if ($Audio) { $a += @('--audio', $Audio) }
$pauses = 0
if ($PauseAt -gt 0 -and $PauseFor -gt 0) { $pauses = $PauseCount; $a += @('--pause-at', $PauseAt, '--pause-for', $PauseFor, '--pause-count', $PauseCount) }
$content = $Seconds - $pauses * $PauseFor      # ile sekund materialu ma byc w pliku
$p = Start-Process $Exe -ArgumentList $a -PassThru
if (-not $p.WaitForExit(($Seconds + 6) * $Cycles * 1000 + 60000)) { $p.Kill(); throw 'Program nie zakonczyl sie w oczekiwanym czasie.' }

$fail = 0
$split = ($SplitEvery -gt 0) -or ($AutoSplit -gt 0)
$sidecars = Get-ChildItem $out -Filter *.json
$expected = $Channels * $Cycles
if ($sidecars.Count -ne $expected) { "BLAD: nagran (plikow .json) $($sidecars.Count), oczekiwano $expected"; $fail++ }
foreach ($sc in $sidecars) {
    $info = Get-Content $sc.FullName -Raw -Encoding utf8 | ConvertFrom-Json
    $frames = 0; $total = 0.0; $bad = @(); $avMax = 0.0
    if (-not $info.clean) { $bad += 'nagranie nie zakonczone poprawnie' }
    if ($split -and $info.parts.Count -lt 2) { $bad += 'brak podzialu na czesci' }
    if (-not $split -and $info.parts.Count -ne 1) { $bad += "czesci: $($info.parts.Count)" }
    foreach ($part in $info.parts) {
        $f = Join-Path $out $part.file
        if (-not (Test-Path $f)) { $bad += "brak pliku $($part.file)"; continue }
        $dur = [double]::Parse((ffprobe -v error -show_entries format=duration -of csv=p=0 $f), $inv)
        # dla MPEG-TS ffprobe wypisuje strumienie dwa razy (program + strumienie) - stad Select -Unique
        $codecs = (ffprobe -v error -show_entries stream=codec_name -of csv=p=0 $f | ForEach-Object { $_.Trim(',') } |
            Where-Object { $_ } | Select-Object -Unique) -join '+'
        $first = (ffprobe -v error -select_streams v:0 -show_entries frame=key_frame -read_intervals '%+#1' -of csv=p=0 $f | Select-Object -First 1).Trim(',')
        # ciaglosc PTS obrazu wewnatrz czesci: najwieksza przerwa miedzy kolejnymi klatkami
        $pts = ffprobe -v error -select_streams v:0 -show_entries packet=pts_time -of csv=p=0 $f |
            ForEach-Object { [double]::Parse($_.Trim(','), $inv) } | Sort-Object
        $gap = 0.0; for ($i = 1; $i -lt $pts.Count; $i++) { $d = $pts[$i] - $pts[$i - 1]; if ($d -gt $gap) { $gap = $d } }
        $frames += $pts.Count; $total += $dur
        # dzwiek: znaczniki w kolejnosci zapisu nie moga sie cofac; przerwa najwyzej ok. 3 klatek (ciecie w miejscu pauzy)
        $apts = ffprobe -v error -select_streams a:0 -show_entries packet=pts_time -of csv=p=0 $f |
            ForEach-Object { [double]::Parse($_.Trim(','), $inv) }
        $agap = 0.0; $aback = 0
        for ($i = 1; $i -lt $apts.Count; $i++) { $d = $apts[$i] - $apts[$i - 1]; if ($d -gt $agap) { $agap = $d }; if ($d -lt 0) { $aback++ } }
        if ($aback) { $bad += "$($part.file): znaczniki dzwieku cofaja sie ($aback)" }
        if ($agap -ge 0.15) { $bad += "$($part.file): przerwa w dzwieku $agap s" }
        if ($pts.Count -and $apts.Count) {
            $avEnd = [math]::Abs($pts[$pts.Count - 1] - $apts[$apts.Count - 1])
            if ($avEnd -ge 0.1) { $bad += "$($part.file): koniec obrazu i dzwieku rozni sie o $avEnd s" }
            if ($avEnd -gt $avMax) { $avMax = $avEnd }
        }
        if ($codecs -ne 'h264+aac') { $bad += "$($part.file): kodeki $codecs" }
        if ($first -ne '1') { $bad += "$($part.file): pierwsza klatka nie jest kluczowa" }
        if ($gap -ge 0.061) { $bad += "$($part.file): przerwa PTS $gap s" }
    }
    # suma klatek wszystkich czesci = czas nagrania (tolerancja obejmuje czas przygotowania grupy do startu)
    $minFrames = ($content - 0.5) * 25; $maxFrames = ($content + 2.5) * 25
    if (@($info.pauses).Count -ne $pauses) { $bad += "pauz w opisie: $(@($info.pauses).Count), oczekiwano $pauses" }
    if ($frames -lt $minFrames -or $frames -gt $maxFrames) { $bad += "klatek $frames, oczekiwano $minFrames..$maxFrames" }
    if ($bad.Count) { $fail++ }
    '{0}  {1}  czesci={2} klatek={3} czas={4:N2}s pauz={5} A/V koniec={6:N3}s  {7}' -f $(if ($bad.Count) { 'BLAD' } else { 'ok  ' }), $info.channel, $info.parts.Count, $frames, $total, @($info.pauses).Count, $avMax, ($bad -join '; ')
}
Remove-Item $out -Recurse -Force
"Wynik: $($sidecars.Count - $fail) / $expected poprawnych"
exit $fail
