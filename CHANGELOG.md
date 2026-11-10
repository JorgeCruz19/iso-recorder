# Historia wersji

Numeracja: 0.1, 0.2, … (plan wersji: `instrukcja_koncowa.md`, rozdz. 6).

## 0.7

Kolejność kanałów:
- Kafel można przeciągnąć myszą w inne miejsce siatki. Za kursorem idzie półprzezroczysta kopia kafla,
  pomarańczowa kreska pokazuje miejsce wstawienia. Upuszczenie poza siatką niczego nie zmienia.
- Przeciągać można za obraz albo za pasek kafla (poza przyciskami); zwykłe kliknięcie działa jak dotąd.
- Przyciski MOVE UP / MOVE DOWN zostają. Zmiana kolejności nie przerywa podglądu ani nagrywania.

Test: `IsoRecorder.exe --demo N --drag-test A B [zrzut.png]` przeciąga kafel A na miejsce kafla B
zdarzeniami myszy i wypisuje kolejność kanałów.

Plan: pauza przechodzi do 0.8, sesja i listy do 0.9, dalsze pozycje o jedną wersję.

## 0.6

Split:
- SPLIT na kaflu i dla zaznaczonych: zamyka bieżący plik i od razu zaczyna następny, bez utraty klatek.
- Auto-split w zakładce Output: co 1–60 minut albo co 1 / 2 / 4 GB.
- Części numerowane od 001; numer bieżącej części widać na kaflu obok czasu. Każda część MKV zaczyna się od zera.
- Nowy kontener do wyboru: MPEG-TS (obok MKV).
- Plik opisu nagrania (.json) obok plików wideo: kanał, enkoder, czasy startu i końca w UTC, lista części
  z położeniem na osi nagrania.

Dyski:
- Nagłówek pokazuje dysk, który zapełni się najszybciej, czas do zapełnienia i liczbę używanych dysków.
- Okno Disks (klik w komórkę Disk albo ☰ → Disks…): wolne miejsce, kanały, potrzebny strumień, czas do zapełnienia.
- Test prędkości zapisu 10 s / 60 s dla każdego dysku: średnia, najgorsza sekunda i werdykt względem potrzeb kanałów.
- Ostrzeżenie, gdy zostaje mniej niż 30 minut lub 10% miejsca; poniżej 1 GB nagrania na tym dysku są zatrzymywane
  z poprawnym zamknięciem plików. Ostrzeżenie przy folderze na dysku FAT32 i gdy zapis nie nadąża.

Test: `tests\rec_test.ps1` z opcjami `-SplitEvery`, `-AutoSplit`, `-Container`.

Jeszcze nie ma: pauzy, wspólnego momentu startu wielu kanałów, zapisu i wczytywania list.

## 0.5

Nagrywanie:
- Rec / Stop na kaflu i dla zaznaczonych: zapis do MKV (H.264 + AAC), jeden plik na kanał, podgląd działa bez przerwy.
- Enkoder wybierany automatycznie po próbie rzeczywistego kodowania (NVENC → Quick Sync → AMF → Media Foundation → OpenH264)
  albo wskazany w ustawieniach; gdy wskazany nie działa, program bierze następny i informuje o tym.
- Zakładka Output: folder (osobny dla każdego kanału), szablon nazwy, enkoder, bitrate obrazu i dźwięku,
  „Apply to selected channels”. Input i Output są zablokowane podczas nagrywania.
- Nazwy plików z szablonu `{session}_{index:02}_{channel}_{date}_{time}_{part:03}`; domyślny folder: Wideo\ISO Recorder.
- Stop finalizuje plik (limit 10 s); zamknięcie programu i usunięcie kanału w trakcie nagrywania najpierw kończą zapis.
- Na kaflu: czerwona ramka, licznik czasu, stan SAVING; w nagłówku: czas nagrania kanału aktywnego, REC,
  łączny strumień zapisu, liczba sesji enkoderów sprzętowych.
- Test: `tests\rec_test.ps1` (ffprobe: czas, start od zera, klatka kluczowa na początku, ciągłość PTS).

Interfejs:
- Wiersz Selection ma mniejsze przyciski i dwie nowe akcje na zaznaczonych: DUPLICATE i REMOVE.
- Menu pod prawym przyciskiem na kaflu (Duplicate / Remove); skróty Shift+Del, Ctrl+D, R, S.
- Legenda stanów (REC / PAUSED / PREVIEW / WAITING) przeniesiona do paska stanu.

Jeszcze nie ma: pauzy i splitu (przyciski pokazują komunikat), wspólnego momentu startu wielu kanałów
(kanały startują po kolei, ok. 60 ms odstępu), monitora wielu dysków i testu prędkości zapisu.

## 0.4

- Panel sterowania przeniesiony spod siatki do prawej kolumny, pod inspektor (cztery grupy jedna pod drugą).
- Blok wideo zajmuje całą wysokość okna, a jego szerokość jest wyliczana tak, żeby 16 kafli 16:9 (4×4)
  wypełniło go dokładnie, z równymi marginesami; prawa kolumna dostaje resztę szerokości (384–560 px).
- Większe kafle: przy 1600×900 kadr ma ok. 289×163 px zamiast 247×139.
- Treść zakładek inspektora jest przewijana; licznik kanałów stoi w tytule grupy Channel list.
- Plan wersji przesunięty o jedną.

## 0.3

- Kadr wideo zawsze 16:9 — na kaflach i w inspektorze. Źródła o innych proporcjach są wpisane w kadr w całości,
  z czarnymi pasami; nic nie jest obcinane.
- Kanał aktywny: pomarańczowa ramka wokół całego kafla i pomarańczowa etykieta (zamiast cienkiej białej linii).
- Kanały zaznaczone: większe, białe pole wyboru i jaśniejszy pasek pod kadrem.
- Inspektor: wybór formatu generatora testowego (16:9, 4:3, 9:16, 2.40:1, 1:1) do sprawdzania proporcji.
- Instrukcja końcowa: opis sesji i list kanałów (5.5), zapisu na kilku dyskach (5.6) i testu prędkości zapisu (5.7).
- Plan wersji przesunięty o jedną.

## 0.2

- Interfejs w jednym języku: domyślnie angielski, do wyboru polski, niemiecki, hiszpański, włoski, francuski
  (☰ → Language; wybór zapamiętywany, zmiana bez restartu i bez przerywania kanałów).
- Panel sterowania podzielony na cztery grupy: Selected channels, Selection, Active channel, Channel list.
- Nowe przyciski SELECT NONE i INVERT (skróty: Ctrl+A, Ctrl+Shift+A, Ctrl+I); SELECT ALL już tylko zaznacza.
- ADD SOURCE przeniesiony do grupy Channel list; MOVE UP / MOVE DOWN do grupy Active channel.
- Szerokość przycisków dopasowana do napisu; nagłówek mieści się przy 1600 px w każdym języku.
- Plan wersji przesunięty o jedną: nagrywanie od 0.3 (szczegóły: `instrukcja_koncowa.md`, rozdz. 6).

## 0.1

- Szkielet projektu: CMake + Ninja, Qt 6 Widgets, GStreamer (MSVC x64); skrypty `scripts/build.ps1` i `scripts/run.ps1`.
- Okno główne według mockupu: nagłówek (sesja, czas nagrania, timecode RTC, dysk, CPU), siatka kafli, inspektor, panel sterowania, pasek stanu.
- Kafel kanału malowany w całości: obraz, ramka stanu, pole zaznaczenia, format, miernik audio, etykieta, pasek przycisków.
- Kanały z generatorem testowym (obraz + dźwięk): podgląd 15 kl./s i mierniki poziomu; do 16 kanałów, osobny pipeline na kanał.
- Dodawanie, usuwanie, zaznaczanie, kanał aktywny, zmiana nazwy, przesuwanie w górę/w dół, widoki 2×2 / 3×3 / 4×4.
- Inspektor: nazwa, wzór obrazu i sygnał audio kanału aktywnego.
- Sprawdzian środowiska: `IsoRecorder.exe --check-env` oraz ☰ → Diagnostyka.
- Tryby pomocnicze: `--demo N`, `--screenshot plik.png`.
- Paczka samodzielna: `scripts/package.ps1` składa `dist\IsoRecorder\` (EXE + Qt + GStreamer, uruchamiana dwuklikiem);
  składana przy każdej wersji, zastępuje poprzednią, nie trafia do repo.

Jeszcze nie działa (przyciski widoczne, ale nieaktywne lub z komunikatem): nagrywanie, split, pauza, zapis/wczytanie
listy, prawdziwe wejścia, scheduler i widok Lista. Aktualne numery wersji: `instrukcja_koncowa.md`, rozdz. 6.
