# Test harmonogramu nagrywania (Scheduler).
#   1. obliczenia okien (zmiana czasu, okno przez polnoc, reguly tygodniowe) - wbudowany --sched-selftest
#   2. zaplanowane nagranie: start o zadanej sekundzie, stop na koncu okna, podzial pliku w trakcie, start grupy kanalow razem
#   3. spozniony start (okno juz trwa przy starcie programu) - wlaczony i wylaczony
#   4. wyjscie na zywo "jak w harmonogramie": nadaje tylko w oknie
# Uzycie:  .\tests\sched_test.ps1 [-Exe sciezka]
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
function Run([string[]]$appArgs, [int]$limit = 90) {
    if ($Exe -like '*\build\*') { $env:PATH = "C:\Qt\6.10.3\msvc2022_64\bin;$gstBin;$saved" }
    $env:ISOREC_PROFILE = 'schedtest'
    $o = Join-Path $env:TEMP 'isorec_sched_out.txt'
    Remove-Item $o -ErrorAction SilentlyContinue
    $p = Start-Process $Exe -ArgumentList $appArgs -PassThru -RedirectStandardOutput $o
    $null = $p.Handle      # bez tego ExitCode bywa pusty
    $env:PATH = $saved; $env:ISOREC_PROFILE = $null
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while (-not $p.HasExited -and $sw.Elapsed.TotalSeconds -lt $limit) { Start-Sleep -Milliseconds 300 }
    if (-not $p.HasExited) { $p.Kill(); Check 'program zakonczyl sie sam' $false }
    $script:exitCode = $p.ExitCode
    if (Test-Path $o) { Get-Content $o -Encoding utf8 } else { @() }
}
function NewOut([string]$name) {
    $d = Join-Path $env:TEMP "isorec_sched_$name"
    Remove-Item $d -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory $d | Out-Null
    $d
}
function Sidecars([string]$dir) {
    Get-ChildItem $dir -Filter *.json | ForEach-Object { Get-Content $_.FullName -Raw -Encoding utf8 | ConvertFrom-Json }
}
function Frames([string]$file) {
    [int](cmd /c "ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames -of csv=p=0 `"$file`" 2>&1")
}

'--- 1. obliczenia okien'
$lines = Run @('--sched-selftest') 30
$lines | ForEach-Object { "      $_" }
Check 'obliczenia harmonogramu bez bledow' ($script:exitCode -eq 0 -and $lines.Count -ge 8) "(kod $($script:exitCode))"

'--- 2. zaplanowane nagranie: 2 kanaly, start za 6 s, 12 s, podzial co 5 s'
$out = NewOut 'a'
$lines = Run @('--demo', 2, '--lang', 'en', '--out', "`"$out`"", '--sched-in', 6, '--sched-len', 12, '--sched-split', 5, '--sched-report', '--quit-after', 24, '--dump-log', "`"$out\log.txt`"")
$ms = [int64]((@($lines | Where-Object { $_ -match '^sched-start=' })[0]) -replace 'sched-start=', '')
$planned = [DateTimeOffset]::FromUnixTimeMilliseconds($ms).LocalDateTime
$sc = @(Sidecars $out)
Check 'dwa nagrania' ($sc.Count -eq 2) "($($sc.Count))"
foreach ($j in $sc) {
    $frames = 0; foreach ($p in $j.parts) { $frames += Frames (Join-Path $out $p.file) }
    $started = [DateTime]::Parse($j.startedUtc).ToLocalTime()
    $late = ($started - $planned).TotalSeconds
    Check "$($j.channel): zamkniete poprawnie" ($j.clean -eq $true)
    Check "$($j.channel): start o zaplanowanej chwili (+-0,3 s)" ([math]::Abs($late) -lt 0.3) ("({0:N2} s)" -f $late)
    Check "$($j.channel): dlugosc 12 s (296-304 klatki)" ($frames -ge 296 -and $frames -le 304) "($frames klatek)"
    Check "$($j.channel): 3 czesci (podzial w 5. i 10. sekundzie)" ($j.parts.Count -eq 3) "($($j.parts.Count))"
}
if ($sc.Count -eq 2) {
    $d = [math]::Abs($sc[0].startClockNs - $sc[1].startClockNs) / 1e6
    Check 'oba kanaly ruszyly w tej samej chwili' ($d -lt 1) ("({0:N2} ms roznicy)" -f $d)
}
$w = @($lines | Where-Object { $_ -match 'waiting=1' }).Count
Check 'przed startem kanal pokazuje stan oczekiwania' ($w -ge 2) "($w s)"
$log = Get-Content "$out\log.txt" -Encoding utf8 -ErrorAction SilentlyContinue
Check 'log: start i stop wg harmonogramu' ((@($log | Select-String 'scheduled recording started').Count -eq 2) -and (@($log | Select-String 'scheduled stop').Count -eq 2))
Remove-Item $out -Recurse -Force

'--- 3a. spozniony start: okno zaczelo sie 20 s temu, trwa jeszcze 10 s'
$out = NewOut 'b'
$lines = Run @('--demo', 1, '--lang', 'en', '--out', "`"$out`"", '--sched-in', -20, '--sched-len', 30, '--quit-after', 16, '--dump-log', "`"$out\log.txt`"")
$sc = @(Sidecars $out)
$frames = 0; if ($sc.Count) { foreach ($p in $sc[0].parts) { $frames += Frames (Join-Path $out $p.file) } }
Check 'nagranie powstalo i skonczylo sie z koncem okna (ok. 7-10 s)' ($sc.Count -eq 1 -and $frames -ge 150 -and $frames -le 260) "($frames klatek)"
Check 'log: start z opoznieniem' (@(Get-Content "$out\log.txt" -Encoding utf8 | Select-String 'started late').Count -eq 1)
Remove-Item $out -Recurse -Force

'--- 3b. to samo z wylaczonym spoznionym startem'
$out = NewOut 'c'
$lines = Run @('--demo', 1, '--lang', 'en', '--out', "`"$out`"", '--sched-in', -20, '--sched-len', 30, '--sched-nocatch', '--quit-after', 8, '--dump-log', "`"$out\log.txt`"")
Check 'nagranie nie powstalo' (@(Get-ChildItem $out -Filter *.mkv).Count -eq 0)
Check 'log: start pominiety' (@(Get-Content "$out\log.txt" -Encoding utf8 | Select-String 'start missed').Count -eq 1)
Remove-Item $out -Recurse -Force

'--- 4. wyjscie na zywo "jak w harmonogramie" (NDI): start za 6 s, okno 8 s'
$out = NewOut 'd'
$lines = Run @('--demo', 1, '--lang', 'en', '--out', "`"$out`"", '--live', 'ndi', '--sched-live', '--sched-in', 6, '--sched-len', 8, '--sched-report', '--quit-after', 20)
$before = @($lines | Where-Object { $_ -match '^t=[1-4] ' -and $_ -match 'live=1' }).Count
$during = @($lines | Where-Object { $_ -match 'state=2' -and $_ -match 'live=1' }).Count
$after = @($lines | Where-Object { $_ -match '^t=(17|18|19) ' -and $_ -match 'live=1' }).Count
Check 'przed oknem wyjscie nie nadaje' ($before -eq 0) "($before s)"
Check 'w oknie wyjscie nadaje razem z nagraniem' ($during -ge 6) "($during s)"
Check 'po oknie wyjscie nie nadaje' ($after -eq 0) "($after s)"
Remove-Item $out -Recurse -Force

"Wynik: bledow $errors"
exit $errors
