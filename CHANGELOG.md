# Historia wersji

Numeracja: 0.1, 0.2, … (plan wersji: `instrukcja_koncowa.md`, rozdz. 6).

## 1.2

Sesja i listy kanałów:
- Listę kanałów można zapisać do pliku `<sesja>.isorec.json` i wczytać z powrotem: kanały w kolejności, nazwy,
  ustawienia wejścia, zapisu (folder, szablon nazw, kontener, enkoder, bitrate, auto-split), wyjścia na żywo, zaznaczenie.
- LOAD i SAVE w grupie Channel list; w menu ☰: New list, Load list…, Save list, Save as…, Recent lists (8 ostatnich).
  Skróty: Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Shift+S.
- Nazwa sesji w nagłówku jest edytowalna (klik). Gwiazdka przy nazwie oznacza niezapisane zmiany. Sesja bez nazwy
  dostaje ją z nazwy pliku przy pierwszym zapisie.
- Autozapis 2 s po każdej zmianie (czas ostatniego autozapisu w pasku stanu); po awarii program proponuje
  przywrócenie niezapisanej listy.
- Przy starcie program otwiera ostatnio używaną listę (☰ → Open last list at startup).
- Zamknięcie programu, nowa lista i wczytanie innej pytają o niezapisane zmiany. Podmiana listy przy trwających
  nagraniach wymaga potwierdzenia i najpierw kończy pliki.
- Zapis jest atomowy (plik tymczasowy + podmiana). Wczytywanie jest odporne: nieznane pola są pomijane, brakujące
  dostają wartości domyślne.

Test: `tests\list_test.ps1` — lista po wczytaniu i ponownym zapisie jest identyczna; autozapis po zabiciu procesu,
przywrócenie, otwarcie ostatniej listy (w osobnym profilu ustawień `ISOREC_PROFILE`).

Jeszcze nie ma: wspólnego momentu startu wielu kanałów (osobna pozycja planu, 1.3), zapamiętywania układu okna
i wyników testu dysków w liście.

## 1.1

Wyjście na żywo (zakładka Output → Live output):
- Każdy kanał może równolegle do nagrywania — i bez niego — wysyłać obraz z dźwiękiem na NDI albo SRT.
- NDI: nazwa źródła do wpisania (domyślnie nazwa kanału); obraz bez kompresji po stronie programu.
- SRT: MPEG-TS z H.264 + AAC, własny bitrate strumienia (2–20 Mb/s) niezależny od nagrania. Adres `:9001` — odbiorcy
  łączą się z tym komputerem; `host:9001` — program łączy się z odbiorcą. Domyślny port to 9000 + numer kanału.
- Kafel pokazuje niebieski znacznik NDI / SRT. Gdy wyjście zawiedzie (zajęty port, brak środowiska NDI), znacznik
  szarzeje, w pasku stanu jest komunikat, a podgląd i nagrywanie działają dalej.
- Pauza i split nagrania nie przerywają wyjścia. Wyjście ustawia się przed nagrywaniem (podczas nagrywania
  ustawienia są zablokowane). Kopia kanału (DUPLICATE) ma wyjście wyłączone, żeby nie dublować nazwy ani portu.

Test: `tests\live_test.ps1 -Type srt|ndi` — odbiór strumienia każdego kanału w trakcie nagrywania i kontrola plików.

Jeszcze nie ma: wirtualnej kamery (na razie przez NDI Tools → Webcam), SRT z hasłem, RTMP, wyjścia z dźwiękiem
innym niż stereo. Plan: sesja i listy przechodzą do 1.2, dalsze pozycje o jedną wersję.

## 1.0

Pauza:
- PAUSE na kaflu, w oknie dużego podglądu, dla zaznaczonych i pod klawiszem `P` wstrzymuje nagranie; ponowne PAUSE
  albo REC wznawia je w tym samym pliku. STOP w pauzie kończy plik w miejscu, w którym zaczęła się pauza.
- W pliku nie ma śladu pauzy: znaczniki czasu obrazu są ciągłe co do klatki, obraz i dźwięk dostają to samo
  przesunięcie, więc pauzy nie rozsynchronizowują ich nawet po wielu powtórzeniach. Po wznowieniu od razu klatka kluczowa.
- W miejscu cięcia dźwięk może mieć lukę do ok. 60 ms (jest przycinany do granic wyznaczonych przez klatki obrazu).
- Kafel w pauzie ma bursztynową ramkę, podświetlony przycisk II i zatrzymany licznik; licznik czasu pokazuje
  długość materiału bez pauz. Auto-split po czasie też liczy tylko nagrany materiał.
- Dla zaznaczonych PAUSE działa jak jeden przycisk: jeśli którykolwiek kanał nagrywa — wstrzymuje nagrywające,
  w przeciwnym razie wznawia wstrzymane.
- Plik opisu (.json) ma listę pauz: położenie w pliku i długość każdej.

Test: `tests\rec_test.ps1 -PauseAt S -PauseFor P [-PauseCount N]` — czas materiału, ciągłość znaczników obrazu
i dźwięku, wspólny koniec obrazu i dźwięku, liczba pauz w opisie.

Jeszcze nie ma: wspólnego momentu startu i pauzy wielu kanałów (kanały reagują po kolei), zapisu i wczytywania list.

## 0.9

Okno dużego podglądu:
- Pod obrazem są przyciski REC / PAUSE / STOP / SPLIT dla tego kanału, obok mierniki audio.
- Przyciski są dostępne i podświetlane według tych samych reguł co na kaflu (PAUSE zadziała od wersji z pauzą).

Test: `IsoRecorder.exe --demo N --shot-big K zrzut.png --big-rec` klika REC i STOP w oknie.

Plan: pauza przechodzi do 1.0, sesja i listy do 1.1, dalsze pozycje o jedną wersję.

## 0.8

Duży podgląd:
- Dwuklik w obraz na kaflu otwiera okno wielkości ok. 80% ekranu z dużym podglądem kanału.
- W oknie widać numer i nazwę kanału, format, ramkę stanu, czas nagrania oraz mierniki audio L/R ze skalą dBFS.
- Na czas otwarcia okna kanał podaje podgląd 1280×720 z pełną częstotliwością klatek (zwykle 480×270, 15 kl./s).
- Zamknięcie: Esc, dwuklik w oknie albo przycisk okna. Okno nie blokuje programu; dwuklik w inny kafel przełącza je
  na ten kanał. Dwuklik w etykietę z nazwą nadal zmienia nazwę.

Test: `IsoRecorder.exe --demo N --shot-big K zrzut.png`.

Plan: pauza przechodzi do 0.9, sesja i listy do 1.0, dalsze pozycje o jedną wersję.

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
