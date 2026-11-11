# ISO Recorder — stan projektu i instrukcje dla kolejnego modelu

Ten plik jest punktem wejścia. Przeczytaj go w całości, zanim cokolwiek zmienisz. Stan na 2026‑11‑11, wersja **1.9**.

## 1. Co to jest

Natywny program dla Windows do równoczesnego nagrywania 1–16 kanałów wideo z dźwiękiem („ISO recording”).
C++20, Qt 6 Widgets, GStreamer. Jeden kanał = jeden `GstPipeline`; awaria kanału nie dotyka pozostałych.

Właściciel projektu pisze po polsku i tak należy mu odpowiadać. Interfejs programu jest domyślnie po angielsku,
z tłumaczeniami PL, DE, ES, IT, FR.

## 2. Dokumenty — co jest czym

| Plik | Rola |
|---|---|
| `AGENTS.md` | ten plik: stan, zasady pracy, pułapki, następne etapy |
| `instrukcja_koncowa.md` | **obowiązująca** specyfikacja; nadpisuje `instrukcja.md`. Rozdz. 3 — mechanizmy (pauza, split, wejścia, wyjścia, segmenty), rozdz. 5 — interfejs, rozdz. 6 — plan wersji |
| `instrukcja.md` | pierwotna specyfikacja (tło, pełna lista funkcji docelowych) |
| `rozmowa.md` | krytyka pierwotnej specyfikacji przez inny model (tło) |
| `CHANGELOG.md` | historia wersji po polsku, pisana dla użytkownika |
| `mockup/`, `interfejs.png` | wzorzec wyglądu |
| `docs/zrzut_<wersja>.png` | zrzut okna na każdą wersję |

## 3. Co działa (1.9)

- **Wejścia**: generator testowy, ekran (`d3d11screencapturesrc`), kamera (`mfvideosrc`), SRT i RTSP (`fallbacksrc`);
  dźwięk: sygnał testowy, cisza, dźwięk systemowy (loopback) i wejścia WASAPI.
- **Nagrywanie**: H.264 + AAC do MKV lub MPEG‑TS; enkoder wybierany próbą (NVENC → Quick Sync → AMF → MF → OpenH264);
  split ręczny i automatyczny; pauza w tym samym pliku; plik opisu `.json` obok nagrania.
- **Grupa kanałów**: wspólna chwila startu, pauzy i stopu (dokładność do jednej klatki).
- **Wyjście na żywo** kanału, równolegle do nagrania: NDI albo SRT; można przełączać w trakcie nagrywania.
- **Zanik sygnału sieciowego**: nagranie trwa (ostatnia klatka → czarny obraz), automatyczne ponowne łączenie.
- **Sesja i listy kanałów**: zapis / wczytanie `.isorec.json`, autozapis, przywracanie po awarii, ostatnie listy.
- **Interfejs**: kafle 2×2 / 3×3 / 4×4 i widok listy, inspektor (Input / Output), panel sterowania, duży podgląd
  (dwuklik), przeciąganie kafli, okno Disks z testem prędkości zapisu, 6 języków.
- **Odporność**: start i stop kanału mają limit czasu — źródło, które nie odpowiada, nie zawiesza programu.

## 4. Następne etapy (rozdz. 6 `instrukcja_koncowa.md`)

| Wersja | Zakres |
|---|---|
| **2.0** | Zaległe poprawki wybrane przez właściciela: reakcja na zanik sygnału do wyboru (trwa / pauza / nowa część), przechwytywanie pojedynczego okna, wybór rozdzielczości i klatkażu kamery, głośność wejścia dźwięku |
| **2.1** | Passthrough: zapis strumienia SRT/RTSP bez ponownego kodowania; pauza = nowa część |
| **2.2** | Wejście NDI, DeckLink; kodeki H.265 / AV1 / ProRes / DNxHR; MOV / MP4 |
| **2.3** | Scheduler (harmonogram startu / stopu / splitu), blokada uśpienia, logi, folder zapasowy |
| **2.4** | WebRTC (WHEP, WHIP), magazyn poświadczeń, odzyskiwanie po awarii, instalator |

Uwagi do 2.0 — co już ustalono:
- Przechwytywanie okna: prototyp działa — `d3d11screencapturesrc capture-api=wgc window-handle=<HWND>`
  (100 klatek 1280×720 z okna Notatnika). Okna trzeba wyliczać przez `EnumWindows`; zapamiętywać po tytule.
- Tryby kamery: `GstDevice` podaje listę formatów (`gst_device_get_caps`); format wymusza się przez
  `capsfilter caps="video/x-raw,…;image/jpeg,…"` przed `decodebin`.
- Reakcja na zanik: stan zaniku zna `Channel::pollSource()`; „pauza” = `pauseRecording()` / `resumeRecording()`
  wywoływane automatycznie, „nowa część” = `split()` po powrocie sygnału.
- Głośność: element `volume` w torze dźwięku, ustawiany na żywo.

Znane braki poza planem: wirtualna kamera jako wyjście (na Windows 10 brak API; dziś przez NDI Tools → Webcam),
hasło RTSP poza adresem, zmiana kolejności przeciąganiem w widoku listy, kanał w stanie błędu nie ponawia startu sam.

## 5. Stałe zasady pracy (ustalone z właścicielem)

1. **Każda zmiana = wersja.** Numeracja co 0.1. Każda runda poprawek po uwagach dostaje własny numer, a pozycje
   planu poniżej przesuwają się o jedną. Przy przesunięciu popraw: tabelę planu w `instrukcja_koncowa.md`,
   stałe `Roadmap` w `src/core/Lang.h` (napisy „od wersji X” w interfejsie) i dwa odwołania w rozdz. 8.
2. **Rutyna wydania**, w tej kolejności:
   - `CMakeLists.txt`: `project(... VERSION x.y.0)` i `APP_VERSION_SHORT`;
   - wpis w `CHANGELOG.md` (po polsku, dla użytkownika: co się zmieniło, test, czego jeszcze nie ma);
   - aktualizacja `instrukcja_koncowa.md` (plan + opis mechanizmu, jeśli doszedł nowy);
   - zrzut okna do `docs/zrzut_<wersja>.png` — **bez zawartości ekranu właściciela** (używaj generatora testowego);
   - commit `Wersja x.y: …`, tag `vx.y`, `git push origin main --tags` (repo prywatne `studiokrause/iso-recorder`);
   - `scripts\package.ps1` → `dist\IsoRecorder\` (poza repo), potem testy na paczce z `-Exe dist\IsoRecorder\IsoRecorder.exe`.
3. **Commity**: autor `studiokrause <15332134+studiokrause@users.noreply.github.com>`; prywatnego adresu e‑mail
   właściciela nie wolno nigdzie wpisywać ani wysyłać. Wiadomości commitów bez polskich znaków.
4. **Jeden język interfejsu naraz.** Każdy nowy napis: klucz po angielsku przez `T("…")` + wiersz w `src/core/Lang.cpp`
   z pięcioma tłumaczeniami. Po dodaniu napisów obejrzyj zrzut w PL i w DE lub FR (dłuższe teksty).
5. **Raportuj uczciwie**: co sprawdzone i czym, a czego nie. Kliknięć myszą nie da się tu testować — pisz to wprost.
   Wejście kamery i DeckLink nie są sprawdzone na sprzęcie (na maszynie są tylko wirtualne kamery).
6. **Nie ruszaj działającego programu właściciela.** `package.ps1` składa paczkę w `dist\IsoRecorder.new` i podmienia
   starą tylko wtedy, gdy żaden proces z niej nie działa (kod wyjścia 2 = nie podmieniono). Zabijaj tylko procesy,
   które sam uruchomiłeś.

## 6. Środowisko

- Windows 10 LTSC, 6 rdzeni logicznych, tylko Intel Quick Sync (enkoder `qsvh264enc`), dwa monitory.
- MSVC (VS Build Tools 18), CMake + Ninja, Qt 6.10.3 w `C:\Qt\6.10.3\msvc2022_64`.
- GStreamer 1.28.8 MSVC x64 w `%LOCALAPPDATA%\Programs\gstreamer\1.0\msvc_x86_64` — **bez** zmiennej
  `GSTREAMER_1_0_ROOT_MSVC_X86_64`; skrypty szukają tej ścieżki same.
- NDI 6 Tools i OBS są zainstalowane; `ffmpeg` / `ffprobe` w `D:\VideoTools\ffmpeg`.
- Budowanie: `scripts\build.ps1` (komunikat „'vswhere.exe' is not recognized” jest nieszkodliwy).
- Wydajność: 8 × 720p25 nagrywanych = ok. 2,3 rdzenia; 16 kanałów jest blisko granicy tej maszyny.
  **Mierz to po zmianach w torze sygnału** (patrz pułapka 7.6).

## 7. Pułapki, które już kosztowały czas

1. **Heredoc w Bashu** psuje apostrofy i `\n`. Większe zmiany w źródłach rób skryptem Pythona zapisanym narzędziem
   do plików i uruchamianym `python -X utf8`; w ścieżkach używaj `/` albo `\\`.
2. **PowerShell 5.1**: `$Input` i `$PROFILE` to zmienne automatyczne (nie nazywaj tak parametrów), `Try` to słowo
   kluczowe; stderr programu przy `$ErrorActionPreference='Stop'` przerywa skrypt (`ffprobe -v quiet`).
3. **`QApplication::quit()` w Qt 6 wywołuje `closeEvent`** — okno z pytaniem („Zapisać zmiany?”) zawiesi każdy test.
   Uruchomienia sterowane argumentami ustawiają właściwość aplikacji `automation`, która wyłącza pytania.
4. **Testy nie mogą dotykać ustawień właściciela.** Tryby `--demo` / `--list` wyłączają autozapis i pamięć list;
   gdy test ich potrzebuje, ustaw `ISOREC_PROFILE=<nazwa>` (osobny klucz rejestru i folder) i posprzątaj po sobie.
5. **Segmenty czasu.** Źródło sieciowe po ponownym połączeniu zaczyna nowy segment. Gałąź zapisu ma własną oś czasu:
   sondy zapamiętują segment źródła, podmieniają zdarzenie SEGMENT na stały segment od zera i przeliczają znaczniki
   na running‑time. Samo przepisywanie PTS kończy się odrzucaniem klatek przez enkoder.
6. **Nie zatrzymuj zdarzenia RECONFIGURE** idącego od `tee` do źródła. „Osłona źródła” z 1.6 zablokowała uzgodnienie
   formatu, podniosła zużycie procesora o 45% i rozregulowała 16 kanałów. Po każdej zmianie w torze sygnału porównaj
   zużycie procesora z poprzednią wersją (worktree z tagu + ten sam przebieg).
7. **Zmiany stanu GStreamera mogą nie wrócić** (sterownik kamery bez sygnału). Start i stop pipeline'u idą przez
   `setStateTimed()`; nie wołaj `gst_element_set_state` na całym pipeline'ie wprost z wątku okna.
8. **`splitmuxsink`**: muxer przez właściwość `muxer` ustawioną przed żądaniem padów; `async-finalize=false`.
9. **`gst_parse_launch`**: format z nawiasami (`video/x-raw(memory:D3D11Memory)`) tylko jako
   `capsfilter caps="…"`; zapis skrócony to błąd składni.
10. **Odbiornik NDI potrzebuje ok. 4 s** na znalezienie źródła; test, który czeka krócej, daje fałszywy błąd.
11. **Wynik testu to nie dowód.** Dwa razy „test przeszedł”, bo przez błąd parametru badał coś innego niż miał.
    Gdy wynik jest podejrzanie dobry albo sprzeczny z innym — zrób ślad (jasność klatek co sekundę, znaczniki czasu).
12. **Szyna GStreamera**: handler synchroniczny zwraca `GST_BUS_DROP` i przekazuje wynik do wątku okna przez
    `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`; nigdzie nie wolno używać `gst_bus_pop` na tej szynie.

## 8. Testy (PowerShell, wymagają `ffprobe`)

| Skrypt | Co sprawdza |
|---|---|
| `tests\rec_test.ps1` | nagrywanie: `-Channels -Seconds -Cycles`, split (`-SplitEvery`, `-AutoSplit`), `-Container ts`, pauza (`-PauseAt -PauseFor -PauseCount`), przełączanie wyjścia (`-LiveCycle`), źródła (`-Source screen`, `-Audio loopback`) |
| `tests\sync_test.ps1` | wspólny start / pauza / stop grupy, domyślnie 16 kanałów |
| `tests\live_test.ps1` | odbiór wyjścia SRT (ffprobe) i NDI w trakcie nagrania |
| `tests\net_test.ps1` | wejście SRT / RTSP z zanikiem i powrotem nadajnika (`build\rtsp_test_server.exe`) |
| `tests\list_test.ps1` | lista kanałów: zapis → wczytanie → identyczny plik; autozapis, przywracanie, ostatnia lista |

Każdy skrypt przyjmuje `-Exe` (test paczki). Przed wydaniem uruchom przynajmniej: `rec_test` z pauzą i splitem,
`sync_test`, `list_test`, a przy zmianach w torze sygnału także `net_test` i `live_test`.

Program ma argumenty do testów i zrzutów (pełna lista w komentarzu na początku `src/main.cpp`), m.in.
`--demo N`, `--rec-test S`, `--screenshot plik`, `--shot-delay ms`, `--view list`, `--tab N`, `--lang xx`,
`--input screen|camera|srt|rtsp`, `--live ndi|srt`, `--list plik`, `--empty`.

## 9. Układ kodu

```
src/main.cpp            start, argumenty testowe, paczka samodzielna (wtyczki GStreamera obok EXE)
src/core/Channel        jeden kanał: źródła, podgląd, gałąź nagrywania, pauza, split, wyjście na żywo, zanik sygnału
src/core/ChannelManager lista kanałów, operacje grupowe, sesja i listy, autozapis, pilnowanie dysków
src/core/Lang           tłumaczenia (T("…")) i stałe Roadmap
src/core/DiskMonitor, DiskTest, EnvCheck
src/media/EncoderFactory  wybór enkodera próbą;  src/media/Devices  ekrany, kamery, urządzenia dźwięku
src/util/FileNameTemplate szablon nazw plików
src/app/MainWindow      nagłówek, panel sterowania, pasek stanu, przełączanie widoków
src/app/ChannelGrid + ChannelTile   widok kafli (malowany QPainterem, przeciąganie)
src/app/ChannelList     widok listy (nagłówek z przyciskami grupowymi, wiersze)
src/app/Inspector       zakładki Input / Output;  PreviewDialog  duży podgląd;  DisksDialog;  Widgets;  Theme
tests/                  skrypty testowe; tests/tools/rtsp_test_server.cpp
scripts/                build.ps1, run.ps1, package.ps1
```
