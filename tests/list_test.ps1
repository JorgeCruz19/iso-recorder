# Test listy kanalow: zapis -> wczytanie -> ponowny zapis musi dac identyczny plik.
# 1. Program startuje z N kanalami testowymi o roznych ustawieniach (formaty, kontener, auto-split, wyjscie SRT) i zapisuje liste A.
# 2. Program wczytuje liste A i zapisuje liste B.
# 3. A i B musza byc identyczne bajt w bajt; dodatkowo sprawdzana jest zawartosc A.
# Uzycie:  tests\list_test.ps1 [-Channels 6] [-Exe build\IsoRecorder.exe]
# Kod wyjscia 0 = wszystko poprawne.
param([int]$Channels = 6, [string]$Exe)
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
$dir = Join-Path $env:TEMP "isorec_list_$(Get-Date -Format 'HHmmss')"
New-Item -ItemType Directory $dir | Out-Null
$a = Join-Path $dir 'a.isorec.json'; $b = Join-Path $dir 'b.isorec.json'

function Run($arguments) {
    $p = Start-Process $Exe -ArgumentList $arguments -PassThru
    if (-not $p.WaitForExit(40000)) { $p.Kill(); throw 'Program nie zakonczyl sie w oczekiwanym czasie.' }
}
Run @('--demo', $Channels, '--lang', 'en', '--out', (Join-Path $dir 'rec'), '--container', 'ts', '--auto-split', 300, '--live', 'srt', 9900, '--save-list', $a)
Run @('--list', $a, '--lang', 'en', '--save-list', $b)

$fail = 0
if (-not (Test-Path $a)) { 'BLAD: lista A nie powstala'; exit 1 }
if (-not (Test-Path $b)) { 'BLAD: lista B nie powstala (wczytanie A nie powiodlo sie?)'; exit 1 }
$same = (Get-FileHash $a).Hash -eq (Get-FileHash $b).Hash
"{0}  lista po wczytaniu i ponownym zapisie jest identyczna ({1} bajtow)" -f $(if ($same) { 'ok  ' } else { 'BLAD' }), (Get-Item $a).Length
if (-not $same) { $fail++; Compare-Object (Get-Content $a) (Get-Content $b) | Select-Object -First 10 | Format-Table -AutoSize | Out-String }

$j = Get-Content $a -Raw -Encoding utf8 | ConvertFrom-Json
$checks = @(
    @('schemat isorec.list', ($j.schema -eq 'isorec.list')),
    @("kanalow $Channels", ($j.channels.Count -eq $Channels)),
    @('kontener ts', (@($j.channels | Where-Object { $_.output.container -ne 'ts' }).Count -eq 0)),
    @('auto-split 300 s', (@($j.channels | Where-Object { $_.output.autoSplit.mode -ne 'time' -or $_.output.autoSplit.seconds -ne 300 }).Count -eq 0)),
    @('wyjscie SRT z roznymi portami', (@($j.channels | ForEach-Object { $_.live.srtAddress } | Select-Object -Unique).Count -eq $Channels -and $j.channels[0].live.type -eq 'srt')),
    @('kanal 3 ma format 1024x768', ($Channels -lt 3 -or ($j.channels[2].input.width -eq 1024 -and $j.channels[2].input.height -eq 768))),
    @('zaznaczenie zapisane', ($j.channels[0].selected -eq $true))
)
foreach ($c in $checks) { "{0}  {1}" -f $(if ($c[1]) { 'ok  ' } else { 'BLAD' }), $c[0]; if (-not $c[1]) { $fail++ } }
# --- Autozapis, przywracanie po awarii i otwieranie ostatniej listy - w osobnym profilu ustawien (ISOREC_PROFILE).
$prof = "test$(Get-Date -Format 'HHmmss')"
$env:ISOREC_PROFILE = $prof
$auto = Join-Path $env:LOCALAPPDATA "IsoRecorder-$prof\autosave.json"
$c = Join-Path $dir 'c.isorec.json'; $d = Join-Path $dir 'd.isorec.json'; $e = Join-Path $dir 'e.isorec.json'
try {
    # 1. trzy kanaly, po 6 s proces jest zabijany ("awaria") - autozapis musi zostac na dysku
    $p = Start-Process $Exe -ArgumentList @('--demo', 3, '--persist', '--lang', 'en') -PassThru
    Start-Sleep 6; $p.Kill(); $p.WaitForExit()
    $n = if (Test-Path $auto) { (Get-Content $auto -Raw -Encoding utf8 | ConvertFrom-Json).channels.Count } else { -1 }
    $ok = $n -eq 3; "{0}  autozapis po 'awarii' istnieje i ma 3 kanaly (ma: {1})" -f $(if ($ok) { 'ok  ' } else { 'BLAD' }), $n; if (-not $ok) { $fail++ }
    # 2. nastepny start przywraca liste z autozapisu; po poprawnym wyjsciu autozapis znika
    Run @('--restore', '--lang', 'en', '--save-list', $c)
    $n = if (Test-Path $c) { (Get-Content $c -Raw -Encoding utf8 | ConvertFrom-Json).channels.Count } else { -1 }
    $ok = $n -eq 3; "{0}  nastepny start przywraca liste z autozapisu (kanalow: {1})" -f $(if ($ok) { 'ok  ' } else { 'BLAD' }), $n; if (-not $ok) { $fail++ }
    $ok = -not (Test-Path $auto); "{0}  autozapis usuniety po poprawnym wyjsciu" -f $(if ($ok) { 'ok  ' } else { 'BLAD' }); if (-not $ok) { $fail++ }
    # 3. zapis "jak przyciskiem SAVE" zapamietuje liste jako ostatnia; start bez argumentow listy otwiera ja sam
    Run @('--persist', '--list', $a, '--lang', 'en', '--save-list-real', $d)
    $p = Start-Process $Exe -ArgumentList @('--lang', 'en') -PassThru
    Start-Sleep 6; $dirty = Test-Path $auto; $p.Kill(); $p.WaitForExit()
    "{0}  lista otwarta przy starcie nie jest oznaczona jako zmieniona" -f $(if (-not $dirty) { 'ok  ' } else { 'BLAD' }); if ($dirty) { $fail++ }
    Run @('--lang', 'en', '--save-list', $e)
    $ok = (Test-Path $d) -and (Test-Path $e) -and ((Get-FileHash $d).Hash -eq (Get-FileHash $e).Hash)
    "{0}  start bez argumentow otwiera ostatnio zapisana liste" -f $(if ($ok) { 'ok  ' } else { 'BLAD' }); if (-not $ok) { $fail++ }
} finally {
    $env:ISOREC_PROFILE = $null
    Remove-Item "HKCU:\Software\IsoRecorder-$prof" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $env:LOCALAPPDATA "IsoRecorder-$prof") -Recurse -Force -ErrorAction SilentlyContinue
}
Remove-Item $dir -Recurse -Force
"Wynik: bledow $fail"
exit $fail
