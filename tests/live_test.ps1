# Test wyjscia na zywo: N kanalow testowych nagrywa S sekund i rownoczesnie wysyla obraz na SRT albo NDI.
# W trakcie nagrywania strumien kazdego kanalu jest odbierany (SRT: ffprobe, NDI: gst-launch z instalacji GStreamera),
# a po zakonczeniu sprawdzane sa pliki nagran (plik opisu: clean, jedna czesc).
# Uzycie:  tests\live_test.ps1 [-Type srt|ndi] [-Channels 2] [-Seconds 14] [-Port 9700] [-Exe build\IsoRecorder.exe]
# Kod wyjscia 0 = wszystko poprawne.
param([string]$Type = 'srt', [int]$Channels = 2, [int]$Seconds = 14, [int]$Port = 9700, [string]$Exe)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
if (-not $Exe) { $Exe = "$root\build\IsoRecorder.exe" }
$gst = $env:GSTREAMER_1_0_ROOT_MSVC_X86_64
if (-not $gst) { $gst = "$env:LOCALAPPDATA\Programs\gstreamer\1.0\msvc_x86_64" }
$gstBin = "$($gst.TrimEnd('\'))\bin"
$savedPath = $env:PATH
if ($Exe -like "*\build\*") {
    $qt = Get-ChildItem C:\Qt -Directory | ForEach-Object { Get-ChildItem $_.FullName -Directory -Filter 'msvc*_64' } |
        Sort-Object FullName | Select-Object -Last 1 -ExpandProperty FullName
    $env:PATH = "$qt\bin;$gstBin;$env:PATH"
}
$out = Join-Path $env:TEMP "isorec_live_$(Get-Date -Format 'HHmmss')"
New-Item -ItemType Directory $out | Out-Null

$p = Start-Process $Exe -ArgumentList @('--demo', $Channels, '--lang', 'en', '--out', $out, '--live', $Type, $Port, '--rec-test', $Seconds) -PassThru
$env:PATH = $savedPath
Start-Sleep 6          # program startuje, nagrywanie rusza po 3 s

$fail = 0
for ($i = 1; $i -le $Channels; $i++) {
    if ($Type -eq 'srt') {
        # -v quiet: przy dolaczeniu w srodku strumienia dekoder wypisuje ostrzezenia do pierwszej klatki kluczowej
        $r = (ffprobe -v quiet -show_entries stream=codec_name -of csv=p=0 "srt://127.0.0.1:$($Port + $i)?timeout=5000000" |
            ForEach-Object { $_.Trim(',') } | Where-Object { $_ } | Select-Object -Unique | Sort-Object) -join '+'
        $ok = $r -eq 'aac+h264'
        "{0}  kanal {1}  SRT :{2}  odebrano: {3}" -f $(if ($ok) { 'ok  ' } else { 'BLAD' }), $i, ($Port + $i), $r
    } else {
        $name = "$($env:COMPUTERNAME) (ISO TEST $i)"
        # Odbiornik NDI potrzebuje ok. 4 s na znalezienie zrodla; klatki liczymy w dzienniku odbiornika.
        $log = Join-Path $out "ndi_$i.log"
        $env:GST_DEBUG = 'ndireceiver:5'; $env:GST_DEBUG_NO_COLOR = '1'
        $r = Start-Process "$gstBin\gst-launch-1.0.exe" -ArgumentList "ndisrc ndi-name=`"$name`" timeout=10000 ! fakesink" `
            -PassThru -WindowStyle Hidden -RedirectStandardError $log
        if (-not $r.WaitForExit(9000)) { $r.Kill() }
        $env:GST_DEBUG = $null; $env:GST_DEBUG_NO_COLOR = $null
        $v = @(Select-String -Path $log -Pattern 'Received video frame').Count
        $au = @(Select-String -Path $log -Pattern 'Received audio frame').Count
        $ok = $v -ge 25 -and $au -ge 25
        "{0}  kanal {1}  NDI `"{2}`"  odebrano klatek obrazu: {3}, dzwieku: {4}" -f $(if ($ok) { 'ok  ' } else { 'BLAD' }), $i, $name, $v, $au
    }
    if (-not $ok) { $fail++ }
}
if (-not $p.WaitForExit(($Seconds + 30) * 1000)) { $p.Kill(); 'BLAD: program nie zakonczyl sie w oczekiwanym czasie'; $fail++ }

$sidecars = Get-ChildItem $out -Filter *.json
if ($sidecars.Count -ne $Channels) { "BLAD: nagran $($sidecars.Count), oczekiwano $Channels"; $fail++ }
foreach ($sc in $sidecars) {
    $info = Get-Content $sc.FullName -Raw -Encoding utf8 | ConvertFrom-Json
    $f = Join-Path $out $info.parts[0].file
    $dur = if (Test-Path $f) { [double]::Parse((ffprobe -v error -show_entries format=duration -of csv=p=0 $f), [cultureinfo]::InvariantCulture) } else { 0 }
    $ok = $info.clean -and $info.parts.Count -eq 1 -and $dur -gt ($Seconds - 1.5)
    "{0}  {1}  nagranie {2:N2} s" -f $(if ($ok) { 'ok  ' } else { 'BLAD' }), $info.channel, $dur
    if (-not $ok) { $fail++ }
}
Remove-Item $out -Recurse -Force
"Wynik: bledow $fail"
exit $fail
