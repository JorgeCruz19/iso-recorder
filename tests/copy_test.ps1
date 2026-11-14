# Test zapisu bez ponownego kodowania (stream copy) dla wejsc SRT i RTSP.
# Nadajnik: SRT - ffmpeg (H.264 3 Mb/s, GOP 1 s, AAC), RTSP - build\rtsp_test_server.exe.
# Sprawdzane: plik zamkniety poprawnie, kodek i rozmiar obrazu jak w strumieniu (bez ponownego kodowania: w opisie
# enkoder "copy", bitrate pliku jak u nadawcy, a nie 8 Mb/s z ustawien), plik zaczyna sie klatka kluczowa, dlugosc zgodna,
# znaczniki ciagle, dzwiek obecny; -Split: reczny podzial daje czesci zaczynajace sie klatka kluczowa;
# -Pause: pauza skraca material i zostawia jeden plik; -Break: zanik nadajnika zamyka plik, po powrocie powstaje nowy.
# Uzycie:  tests\copy_test.ps1 [-Type srt|rtsp] [-Container mkv|ts] [-Split] [-Pause] [-Break] [-Exe sciezka]
param([string]$Type = 'srt', [string]$Container = 'mkv', [switch]$Split, [switch]$Pause, [switch]$Break, [int]$Port = 9970, [string]$Exe = '')
$ErrorActionPreference = 'Continue'
$inv = [cultureinfo]::InvariantCulture
$root = Split-Path $PSScriptRoot
if (-not $Exe) { $Exe = "$root\build\IsoRecorder.exe" }
$gstBin = "$env:LOCALAPPDATA\Programs\gstreamer\1.0\msvc_x86_64\bin"
$saved = $env:PATH
$errors = 0
function Check([string]$what, [bool]$ok, [string]$got = '') {
    if ($ok) { "ok    $what  $got" } else { "BLAD  $what  $got"; $script:errors++ }
}
function Start-Sender {
    if ($Type -eq 'srt') {
        $a = "-hide_banner -loglevel error -re -f lavfi -i testsrc2=size=1280x720:rate=25 -f lavfi -i sine=frequency=440:sample_rate=48000 " +
             "-c:v libx264 -preset ultrafast -tune zerolatency -g 25 -b:v 3M -c:a aac -b:a 128k -f mpegts `"srt://127.0.0.1:$($Port)?mode=listener`""
        Start-Process ffmpeg -ArgumentList $a -PassThru -WindowStyle Hidden
    } else {
        $env:PATH = "$gstBin;$saved"
        $p = Start-Process "$root\build\rtsp_test_server.exe" -ArgumentList $Port -PassThru -WindowStyle Hidden
        $env:PATH = $saved
        $p
    }
}
$uri = if ($Type -eq 'srt') { "srt://127.0.0.1:$Port" } else { "rtsp://127.0.0.1:$Port/test" }
$out = Join-Path $env:TEMP "isorec_copy_$(Get-Date -Format 'HHmmss')"
New-Item -ItemType Directory $out | Out-Null
$seconds = if ($Break) { 30 } else { 16 }
$sender = Start-Sender
Start-Sleep 2
$appArgs = @('--demo', 1, '--lang', 'en', '--out', "`"$out`"", '--input', $Type, '--uri', $uri, '--encoder', 'copy', '--container', $Container,
             '--rec-delay', 7, '--rec-test', $seconds, '--dump-log', "`"$out\log.txt`"")
if ($Split) { $appArgs += @('--split-every', 5) }
if ($Pause) { $appArgs += @('--pause-at', 5, '--pause-for', 4) }
if ($Exe -like '*\build\*') { $env:PATH = "C:\Qt\6.10.3\msvc2022_64\bin;$gstBin;$saved" }
$env:ISOREC_PROFILE = 'copytest'
$app = Start-Process $Exe -ArgumentList $appArgs -PassThru
$env:PATH = $saved; $env:ISOREC_PROFILE = $null
if ($Break) {       # nadajnik znika w 7. sekundzie nagrania na 8 s
    Start-Sleep 14
    if (-not $sender.HasExited) { $sender.Kill() }
    Start-Sleep 8
    $sender = Start-Sender
}
$sw = [Diagnostics.Stopwatch]::StartNew()
while (-not $app.HasExited -and $sw.Elapsed.TotalSeconds -lt ($seconds + 60)) { Start-Sleep -Milliseconds 300 }
if (-not $app.HasExited) { $app.Kill(); Check 'program zakonczyl sie sam' $false }
if (-not $sender.HasExited) { $sender.Kill() }

function Probe([string]$file) {
    $v = @(cmd /c "ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,width,height -of csv=p=0 `"$file`" 2>&1")[0]
    $aLines = @(cmd /c "ffprobe -v error -select_streams a:0 -show_entries stream=codec_name -of csv=p=0 `"$file`" 2>&1")
    $a = if ($aLines.Count) { [string]$aLines[0] } else { '' }
    $pk = @(cmd /c "ffprobe -v error -select_streams v:0 -show_entries packet=pts_time,flags -of csv=p=0 `"$file`" 2>&1")
    $pts = @($pk | ForEach-Object { $x = ($_ -split ',')[0]; if ($x -match '^[\d.]+$') { [double]::Parse($x, $inv) } }) | Sort-Object
    $gap = 0.0; for ($i = 1; $i -lt $pts.Count; $i++) { $d = $pts[$i] - $pts[$i - 1]; if ($d -gt $gap) { $gap = $d } }
    # passthrough: bez przeliczania na staly klatkaz (znaczniki ze strumienia maja kilka ms nierownomiernosci)
    $decLines = @(cmd /c "ffmpeg -v error -i `"$file`" -fps_mode passthrough -f null - 2>&1")
    # ostrzezenie o kolejnosci znacznikow pochodzi z przeliczenia na staly klatkaz w ffmpeg (znaczniki RTSP nadaje odbiornik,
    # z dokladnoscia do kilku ms) - to nie jest blad dekodowania
    $decLines = @($decLines | Where-Object { $_ -notmatch 'non monotonically' })
    $dec = $decLines.Count
    $decLines | Select-Object -First 3 | ForEach-Object { "      ffmpeg: $_" } | Write-Host
    [pscustomobject]@{ Video = $v.Trim(','); Audio = $a.Trim(','); Frames = $pts.Count; FirstKey = ($pk.Count -gt 0 -and $pk[0] -match 'K'); Gap = $gap
                       Span = $(if ($pts.Count) { $pts[$pts.Count - 1] - $pts[0] } else { 0 }); DecodeErrors = $dec; Size = (Get-Item $file).Length }
}
$sc = @(Get-ChildItem $out -Filter *.json | Sort-Object Name | ForEach-Object { Get-Content $_.FullName -Raw -Encoding utf8 | ConvertFrom-Json })
$log = Get-Content "$out\log.txt" -Encoding utf8 -ErrorAction SilentlyContinue
if ($sc.Count -eq 0) { Check 'nagranie powstalo' $false; $log | Select-Object -Last 8; Remove-Item $out -Recurse -Force; "Wynik: bledow $errors"; exit $errors }
if ($Break) {
    Check 'zanik: dwa nagrania (przed zanikiem i po powrocie)' ($sc.Count -eq 2) "($($sc.Count))"
    $n = 0
    foreach ($j in $sc) {
        $n++
        $p = Probe (Join-Path $out $j.parts[0].file)
        Check "nagranie $($n): zaczyna sie klatka kluczowa i dekoduje sie bez bledow" ($p.FirstKey -and $p.DecodeErrors -eq 0 -and $p.Frames -gt 50) "($($p.Frames) klatek, bledow dekodowania $($p.DecodeErrors))"
    }
    # zanik zamyka pierwszy plik (poprawnie, przez EOS); drugi konczy zwykly Stop
    Check 'oba nagrania zamkniete poprawnie' ($sc[0].clean -and $sc[-1].clean) "($($sc[0].stopReason))"
} else {
    $j = $sc[0]
    Check 'nagranie zamkniete poprawnie' ($j.clean -eq $true)
    Check 'w opisie enkoder "copy"' ($j.video.encoder -eq 'copy') "($($j.video.encoder))"
    $frames = 0; $bytes = 0; $allKey = $true; $dec = 0; $gap = 0.0; $first = $null
    foreach ($part in $j.parts) {
        $p = Probe (Join-Path $out $part.file)
        if (-not $first) { $first = $p }
        $frames += $p.Frames; $bytes += $p.Size; $allKey = $allKey -and $p.FirstKey; $dec += $p.DecodeErrors; if ($p.Gap -gt $gap) { $gap = $p.Gap }
    }
    Check 'obraz H.264 1280x720 jak w strumieniu' ($first.Video -eq 'h264,1280,720') "($($first.Video))"
    Check 'dzwiek AAC obecny' ($first.Audio -eq 'aac') "($($first.Audio))"
    Check 'kazda czesc zaczyna sie klatka kluczowa' $allKey
    Check 'plik dekoduje sie bez bledow' ($dec -eq 0) "($dec)"
    Check 'znaczniki obrazu ciagle w kazdej czesci' ($gap -lt 0.061) ("(najwieksza przerwa {0:N3} s)" -f $gap)
    $expect = if ($Pause) { ($seconds - 4) * 25 } else { $seconds * 25 }
    # start, wznowienie i podzial czekaja na klatke kluczowa nadajnika (co 1 s) - stad tolerancja
    Check "dlugosc materialu ok. $expect klatek" ($frames -ge $expect - 60 -and $frames -le $expect + 5) "($frames)"
    $mbps = $bytes * 8 / ($frames / 25.0) / 1e6
    Check 'bitrate pliku jak u nadawcy (2-4,5 Mb/s), nie 8 Mb/s z ustawien kanalu' ($mbps -gt 2 -and $mbps -lt 4.5) ("({0:N1} Mb/s)" -f $mbps)
    if ($Split) { Check 'reczny podzial: 3-5 czesci' ($j.parts.Count -ge 3 -and $j.parts.Count -le 5) "($($j.parts.Count))" }
    else { Check 'jedna czesc' ($j.parts.Count -eq 1) "($($j.parts.Count))" }
    if ($Pause) { Check 'jedna pauza w opisie, 4-6 s' (@($j.pauses).Count -eq 1 -and $j.pauses[0].durationMs -ge 3900 -and $j.pauses[0].durationMs -le 6100) "($([math]::Round($j.pauses[0].durationMs / 1000, 1)) s)" }
}
Check 'log: nagranie w trybie kopii strumienia' (@($log | Select-String 'stream copy').Count -ge 1)
Remove-Item $out -Recurse -Force
"Wynik: bledow $errors"
exit $errors
