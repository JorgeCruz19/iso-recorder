# Test kodekow i kontenerow nagrania: H.264, H.265, AV1, ProRes 422 w MKV / MPEG-TS / MP4 / MOV.
# Dla kazdej dozwolonej pary: nagranie z generatora testowego, sprawdzenie kodeka w pliku, liczby klatek, dzwieku,
# poprawnego zamkniecia i dekodowania. Dodatkowo: split i pauza w MP4 / MOV, para niedozwolona (AV1 + MPEG-TS) ma zostac
# zamieniona na MKV, ProRes 1080p ma nadazac w czasie rzeczywistym.
# Uzycie:  tests\codec_test.ps1 [-Exe sciezka] [-Quick]   (-Quick: tylko po jednym kontenerze na kodek)
param([string]$Exe = '', [switch]$Quick)
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot
if (-not $Exe) { $Exe = "$root\build\IsoRecorder.exe" }
$gstBin = "$env:LOCALAPPDATA\Programs\gstreamer\1.0\msvc_x86_64\bin"
$saved = $env:PATH
$errors = 0
function Check([string]$what, [bool]$ok, [string]$got = '') {
    if ($ok) { "ok    $what  $got" } else { "BLAD  $what  $got"; $script:errors++ }
}
function Record([string[]]$extra, [int]$seconds = 6) {
    $out = Join-Path $env:TEMP "isorec_codec_$(Get-Random)"
    New-Item -ItemType Directory $out | Out-Null
    if ($Exe -like '*\build\*') { $env:PATH = "C:\Qt\6.10.3\msvc2022_64\bin;$gstBin;$saved" }
    $env:ISOREC_PROFILE = 'codectest'
    $a = @('--demo', 1, '--lang', 'en', '--out', "`"$out`"", '--rec-test', $seconds, '--dump-log', "`"$out\log.txt`"") + $extra
    $p = Start-Process $Exe -ArgumentList $a -PassThru
    $env:PATH = $saved; $env:ISOREC_PROFILE = $null
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while (-not $p.HasExited -and $sw.Elapsed.TotalSeconds -lt ($seconds + 50)) { Start-Sleep -Milliseconds 300 }
    if (-not $p.HasExited) { $p.Kill(); Check 'program zakonczyl sie sam' $false }
    $out
}
function Probe([string]$file) {
    $vLines = @(cmd /c "ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=codec_name,nb_read_frames -of csv=p=0 `"$file`" 2>&1")
    $aLines = @(cmd /c "ffprobe -v error -select_streams a:0 -show_entries stream=codec_name -of csv=p=0 `"$file`" 2>&1")
    $v = if ($vLines.Count) { [string]$vLines[0] } else { '' }
    $au = if ($aLines.Count) { [string]$aLines[0] } else { '' }
    $dec = @(cmd /c "ffmpeg -v error -i `"$file`" -fps_mode passthrough -f null - 2>&1" | Where-Object { $_ -notmatch 'non monotonically' }).Count
    [pscustomobject]@{ Codec = ($v -split ',')[0]; Frames = [int]("0" + ($v -split ',')[1]); Audio = $au.Trim(','); DecodeErrors = $dec }
}
$ffName = @{ h264 = 'h264'; h265 = 'hevc'; av1 = 'av1'; prores = 'prores' }
$pairs = @(
    @('h264', 'mkv'), @('h264', 'ts'), @('h264', 'mp4'), @('h264', 'mov'),
    @('h265', 'mkv'), @('h265', 'ts'), @('h265', 'mp4'), @('h265', 'mov'),
    @('av1', 'mkv'), @('av1', 'mp4'), @('av1', 'mov'),
    @('prores', 'mkv'), @('prores', 'mov'))
if ($Quick) { $pairs = @(@('h264', 'mp4'), @('h265', 'mov'), @('av1', 'mp4'), @('prores', 'mov')) }
'--- kodek x kontener (6 s, 720p25)'
foreach ($pc in $pairs) {
    $codec, $cont = $pc
    $out = Record @('--codec', $codec, '--container', $cont)
    $j = Get-ChildItem $out -Filter *.json | Select-Object -First 1 | ForEach-Object { Get-Content $_.FullName -Raw -Encoding utf8 | ConvertFrom-Json }
    $f = Get-ChildItem $out -Filter "*.$cont" | Select-Object -First 1
    if (-not $j -or -not $f) { Check "$codec w $cont" $false '(brak pliku)'; Get-Content "$out\log.txt" -ErrorAction SilentlyContinue | Select-Object -Last 3; Remove-Item $out -Recurse -Force; continue }
    $p = Probe $f.FullName
    $ok = $j.clean -and $p.Codec -eq $ffName[$codec] -and $p.Frames -ge 146 -and $p.Frames -le 152 -and $p.Audio -eq 'aac' -and $p.DecodeErrors -eq 0 -and
          $j.video.codec -eq $codec -and $j.container -eq $cont
    Check ("{0,-7} w {1,-4}" -f $codec, $cont) $ok ("({0}, {1} klatek, dzwiek {2}, bledow dekodowania {3}, enkoder {4})" -f $p.Codec, $p.Frames, $p.Audio, $p.DecodeErrors, $j.video.encoder)
    Remove-Item $out -Recurse -Force
}

'--- split co 3 s i pauza w MP4 (H.265) oraz MOV (ProRes)'
foreach ($pc in @(@('h265', 'mp4'), @('prores', 'mov'))) {
    $codec, $cont = $pc
    $out = Record @('--codec', $codec, '--container', $cont, '--split-every', 3) 10
    $j = Get-ChildItem $out -Filter *.json | Select-Object -First 1 | ForEach-Object { Get-Content $_.FullName -Raw -Encoding utf8 | ConvertFrom-Json }
    $frames = 0; $dec = 0
    foreach ($part in $j.parts) { $p = Probe (Join-Path $out $part.file); $frames += $p.Frames; $dec += $p.DecodeErrors }
    Check "$codec w $($cont): split daje 3-4 czesci, razem 10 s" ($j.clean -and $j.parts.Count -ge 3 -and $j.parts.Count -le 4 -and $frames -ge 246 -and $frames -le 252 -and $dec -eq 0) "($($j.parts.Count) czesci, $frames klatek)"
    Remove-Item $out -Recurse -Force
    $out = Record @('--codec', $codec, '--container', $cont, '--pause-at', 3, '--pause-for', 3) 10
    $j = Get-ChildItem $out -Filter *.json | Select-Object -First 1 | ForEach-Object { Get-Content $_.FullName -Raw -Encoding utf8 | ConvertFrom-Json }
    $p = Probe (Join-Path $out $j.parts[0].file)
    Check "$codec w $($cont): pauza 3 s skraca material do 7 s, jeden plik" ($j.clean -and $j.parts.Count -eq 1 -and @($j.pauses).Count -eq 1 -and $p.Frames -ge 171 -and $p.Frames -le 179 -and $p.DecodeErrors -eq 0) "($($p.Frames) klatek)"
    Remove-Item $out -Recurse -Force
}

'--- para niedozwolona: AV1 + MPEG-TS'
$out = Record @('--codec', 'av1', '--container', 'ts')
$log = Get-Content "$out\log.txt" -Encoding utf8 -ErrorAction SilentlyContinue
Check 'nagranie trafia do MKV, z komunikatem' ((@(Get-ChildItem $out -Filter *.mkv).Count -eq 1) -and (@($log | Select-String 'cannot hold').Count -eq 1))
Remove-Item $out -Recurse -Force

'--- ProRes 1080p25 w czasie rzeczywistym (12 s)'
$out = Record @('--codec', 'prores', '--container', 'mov', '--size', '1920x1080') 12
$j = Get-ChildItem $out -Filter *.json | Select-Object -First 1 | ForEach-Object { Get-Content $_.FullName -Raw -Encoding utf8 | ConvertFrom-Json }
$p = Probe (Join-Path $out $j.parts[0].file)
$log = Get-Content "$out\log.txt" -Encoding utf8 -ErrorAction SilentlyContinue
$mbps = (Get-Item (Join-Path $out $j.parts[0].file)).Length * 8 / 12 / 1e6
Check 'ProRes 1080p: komplet klatek, bez ostrzezenia o nienadazaniu' ($j.clean -and $p.Frames -ge 296 -and $p.Frames -le 302 -and @($log | Select-String 'cannot keep up').Count -eq 0) ("({0} klatek, {1:N0} Mb/s)" -f $p.Frames, $mbps)
Remove-Item $out -Recurse -Force
"Wynik: bledow $errors"
exit $errors
