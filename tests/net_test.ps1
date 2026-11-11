# Test wejsc sieciowych: kanal odbiera strumien SRT albo RTSP z lokalnego nadajnika i nagrywa go.
# W trakcie nagrania nadajnik jest zatrzymywany i uruchamiany ponownie (zanik i powrot sygnalu).
# Sprawdzane: plik poprawnie zamkniety, jedna czesc, ciagle znaczniki obrazu (bez przerw mimo zaniku), czas zgodny,
# w pliku opisu dokladnie jeden zanik sygnalu, obraz przed zanikiem i po powrocie nie jest czarny, a w trakcie zaniku jest.
# Nadajnik: SRT - ffmpeg (listener na porcie), RTSP - build\rtsp_test_server.exe.
# Uzycie:  tests\net_test.ps1 [-Type srt|rtsp] [-Port 9950] [-NoBreak] [-Exe build\IsoRecorder.exe]
# -Loss pause|split : reakcja kanalu na zanik; sprawdzane jest wtedy, czy nagranie ma jedna pauze (pause) albo dwie czesci (split).
# Kod wyjscia 0 = wszystko poprawne.
param([string]$Type = 'srt', [int]$Port = 9950, [switch]$NoBreak, [string]$Loss = '', [string]$Exe)
$ErrorActionPreference = 'Stop'
$inv = [cultureinfo]::InvariantCulture
$root = Split-Path $PSScriptRoot
if (-not $Exe) { $Exe = "$root\build\IsoRecorder.exe" }
$gst = $env:GSTREAMER_1_0_ROOT_MSVC_X86_64
if (-not $gst) { $gst = "$env:LOCALAPPDATA\Programs\gstreamer\1.0\msvc_x86_64" }
$gstBin = "$($gst.TrimEnd('\'))\bin"
$qt = Get-ChildItem C:\Qt -Directory | ForEach-Object { Get-ChildItem $_.FullName -Directory -Filter 'msvc*_64' } |
    Sort-Object FullName | Select-Object -Last 1 -ExpandProperty FullName
$savedPath = $env:PATH

function Start-Sender {
    if ($Type -eq 'srt') {
        $a = "-hide_banner -loglevel error -re -f lavfi -i testsrc2=size=1280x720:rate=25 -f lavfi -i sine=frequency=440:sample_rate=48000 " +
             "-c:v libx264 -preset ultrafast -tune zerolatency -g 25 -b:v 3M -c:a aac -b:a 128k -f mpegts `"srt://127.0.0.1:$($Port)?mode=listener`""
        Start-Process ffmpeg -ArgumentList $a -PassThru -WindowStyle Hidden
    } else {
        $env:PATH = "$gstBin;$savedPath"
        $p = Start-Process "$root\build\rtsp_test_server.exe" -ArgumentList $Port -PassThru -WindowStyle Hidden
        $env:PATH = $savedPath
        $p
    }
}
$uri = if ($Type -eq 'srt') { "srt://127.0.0.1:$Port" } else { "rtsp://127.0.0.1:$Port/test" }
$out = Join-Path $env:TEMP "isorec_net_$(Get-Date -Format 'HHmmss')"
New-Item -ItemType Directory $out | Out-Null

# Przebieg (sekundy od startu programu): 0 nadajnik i program, 3 start nagrania, 13 nadajnik stop, 20 nadajnik start, 31 koniec nagrania.
$seconds = 28
$sender = Start-Sender
Start-Sleep 1
if ($Exe -like "*\build\*") { $env:PATH = "$qt\bin;$gstBin;$savedPath" }
$appArgs = @('--demo', 1, '--lang', 'en', '--out', $out, '--input', $Type, '--uri', $uri, '--rec-test', $seconds)
if ($Loss) { $appArgs += @('--loss', $Loss) }
$app = Start-Process $Exe -ArgumentList $appArgs -PassThru
$env:PATH = $savedPath
if (-not $NoBreak) {
    Start-Sleep 13
    if (-not $sender.HasExited) { $sender.Kill() }
    $stopAt = Get-Date
    Start-Sleep 7
    $sender = Start-Sender
    $backAt = Get-Date
}
if (-not $app.WaitForExit(($seconds + 40) * 1000)) { $app.Kill(); 'BLAD: program nie zakonczyl sie w oczekiwanym czasie' }
if (-not $sender.HasExited) { $sender.Kill() }

$fail = 0
function Check($name, $ok, $detail) { "{0}  {1}  {2}" -f $(if ($ok) { 'ok  ' } else { 'BLAD' }), $name, $detail; if (-not $ok) { $script:fail++ } }
$sc = Get-ChildItem $out -Filter *.json | Select-Object -First 1
if (-not $sc) { 'BLAD: brak nagrania'; Remove-Item $out -Recurse -Force; exit 1 }
$j = Get-Content $sc.FullName -Raw -Encoding utf8 | ConvertFrom-Json
if ($Loss) {
    # reakcja na zanik: pauza = jedna pauza w opisie i krotszy material; nowa czesc = dwie czesci
    $frames = 0; foreach ($part in $j.parts) { $frames += [int](ffprobe -v error -select_streams v:0 -count_packets -show_entries stream=nb_read_packets -of csv=p=0 (Join-Path $out $part.file)).Trim(',') }
    Check 'nagranie poprawnie zamkniete' ($j.clean) ''
    Check 'jeden zanik sygnalu w opisie' ($j.signalLosses -eq 1) "($($j.signalLosses))"
    if ($Loss -eq 'pause') {
        Check 'jedna czesc i jedna pauza w opisie' ($j.parts.Count -eq 1 -and @($j.pauses).Count -eq 1) ("(czesci {0}, pauz {1}, dlugosc pauzy {2:N1} s)" -f $j.parts.Count, @($j.pauses).Count, $(if (@($j.pauses).Count) { $j.pauses[0].durationMs / 1000 } else { 0 }))
        Check 'material krotszy o czas pauzy' ($frames -lt ($seconds - 2) * 25 -and $frames -gt ($seconds - 8) * 25) "($frames klatek)"
    } else {
        Check 'dwie czesci pliku' ($j.parts.Count -eq 2) "(czesci $($j.parts.Count))"
        Check 'material pelnej dlugosci' ($frames -ge ($seconds - 2) * 25) "($frames klatek)"
    }
    Remove-Item $out -Recurse -Force
    "Wynik: bledow $fail"
    exit $fail
}
$f = Join-Path $out $j.parts[0].file
$pts = ffprobe -v error -select_streams v:0 -show_entries packet=pts_time -of csv=p=0 $f | ForEach-Object { [double]::Parse($_.Trim(','), $inv) } | Sort-Object
$gap = 0.0; for ($i = 1; $i -lt $pts.Count; $i++) { $d = $pts[$i] - $pts[$i - 1]; if ($d -gt $gap) { $gap = $d } }
# srednia jasnosc klatki w danej sekundzie pliku (obraz testowy jest jasny, obraz zastepczy przy zaniku - czarny)
function Luma($t) {
    $line = ffmpeg -hide_banner -loglevel error -ss $t -i $f -frames:v 1 -vf "signalstats,metadata=print:key=lavfi.signalstats.YAVG:file=-" -f null - 2>$null |
        Select-String 'YAVG=' | Select-Object -First 1
    if ($line) { [double]::Parse(($line.Line -replace '.*YAVG=', ''), $inv) } else { -1 }
}
Check 'nagranie poprawnie zamkniete, jedna czesc' ($j.clean -and $j.parts.Count -eq 1) ''
# RTSP: zrodlo ma 2 s bufora i przy ponownym polaczeniu zaczyna nowa os czasu - dopuszczalna jedna luka do 2,5 s
$maxGap = if ($Type -eq 'rtsp' -and -not $NoBreak) { 2.5 } else { 0.061 }
$span = $pts[$pts.Count - 1] - $pts[0]
Check 'czas nagrania zgodny' ($span -ge $seconds - 1.5 -and $span -le $seconds + 1) ("({0:N1} s, {1} klatek)" -f $span, $pts.Count)
Check 'znaczniki obrazu bez przerw ponad dopuszczalna' ($gap -lt $maxGap) ("(najwieksza przerwa {0:N3} s, dopuszczalna {1})" -f $gap, $maxGap)
$t0 = [datetime]::Parse($j.startedUtc, $inv, [Globalization.DateTimeStyles]::AdjustToUniversal -bor [Globalization.DateTimeStyles]::AssumeUniversal)
$before = Luma 2
Check 'obraz ze strumienia przed zanikiem' ($before -gt 40) ("(jasnosc {0:N0})" -f $before)
if ($NoBreak) {
    Check 'brak zanikow sygnalu w opisie' ($j.signalLosses -eq 0) "($($j.signalLosses))"
} else {
    # nadajnik staje ok. 10. sekundy pliku i wraca ok. 17.; przez pierwsze ok. 3 s zaniku stoi ostatnia klatka, potem jest czarny obraz
    $stopT = ($stopAt.ToUniversalTime() - $t0).TotalSeconds; $backT = ($backAt.ToUniversalTime() - $t0).TotalSeconds
    $during = Luma ([math]::Round($backT - 1.5, 1)); $after = Luma ([math]::Round($backT + 5, 1))
    "      nadajnik zatrzymany w {0:N1} s pliku, wznowiony w {1:N1} s" -f $stopT, $backT
    Check 'jeden zanik sygnalu w opisie, 2-7 s' ($j.signalLosses -eq 1 -and $j.signalLostMs -ge 2000 -and $j.signalLostMs -le 7500) ("({0}, lacznie {1:N1} s)" -f $j.signalLosses, ($j.signalLostMs / 1000))
    Check 'obraz zastepczy (czarny) w trakcie zaniku' ($during -ge 0 -and $during -lt 30) ("(jasnosc {0:N0})" -f $during)
    Check 'obraz ze strumienia po powrocie' ($after -gt 40) ("(jasnosc {0:N0})" -f $after)
}
Remove-Item $out -Recurse -Force
"Wynik: bledow $fail"
exit $fail
