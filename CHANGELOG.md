# Historia wersji

Numeracja: 0.1, 0.2, … (plan wersji: `instrukcja_koncowa.md`, rozdz. 6).

## 2.0

Pierwsze wydanie publikowane jako Release.

Źródła:
- **Przechwytywanie okna**: Input type → Window capture, okno wybiera się z listy. Program zapamiętuje tytuł okna
  i szuka go przy każdym starcie kanału.
- **Tryb kamery**: wybór rozmiaru obrazu z listy podawanej przez kamerę (Auto = wybór sterownika).
- **Głośność wejścia** (Input gain): od −24 do +18 dB, działa od razu, także podczas nagrywania.
- **Reakcja na zanik sygnału** (SRT, RTSP) do wyboru: nagrywaj dalej z czarnym obrazem, pauza do powrotu sygnału
  albo nowa część pliku po powrocie. Można zmienić w trakcie nagrywania.

Odporność:
- **Automatyczny restart źródła.** Gdy źródło padnie (zamknięte okno, odłączona kamera, błąd), kanał przechodzi w stan
  błędu i sam próbuje wystartować ponownie — po 5, 10, 20 s, potem co 30 s. Jeśli w chwili awarii nagrywał, plik jest
  poprawnie zamykany, a po udanym restarcie nagrywanie rusza samo w nowym pliku. Każdy krok jest w pasku stanu i w logu.
- Kanał w stanie błędu startuje od nowa także po zmianie jego ustawień.

Log zdarzeń:
- Wszystkie komunikaty programu (start i koniec nagrań, pauzy, zaniki i powroty sygnału, błędy, restarty, listy)
  trafiają z datą i godziną do pliku dziennego w `%LOCALAPPDATA%\IsoRecorder\logs`. Pliki starsze niż 30 dni są usuwane.
- Okno logu: ☰ → Event log… albo klik w komunikaty w pasku stanu; przycisk otwiera folder z plikami.

Widok listy:
- Kolejność kanałów można zmieniać przeciąganiem wiersza (pomarańczowa kreska pokazuje miejsce wstawienia).

Inne:
- ☰ → About podaje autorów. README z opisem programu.

Test: `tests\net_test.ps1 -Loss pause|split`, `tests\rec_test.ps1 -Source window -Window "tytuł"`.

Nie sprawdzone na sprzęcie: tryb kamery (na maszynie deweloperskiej są tylko wirtualne kamery).
Źródło, które w ogóle nie odpowiada (limit czasu startu), nie jest ponawiane automatycznie — każda próba blokowałaby
okno na 2,5 s; taki kanał trzeba uruchomić zmianą ustawień.

## 1.9

Widok listy:
- Miniatury są większe. Wysokość wiersza wynika z miejsca: 16 kanałów (maksimum) mieści się bez przewijania,
  a przy mniejszej liczbie wiersze i miniatury rosną (przy 8 kanałach miniatura ma ok. 135×76 zamiast 64×36).
- W nagłówku, nad polami zaznaczenia, jest pole „zaznacz wszystkie”: klik zaznacza wszystkie kanały, a gdy wszystkie
  są zaznaczone — odznacza. Kreska w polu oznacza, że zaznaczona jest część.
- W nagłówku, nad przyciskami wierszy, są przyciski REC / II / STOP / SPLIT działające na zaznaczone kanały
  (te same reguły co w panelu Selected channels: wspólna chwila, pytanie przed zatrzymaniem kilku nagrań).

## 1.8

Widok listy:
- Przycisk List w nagłówku przełącza kafle na listę: jeden wiersz na kanał z miniaturą, nazwą, źródłem, formatem,
  stanem, czasem nagrania, strumieniem zapisu, wyjściem na żywo, miernikami L/R i przyciskami REC / II / STOP / SPLIT.
- Zachowanie jak na kaflach: klik = kanał aktywny (pomarańczowy pasek z lewej), pole albo Ctrl+klik = zaznaczenie,
  dwuklik w miniaturę = duży podgląd, dwuklik w nazwę = zmiana nazwy, prawy przycisk = Duplicate / Remove.
- Wybrany widok (2×2, 3×3, 4×4, List) jest pamiętany między uruchomieniami.

Inspektor:
- Listy rozwijane mają szerokość kolumny zamiast najdłuższej pozycji. Długie nazwy urządzeń rozpychały wcześniej
  formularz poza krawędź okna i obcinały etykiety. Długi tekst jest skracany wielokropkiem w środku, pełna nazwa
  jest w podpowiedzi, a rozwinięta lista ma szerokość pola i najwyżej 12 pozycji (dalej przewijanie).
- Etykiety pól mają stałą szerokość i łamią się na dwie linie zamiast być obcinane.
- Strzałka listy rozwijanej jest strzałką (wcześniej rysował się kwadracik).

Jeszcze nie ma w widoku listy: zmiany kolejności przeciąganiem (działają MOVE UP / MOVE DOWN) i sortowania kolumn.

## 1.7

Poprawki błędów:
- **Zawieszanie przy źródle, które nie odpowiada.** Kamera, która przestała podawać obraz (np. „NDI Webcam” po zniknięciu
  źródła NDI), blokowała program przy zatrzymywaniu kanału: przy zamykaniu, zmianie ustawień i podmianie listy —
  także przy starcie, gdy program sam otwierał ostatnią listę. Start i stop kanału mają teraz limit czasu (2,5 s i 2 s);
  po jego przekroczeniu kanał przechodzi w stan błędu „the source does not respond”, a program działa dalej.
- **Start awaryjny.** Shift trzymany przy uruchamianiu (albo argument `--empty`) otwiera program z pustą listą, bez
  wczytywania ostatniej listy i autozapisu.
- **Zużycie procesora.** Wersja 1.6 zużywała ok. 45% więcej procesora podczas nagrywania (8 kanałów: 3,4 zamiast
  2,3 rdzenia), bo dodana w niej „osłona źródła” blokowała uzgodnienie formatu obrazu i wymuszała zbędną konwersję.
  Przy 16 kanałach maszyna się nasycała i kanały kończyły się nierówno. Osłona została usunięta.

Ograniczenie: po przekroczeniu limitu czasu program porzuca niedziałające źródło i przy wyjściu kończy proces bez
czekania na nie; 16 niedziałających źródeł w liście oznacza do ok. 40 s oczekiwania przy starcie.

## 1.6

Wejścia sieciowe (zakładka Input):
- Input type: SRT albo RTSP, adres w polu Address. SRT: `host:9000` — program łączy się z nadajnikiem, `:9000` —
  program czeka na nadajnik na porcie 9000. RTSP: `rtsp://host:554/stream` (`rtspt://` wymusza TCP).
- Strumień jest dekodowany i wpisywany w format kanału jak każde inne źródło; dźwięk pochodzi ze strumienia.
- Zanik sygnału nie przerywa pracy kanału ani nagrania: przez ok. 2–3 s stoi ostatnia klatka, potem idzie czarny
  obraz i cisza, a program sam łączy się ponownie. Kafel pokazuje RECONNECTING, pasek stanu — komunikat o zaniku
  i o powrocie sygnału. Nagranie pozostaje jednym, poprawnie zamkniętym plikiem.
- Plik opisu (.json) podaje liczbę zaników sygnału w nagraniu i ich łączny czas.

Nagrywanie:
- Gałąź zapisu ma własną oś czasu, niezależną od segmentów źródła (źródło sieciowe po ponownym połączeniu zaczyna
  nową) — bez tego po powrocie sygnału enkoder odrzucał klatki przez kilka sekund.
- Stop czeka na zaległe klatki do 2 s (plus bufor źródła sieciowego), zamiast 0,6 s — przy chwilowym przeciążeniu
  końcówka kanału mogła być krótsza.

Test: `tests\net_test.ps1 -Type srt|rtsp [-NoBreak]` — lokalny nadajnik (ffmpeg albo `build\rtsp_test_server.exe`)
jest w trakcie nagrania zatrzymywany i uruchamiany ponownie.

Ograniczenia: RTSP ma ok. 2 s opóźnienia (bufor) i przy ponownym połączeniu zostawia w znacznikach czasu lukę
ok. 1,9 s; SRT przechodzi zanik bez luki. Nie ma jeszcze: zapisu bez ponownego kodowania (passthrough — osobna
pozycja planu, 1.7), SRT z hasłem, wyboru innej reakcji na zanik (pauza, nowa część), haseł RTSP poza adresem.

## 1.5

Wejścia lokalne (zakładka Input):
- Input type: Test generator, Screen grab (przechwytywanie ekranu) albo Camera (kamera UVC / karta przechwytująca
  widoczna w systemie jako kamera). Pole Device wybiera monitor albo kamerę.
- Audio source: sygnały testowe, cisza, dźwięk systemowy (to, co gra na wybranym wyjściu) albo wejście dźwięku
  (mikrofon, wejście liniowe) — urządzenie domyślne lub wskazane z listy.
- Obraz z każdego źródła jest wpisywany w wybrany format kanału (Video format) z czarnymi pasami przy innych
  proporcjach; kursor myszy jest nagrywany.
- Pasek kafla pokazuje źródło (Screen 1, nazwa kamery). Ustawienia wejścia zapisują się w liście kanałów.
- Nagrywanie, pauza, split, wspólny start grupy i wyjście na żywo działają z nowymi źródłami tak samo.

Stop:
- Zamknięcie pliku czeka, aż obraz i dźwięk dojdą do wspólnej chwili Stop (najdłużej 600 ms). Urządzenia dźwiękowe
  oddają dane z opóźnieniem i bez tego końcówka dźwięku mogła być krótsza od obrazu nawet o 0,17 s.

Test: `tests\rec_test.ps1 -Source screen -Audio loopback|device|none`.

Nie sprawdzone na sprzęcie: wejście Camera — na maszynie deweloperskiej są tylko wirtualne kamery (NDI Webcam,
OBS), które bez działającego programu-źródła nie podają obrazu.

Jeszcze nie ma: wyboru okna zamiast całego ekranu, wyboru rozdzielczości i klatkażu kamery, regulacji głośności
i opóźnienia dźwięku, więcej niż dwóch kanałów dźwięku.

## 1.4

Wspólny moment dla grupy kanałów:
- REC, PAUSE i STOP dla zaznaczonych kanałów działają we wszystkich kanałach w tej samej chwili — różnica między
  kanałami nie przekracza jednej klatki (wcześniej kanały startowały po kolei, ok. 60 ms na kanał).
- Start grupy przebiega w dwóch krokach: najpierw program przygotowuje zapis we wszystkich kanałach, potem wyznacza
  jedną chwilę startu (ok. 80 ms później), od której wszystkie zaczynają zapisywać. Pliki grupy mają więc wspólny
  punkt zero i tę samą liczbę klatek (±1).
- Wszystkie kanały chodzą na jednym zegarze, a plik opisu (.json) podaje na nim chwilę startu oraz czas pierwszej
  i ostatniej zapisanej klatki — do sprawdzenia synchronizacji i do układania materiału w montażu.
- Zamknięcie programu i automatyczne zatrzymanie przy braku miejsca też kończą nagrania we wspólnej chwili.

Test: `tests\sync_test.ps1 [-Channels 16] [-PauseAt S -PauseFor P]`.

Ograniczenie: kanały mają różną fazę klatek (każde źródło tyka po swojemu), więc „ta sama chwila” oznacza dokładność
do jednej klatki, nie co do próbki. Split grupy nadal tnie każdy kanał na jego najbliższej klatce kluczowej.

## 1.3

Wyjście na żywo w trakcie nagrywania:
- Sekcję Live output można zmieniać w każdej chwili, także podczas nagrywania i pauzy: włączyć, wyłączyć,
  przełączyć między NDI i SRT, zmienić nazwę, adres albo bitrate. Nagranie trwa bez przerwy.
- Pozostałe ustawienia zakładki Output (folder, kontener, enkoder…) są nadal zablokowane podczas nagrywania.
- Awaria wyjścia włączanego w trakcie nagrania (np. zajęty port) wyłącza tylko wyjście — plik jest nienaruszony.
- Technicznie: gałąź wyjścia jest dopinana do działającego kanału tak samo jak gałąź nagrywania, zamiast być
  stałą częścią kanału; zmiana wyjścia nie przebudowuje już kanału i nie przerywa podglądu.

Test: `tests\rec_test.ps1 -LiveCycle S [-LiveType srt|ndi]` (przełączanie wyjścia co S sekund w trakcie nagrania)
i `tests\live_test.ps1 -EnableAfter S` (wyjście włączone S sekund po starcie nagrania).

Plan: wspólny moment startu wielu kanałów przechodzi do 1.4, dalsze pozycje o jedną wersję.

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
