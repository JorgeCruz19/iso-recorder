# ISO Recorder — instrukcja końcowa

> Ten dokument **nadpisuje** `instrukcja.md` wszędzie tam, gdzie się różnią. Czego tu nie ma, obowiązuje w brzmieniu z `instrukcja.md`.
> Powstał z przeglądu: `instrukcja.md`, `rozmowa.md` (uwagi innego modelu), `mockup/interfejs.html` + `interfejs.png` oraz sprawdzenia maszyny, na której program powstaje.
> Data: 2026-11-09.

---

## 1. Decyzje wiążące (skrót)

| # | Decyzja |
|---|---------|
| D1 | Stos bez zmian: **C++20 + Qt 6 Widgets + GStreamer (MSVC) + CMake/Ninja**. |
| D2 | **Mockup jest specyfikacją interfejsu.** Rozdz. 6.1–6.2 `instrukcja.md` (szkic ASCII, zakładki w karcie kanału, pasek menu) przestają obowiązywać — patrz rozdz. 5 niżej. |
| D3 | Limit **16 kanałów od pierwszej wersji** (nie 4, jak proponuje rozmowa). Jeden pipeline na kanał sprawia, że limit jest stałą, nie kosztem. Ogranicza się liczbę *typów wejść* w kolejnych wersjach, nie liczbę kanałów. |
| D4 | **Pauza**: w trybie z enkoderem — prawdziwa pauza przez przepisywanie PTS przed enkoderem (rozdz. 3.1). W trybie passthrough — pauza = zamknięcie części, wznowienie = nowa część. |
| D5 | **WHIP/WHEP zostają w zakresie**, ale jako ostatni typ wejścia (ostatnia wersja planu) i tylko gdy wtyczka się ładuje. MediaMTX jako brama jest udokumentowaną alternatywą, nie zamiennikiem. |
| D6 | **Typy wejść i kodeki są bramkowane dostępnością**: przy starcie aplikacja sprawdza, które elementy GStreamera faktycznie działają; niedostępne pozycje są w UI wyszarzone z powodem („brak wtyczki ndi”, „brak enkodera HW”). |
| D7 | Wersjonowanie: **0.1, 0.2, … co 0.1** — każda wersja to commit + tag `v0.N` + push do prywatnego repo `studiokrause/iso-recorder`. |
| D8 | Testy plików (`ffprobe`) od pierwszej wersji z nagrywaniem, nie na końcu. |
| D9 | Interfejs w jednym języku naraz: domyślnie angielski, do wyboru PL, DE, ES, IT, FR (5.8). |

---

## 2. Ocena uwag z `rozmowa.md`

| Uwaga innego modelu | Werdykt | Uzasadnienie |
|---|---|---|
| 1. Pauza przez `gst_pad_set_offset` jest ryzykowna; MVP bez pauzy, potem „pauza = nowa część” | **Częściowo słuszna** | Diagnoza trafna (kod z 9.5 jest błędny — patrz 3.1), ale wniosek za daleko idący. Przed enkoderem klatki są surowe i przepisanie PTS jest proste i pewne. „Pauza = nowa część” zostaje tylko dla passthrough. |
| 2. `whipserversrc` młody, lepiej MediaMTX | **Częściowo słuszna** | Ryzyko realne, ale mockup pokazuje kanał WHIP, a brama to dodatkowy proces do wdrożenia i pilnowania. Zostaje natywnie, na końcu, za bramką dostępności (D5, D6). |
| 3. ±1 klatka między kanałami realna tylko dla `testsrc` | **Słuszna** | Wspólny zegar wyrównuje *moment rozpoczęcia zapisu*, nie opóźnienie źródeł (SRT 200 ms, NDI ~1–2 klatki, HDMI ~0). Kryterium przepisane w 3.4. |
| 4. Lista nazw enkoderów nie wystarczy, `probeOpen()` nie wykryje problemów z pamięcią D3D11 | **Słuszna** | Zastąpione próbą rzeczywistego mini‑pipeline'u (3.3). |
| 5. Sprawdzian `gst-inspect` przed M1 | **Słuszna** | Wbudowany w aplikację jako tryb `--check-env` (3.6) — działa też u klienta. |
| MVP = 4 kanały | **Odrzucona** | Patrz D3. |
| Chipy na kaflu wymagają `paintEvent`, nie QSS | **Słuszna** | Kafel jest jednym widgetem malowanym w całości `QPainter` (5.2). |
| `git init` + `.gitignore` jako pierwszy krok | **Słuszna** | Plus prywatne repo na GitHub. Pliki `*.isorec.json` **nie** są ignorowane globalnie (przykładowa lista w `examples/` ma być w repo) — ignorowany jest tylko katalog nagrań i build. |
| Harmonogram 5–6 tyg. na MVP | Bez znaczenia | Zastąpiony planem wersji (rozdz. 6). |

---

## 3. Poprawki merytoryczne do `instrukcja.md`

### 3.1 Pauza (zastępuje fragment 9.5 „Pauza”)

Błędy w oryginale:

- Kod liczy przerwę **osobno na każdym padzie** z `pts` bufora, a tekst pod spodem wymaga wspólnej wartości. Audio i wideo dostaną różne przesunięcia (bufory audio mają inną ziarnistość) → dryf A/V po każdej pauzie.
- `gst_pad_set_offset()` wołane z wnętrza probe na tym samym padzie zmienia *running‑time* przez ponowne wysłanie zdarzenia segmentu; enkodery sprzętowe i `splitmuxsink` reagują na to różnie. To nie jest przewidywalne.
- Pierwsza przerwa: `m_pauseStartPts` ustawia się przy pierwszym *odrzuconym* buforze danego pada, czyli o różnym czasie dla A i V.

Rozwiązanie obowiązujące (tryb z enkoderem):

1. Stan pauzy trzyma `RecordBin` w jednej strukturze pod jednym muteksem: `paused`, `pauseStartRt`, `pausedTotal` — wszystkie w **running‑time pipeline'u**.
2. `pause()` zapisuje `pauseStartRt = running_time_now`. `resume()` ustawia `resumePending`.
3. Probe **wideo** jest „zegarem prawdy”: pierwsza klatka wideo po `resume()` wyznacza `resumeRt` = jej running‑time; wtedy `pausedTotal += resumeRt - pauseStartRt`. Granice pauzy leżą więc zawsze na granicy klatki.
4. Oba probe (A i V): bufor z running‑time w oknie pauzy → `DROP`; pozostałe → `gst_buffer_make_writable`, `PTS -= pausedTotal`, `DTS -= pausedTotal` (jeśli ważne). Pierwszy bufor po wznowieniu dostaje `GST_BUFFER_FLAG_DISCONT`.
5. Audio: bufor przecinający granicę pauzy jest odrzucany w całości (błąd ≤ 1 bufor ≈ 10 ms, bez kumulacji, bo `pausedTotal` jest wspólne).
6. Po wznowieniu `force-key-unit` do enkodera.
7. Kanał bez wideo (samo audio): zegarem prawdy jest probe audio.

Tryb passthrough: Pause = `split` + wstrzymanie zapisu do następnej klatki kluczowej po Resume; plik po wznowieniu to nowa część (`{part}` rośnie). UI pokazuje to wprost w podpowiedzi przycisku.

Test akceptacyjny (od wersji z pauzą): Rec 10 s → Pause 5 s → Resume → 10 s → Stop; `ffprobe`: czas ≈ 20 s (±2 klatki), PTS wideo ściśle rosnące bez luki > 1,5 klatki, różnica początku/końca A i V < 40 ms.

Stan po wersji 1.0 (tryb z enkoderem) — zaimplementowane w sondach na wejściu gałęzi nagrywania (`Channel::recBuffer`):

- Jedna struktura stanu pod jednym muteksem, czasy w running‑time; znacznik w pliku = running‑time − początek nagrania − suma pauz, ta sama suma dla obrazu i dźwięku.
- Obie granice pauzy wyznacza klatka wideo: pierwsza klatka po kliknięciu Pause jest pierwszą odrzuconą, a pierwsza klatka po kliknięciu Resume trafia dokładnie w jej miejsce — znaczniki obrazu są ciągłe co do klatki.
- Dźwięk jest przycinany zachowawczo (bufor wątpliwy jest odrzucany), więc w miejscu cięcia może powstać luka do ok. 60 ms, ale znaczniki nigdy się nie nakładają; pierwszy bufor po wznowieniu ma `DISCONT`.
- Po wznowieniu do enkodera idzie `force-key-unit`. Stop działa także w pauzie. Pauzy są w pliku opisu (`pauses`: `atMs`, `durationMs`).
- Wyniki (Quick Sync, 720p25): 1–3 pauzy, MKV i MPEG‑TS, 16 kanałów, z auto‑splitem — czas materiału zgodny, brak przerw w znacznikach obrazu (< 61 ms), koniec obrazu i dźwięku w odległości < 60 ms, tak samo jak bez pauzy.
- Nie zrobione: pauza w trybie passthrough (nowa część pliku) — dojdzie razem z passthrough; wspólny moment pauzy wielu kanałów — razem ze startem grupowym.

### 3.1a Nagrywanie — stan po wersji 0.5

- Gałąź zapisu (`queue → konwersja → enkoder → h264parse` oraz `queue → audioconvert → avenc_aac → aacparse` → `splitmuxsink` z `matroskamux`) jest dopinana do działającego pipeline'u przy Rec i odpinana po Stop; podgląd nie przerywa.
- `splitmuxsink`: muxer podawany właściwością `muxer` **przed** zażądaniem padów (`muxer-factory` działa tylko przy `async-finalize=true`), `async-finalize=false`, `start-index=1`.
- Znaczniki czasu: probe na wejściu obu kolejek odejmuje wspólną bazę (running‑time w chwili Rec) i odrzuca bufory sprzed niej — plik zaczyna się od zera, obraz i dźwięk mają tę samą bazę. To ten sam mechanizm, na którym oprze się pauza (3.1) i start grupowy (3.4). Założenie tej wersji: PTS = running‑time (prawda dla źródeł testowych; dla prawdziwych wejść dojdzie przeliczenie przez segment).
- Stop: blokada wyjść `tee` → odpięcie → EOS do gałęzi → oczekiwanie na EOS z gałęzi przekazany przez `message-forward` pipeline'u (`GstBinForwarded`) → dopiero wtedy zdjęcie gałęzi. Limit czasu 10 s, po nim wymuszone zamknięcie i komunikat.
- Błąd wewnątrz gałęzi zapisu kończy tylko nagranie; podgląd kanału działa dalej.
- Zamknięcie okna w trakcie nagrywania najpierw finalizuje wszystkie pliki.
- Enkoder: próba mini‑pipeline'u na rozdzielczość i klatkaż, wynik w pamięci podręcznej; kolejność NVENC → Quick Sync → AMF → Media Foundation → OpenH264. GOP = 1 s.
- Do wersji 1.3 Rec dla wielu kanałów startował je po kolei (ok. 60 ms na kanał). **Od 1.4** grupa ma wspólną chwilę `T`: wszystkie pipeline'y używają zegara systemowego GStreamera (`gst_pipeline_use_clock`), więc jedna wartość zegara oznacza tę samą chwilę w każdym kanale (running‑time = zegar − `base_time` kanału). Start: `startRecording` dopina gałęzie (baza = nieskończoność, nic nie przechodzi), potem `beginRecordingAt(T)` z `T` = teraz + 80 ms ustawia bazę we wszystkich kanałach. Pauza, wznowienie i Stop dostają jedno `T` = teraz; Stop odrzuca w sondach bufory od `T` i dopiero potem zamyka gałąź. Plik opisu ma `startClockNs`, `firstFrameClockNs`, `lastFrameClockNs`. Wynik na 16 kanałach 720p25: pierwsze klatki 1–39 ms po `T`, ostatnie klatki w rozrzucie 33–35 ms, liczba klatek 247–248, położenie pauzy w rozrzucie 33 ms.
- Wyniki na maszynie deweloperskiej (Quick Sync, 720p25, 8 Mb/s): 16 kanałów równocześnie bez zgubionych klatek; 50 cykli Rec/Stop na 2 kanałach — 100 plików poprawnych, liczba uchwytów i pamięć bez wzrostu. Test powtarzalny: `tests\rec_test.ps1`.

### 3.1b Split, części i plik opisu — stan po wersji 0.6

- **Split ręczny** (SPLIT na kaflu, dla zaznaczonych, skrót `X`): `force-key-unit` do enkodera + sygnał `split-now`; cięcie na najbliższej klatce kluczowej, bez utraty klatek (test: suma klatek części = czas nagrania × klatkaż).
- **Auto-split** (zakładka Output): co 1–60 min (`max-size-time` + `send-keyframe-requests`) albo co 1/2/4 GB (`max-size-bytes`).
- **Części**: `{part}` rośnie od 001; numer bieżącej części widać na kaflu obok czasu. Każda część MKV zaczyna się od zera (`matroskamux offset-to-zero`), w MPEG‑TS znaczniki są ciągłe między częściami.
- **Kontenery**: MKV (domyślny) i MPEG‑TS (`mpegtsmux`). MOV/MP4 w etapie z kodekami.
- **Plik opisu** `<nazwa bez numeru części>.json` (schemat `isorec.recording`), zapisywany po zakończeniu nagrania: sesja, kanał, źródło, format, enkoder, bitrate, czas startu i końca w UTC, running‑time pipeline'u w chwili startu, lista części z `startMs`/`endMs` na osi nagrania i rozmiarem, znacznik `clean` i powód zatrzymania. Służy do synchronizacji w montażu i jest podstawą testu `tests\rec_test.ps1`.
- **Dysk nie nadąża**: gdy kolejka zapisu kanału jest zapełniona ponad połowę przez 5 s, program wypisuje ostrzeżenie (raz na nagranie).

### 3.2 Stop, split i `splitmuxsink`

- **`async-finalize=false`** (oryginał w 5.4 podaje `true`). Przy `true` plik zamyka się w tle i komunikat EOS nie oznacza, że plik jest gotowy; dla MKV/TS finalizacja i tak trwa milisekundy. `true` rozważyć dopiero dla MP4 z dużym `moov`.
- `split-at-running-time` to **sygnał akcji**, nie właściwość (oryginał 9.6 sugeruje właściwość).
- Mostek busa z 9.3 zwraca `GST_BUS_DROP`, więc **nic w aplikacji nie może używać `gst_bus_pop`/`gst_bus_timed_pop`** — oczekiwanie na finalizację opiera się wyłącznie na komunikacie przekazanym do Qt + `QTimer` z timeoutem.
- Mostek musi trzymać `QPointer<Channel>`/obiekt kontekstu; sam wskaźnik z oryginału wisi po usunięciu kanału, gdy komunikaty są jeszcze w kolejce.
- `fMP4` (`isofmp4mux`) wewnątrz `splitmuxsink` — status „do sprawdzenia”, nie obietnica. Kontenery gwarantowane: **MKV, MPEG‑TS**; MOV/MP4 przez `qtmux` w trybie robust (`muxer-properties`).
- Kanał bez audio: `RecordBin` budowany tylko z gałęzią wideo (oryginał zakłada zawsze dwa pady).

### 3.3 Enkodery (zastępuje 9.7)

- `probeOpen()` (READY/PAUSED na samym elemencie) nie wykrywa błędów negocjacji pamięci ani limitu sesji. Zastępuje go **próba mini‑pipeline'u**: `videotestsrc num-buffers=8 ! <caps docelowe> ! [d3d11upload] ! <enkoder z ustawieniami> ! fakesink`, synchronicznie, z timeoutem 3 s; wynik w pamięci podręcznej na czas sesji, klucz: enkoder × rozdzielczość × fps × głębia bitowa.
- Próba jest powtarzana **przy każdym Rec** dla enkoderów z limitem sesji (NVENC); przy porażce — następny z listy i ostrzeżenie w pasku stanu.
- Limit sesji NVENC zależy od wersji sterownika — **nie wpisywać liczby na sztywno**. Wskaźnik w nagłówku z mockupu („NVENC 12/∞”) zmienia się na **„ENK. HW n”** (liczba aktywnych sesji, bez mianownika).
- Kolejność „Auto (HW)”: NVENC → QSV → AMF → MF → OpenH264 (bez zmian), ale ustalana z wyników prób, nie z samej obecności fabryki.

### 3.4 Synchronizacja (zastępuje kryterium z 9.6 i rozdz. 11)

- Gwarantowane: wszystkie kanały z jednej komendy grupowej zaczynają zapis od pierwszej klatki o running‑time ≥ wspólnego `T` (wspólny zegar i `base-time`). To daje **±1 klatkę w czasie dotarcia klatki do aplikacji**.
- Niegwarantowane: zgodność treści między kanałami — różne źródła mają różne opóźnienie. Do montażu służy sidecar: czas UTC i running‑time pierwszej klatki każdej części + timecode RTC.
- Kryterium akceptacji „±1 klatka między kanałami” obowiązuje dla `testsrc` i wejść lokalnych tego samego typu; dla sieciowych — „różnica startu ≤ 1 klatka + zadeklarowane opóźnienie źródła”.

### 3.5 Podgląd (korekta 9.4)

- `videorate drop-only=true max-rate=15` ma stać **przed** skalowaniem (oryginał skaluje wszystkie 50 klatek/s, potem wyrzuca 70%).
- `d3d11scale` i `d3d11convert` to dziś ten sam element — wystarczy `d3d11convert`.
- Źródła programowe (`videotestsrc`, dekodery SW) potrzebują `d3d11upload`; budowniczy wstawia go warunkowo. Wariant bez GPU (`videoconvertscale`) musi istnieć jako zapas — maszyna wirtualna i sesja RDP nie mają D3D11 z enkoderem.
- Rozmiar podglądu zależy od widoku: 2×2 → 640×360, 3×3/4×4 → 480×270; inspektor korzysta z tej samej klatki co kafel aktywnego kanału (bez drugiej gałęzi).

### 3.6 Środowisko i narzędzia (korekty rozdz. 2 i 4)

- **Qt „LTS” nie dotyczy licencji LGPL** — poprawki LTS po pierwszych miesiącach dostają tylko klienci komercyjni. Zapis „Qt 6.8 LTS” zmienia się na: **Qt ≥ 6.8, wydanie open‑source, wersja przypięta w `CMakePresets.json`**. Instalacja przez `aqtinstall` (bez konta Qt), nie przez Online Installer.
- **MSVC**: „v143 / VS 2022” → **v143 lub nowszy**. Na maszynie deweloperskiej jest VS Build Tools 18 (MSVC 14.51) z własnym CMake i Ninja; binaria Qt `msvc2022_64` są z nim zgodne ABI.
- **GStreamer**: „1.26.x” → **bieżąca stabilna seria MSVC x64, wersja przypięta**; `whepsrc` z `webrtchttp` jest przestarzały — używać `whepclientsrc`.
- `main.cpp`: katalog aplikacji z `GetModuleFileNameW`, nie z `argv[0]` (zawodzi przy uruchomieniu przez skrót/PATH).
- Tryb pracy z GStreamerem: w buildzie deweloperskim z instalacji systemowej (`GSTREAMER_1_0_ROOT_MSVC_X86_64`), w paczce — z katalogu aplikacji. Przełącza to obecność `gstreamer-1.0\` obok EXE.
- **`IsoRecorder.exe --check-env`**: wypisuje wersję GStreamera i tabelę wymaganych elementów (jest/brak) oraz wyniki prób enkoderów; zwraca kod ≠ 0, gdy brakuje elementów obowiązkowych. Ten sam raport jest w aplikacji (menu ☰ → Diagnostyka).
- Wersja w CMake: `project(IsoRecorder VERSION 0.1.0)`; `createdWith` w JSON bierze ją z kompilacji (oryginał ma wpisane „1.0.0”).

### 3.7 Maszyna deweloperska — stan na 2026‑11‑09

| Element | Stan |
|---|---|
| System | Windows 10 IoT LTSC 2021 (19044) — WGC działa, żółtej ramki nie da się wyłączyć → dla monitorów domyślnie DXGI |
| CPU / RAM | i5‑8400T (6 rdzeni), 32 GB |
| GPU | Intel UHD 630 → **tylko QSV** (brak NVENC/AMF) |
| MSVC, CMake, Ninja, Windows SDK | są (VS Build Tools 18) |
| Qt | 6.10.3 `msvc2022_64` w `C:\Qt` (zainstalowane 2026‑11‑10 przez `aqtinstall`) |
| GStreamer | 1.28.8 MSVC x64 w `%LOCALAPPDATA%\Programs\gstreamer\1.0\msvc_x86_64` (instalacja dla użytkownika, **bez** zmiennej `GSTREAMER_1_0_ROOT_MSVC_X86_64` — skrypty szukają tej ścieżki same) |
| Wtyczki | `--check-env`: komplet poza `nvh264enc` i `amfh264enc` (brak takiego GPU); są m.in. `qsvh264enc`, `ndisrc`, `srtsrc`, `whepclientsrc`, `whipserversrc`, `fallbacksrc` |
| NDI Runtime 6 | jest |
| ffmpeg/ffprobe | jest (`D:\VideoTools\ffmpeg`) |
| Karty HDMI / kamery | nie wykryto |

Wnioski: profil „16 × 1080p50 z enkodowaniem” jest na tej maszynie nieosiągalny (realnie ~3–4 kanały 1080p przez QSV; więcej tylko w passthrough). Testy 16 kanałów prowadzić na `videotestsrc` w 720p25 albo w passthrough. Wejścia DeckLink/UVC można tu zaimplementować, ale **nie da się ich tu zweryfikować** — w notatkach wersji będą oznaczane jako „niesprawdzone na sprzęcie”.

### 3.8 Drobne

- Repo: w `.gitignore` — `build/`, `dist/`, `out/`, `*.user`, katalog nagrań; `media/` i `mockup/` zostają w repo (mockup bez nich nie działa).
- `SetDefaultDllDirectories(APPLICATION_DIR | SYSTEM32)` z 12.21 blokuje ładowanie NDI Runtime z jego własnego katalogu → używać `LOAD_LIBRARY_SEARCH_DEFAULT_DIRS` + `AddDllDirectory` dla katalogu aplikacji.
- Autosave „przy każdej zmianie” → z opóźnieniem 2 s (debounce), inaczej przeciąganie suwaka zapisuje plik kilkadziesiąt razy na sekundę.
- Stop grupowy z potwierdzeniem (6.3) — potwierdzenie tylko gdy zatrzymywane są ≥ 2 nagrywające kanały; pojedynczy Stop na kaflu działa od razu.

---

## 4. Zakres funkcji — bez zmian względem F1–F8

Wszystkie funkcje z rozdz. 1.1 `instrukcja.md` pozostają w zakresie. Zmienia się kolejność dostarczania (rozdz. 6).

---

## 5. Interfejs — według mockupu

### 5.1 Układ okna

```
┌ natywny pasek tytułu Windows: „ISO Recorder — <sesja>” ───────────────────────────────┐
├ NAGŁÓWEK 72 px: Session │ Recording time + REC │ Timecode RTC │ Disk │ statystyki │ ☰ │ View
├───────────────────────────────────────────────┬───────────────────────────────────────┤
│ BLOK WIDEO                                    │ INSPEKTOR                             │
│  siatka 4×4 kafli 16:9                        │  podgląd 16:9, mierniki L/R           │
│  równe marginesy ze wszystkich stron          │  zakładki Input / Output / Scheduler  │
│  i między kaflami                             │  (treść zakładek przewijana)          │
│                                               ├───────────────────────────────────────┤
│                                               │ PANEL STEROWANIA (4 grupy w pionie)   │
│                                               │  Selected channels: REC PAUSE STOP SPLIT
│                                               │  Selection: ALL NONE INVERT DUPLICATE REMOVE (małe)
│                                               │  Active channel: MOVE UP · MOVE DOWN  │
│                                               │  Channel list (n / 16): ADD · LOAD · SAVE
├───────────────────────────────────────────────┴───────────────────────────────────────┤
└ PASEK STANU 22 px: Autosave · ostatnie zdarzenia · komunikat · legenda stanów (REC / PAUSED / PREVIEW / WAITING)
```

**Proporcje bloku wideo.** Blok wideo zajmuje całą wysokość między nagłówkiem a paskiem stanu. Jego szerokość jest **wyliczana z wysokości** tak, żeby 4 × 4 kafle (kadr 16:9 + pasek 26 px + margines 4 px) wypełniły go dokładnie, z równymi marginesami ze wszystkich stron i między kaflami:

```
wysokość kafla  th = (H − 2·4) / 4
szerokość kadru     = (th − 26 − 2·4) · 16/9
szerokość kafla tw = szerokość kadru + 2·4
szerokość bloku     = 4·tw + 2·4
prawa kolumna       = szerokość okna − szerokość bloku     (ograniczona do 384…560 px)
```

Przy 1600×900 i 1920×1080 prawa kolumna ma ok. 384–388 px. Gdy okno jest węższe lub szersze, niż pozwala ograniczenie, siatka jest środkowana w bloku. Widoki 2×2 i 3×3 korzystają z tego samego bloku (siatka środkowana).

Odstępstwa od mockupu (świadome):

| Mockup | Program | Powód |
|---|---|---|
| Stały rozmiar 1920×1080 | Układ elastyczny, minimum 1280×720 | Program musi działać na innych monitorach i przy skalowaniu DPI |
| Panel sterowania jako pasek 86 px na całą szerokość pod siatką i inspektorem; inspektor 500 px | Panel sterowania **w prawej kolumnie pod inspektorem**; szerokość prawej kolumny wynika z proporcji bloku wideo (patrz wyżej) | Decyzja właściciela: blok wideo ma mieć kształt dokładnie pod 16 kafli 16:9 z równymi marginesami |
| Własny pasek tytułu | Natywny pasek Windows (ciemny przez `DWMWA_USE_IMMERSIVE_DARK_MODE`) | Okno bez ramki traci przyciąganie, cień i obsługę wielu monitorów |
| Brak menu | Przycisk **☰** w nagłówku: Nowa lista, Wczytaj, Zapisz, Zapisz jako, Ostatnie, Ustawienia, Diagnostyka, O programie | Mockup nie ma miejsca na funkcje z F8 i ustawienia |
| „NVENC 12/∞” | „ENK. HW n” | Patrz 3.3 |
| Czcionka Open Sans z Google Fonts | Open Sans dołączona do zasobów (licencja OFL), zapas: Segoe UI | Program nie może zależeć od sieci |
| Grupa „Wybór”: SELECT ALL + ADD SOURCE; grupa „Lista kanałów”: W GÓRĘ, W DÓŁ, WCZYTAJ, ZAPISZ | Cztery grupy: **Selected channels** (transport), **Selection** (SELECT ALL / SELECT NONE / INVERT), **Active channel** (MOVE UP / MOVE DOWN), **Channel list** (ADD SOURCE / LOAD / SAVE) | Decyzja właściciela: dodawanie źródła nie jest wyborem, a przesuwanie dotyczy jednego kanału, nie listy |
| Etykiety mieszane PL/EN | Jeden język naraz: domyślnie **angielski**, do wyboru PL, DE, ES, IT, FR (☰ → Language) | Decyzja właściciela; patrz 5.8 |
| Widok „Lista” niezaprojektowany | Tabela: nr, nazwa, stan, źródło, format, czas, rozmiar, VU poziomy, przyciski | — |

Kolory, wymiary i typografia — dokładnie z bloku `:root` i reguł CSS mockupu (jedno źródło prawdy: `src/app/Theme.h`).

### 5.2 Kafel kanału (`ChannelTile`)

Jeden widget, całość malowana w `paintEvent` (`QPainter`), bez widgetów potomnych — 16 kafli × kilkanaście kontrolek byłoby zbyt kosztowne w odświeżaniu 15 Hz.

- **Kadr zawsze 16:9** — w kaflu i w inspektorze; rozmiar kafla wynika z ciaśniejszego wymiaru siatki, wolne miejsce zostaje tłem. Źródło o innych proporcjach (4:3, 9:16, 2.40:1, 1:1…) jest **wpisane w kadr w całości**, z czarnymi pasami; nic nie jest obcinane (mockup używał `object-fit: cover` — odrzucone). Pasy dopisuje `videoconvertscale add-borders=true` przy stałych `caps` podglądu 16:9 z kwadratowym pikselem, więc działa to dla każdego wejścia, także anamorficznego.
- **Ramka stanu** (wewnątrz kadru): 3 px czerwona (REC), bursztynowa (PAUZA), zielona (PREVIEW), 2 px szara #3c3c3c (bezczynny/czeka).
- **Kanał aktywny** (jeden; pokazany w inspektorze, przesuwany przez MOVE UP/DOWN): pomarańczowa ramka 3 px **wokół całego kafla** i pomarańczowa etykieta z ciemnym tekstem. Biała linia 1 px z mockupu była nieczytelna na tle ramki stanu — odrzucona. Pomarańcz to ten sam akcent co aktywna zakładka inspektora.
- **Kanały zaznaczone** (dowolnie wiele; cel przycisków Selected channels): białe, pełne pole wyboru 18 px z czarnym znakiem i jaśniejszy pasek pod kadrem.
- **Nakładki**: pole zaznaczenia (lewy górny), format `1080p50` (dla proporcji innych niż 16:9: `1024×768p25`), czas nagrania z kropką stanu (prawy górny; przy zaplanowanym starcie „START 11:00”), miernik audio pionowy 2 słupki (prawa krawędź), etykieta `NN NAZWA` (dół, środek), komunikat pełnoekranowy (`RECONNECTING`, `NO SIGNAL`, `BŁĄD`) z przyciemnieniem i odbarwieniem obrazu.
- **Pasek** 26 px: `REC` `II` `STOP` `SPLIT` (podświetlenie i wyszarzenie wg stanu), opis źródła, uchwyt `⋮⋮`, `✕`.
- **Mysz**: klik w obraz = kanał aktywny (inspektor); klik w pole zaznaczenia = zaznaczenie; Ctrl+klik = przełącz zaznaczenie; Shift+klik = zakres; dwuklik w etykietę = zmiana nazwy; przeciąganie kafla (za obraz lub pasek, poza przyciskami) = zmiana kolejności — półprzezroczysta kopia idzie za kursorem, pomarańczowa kreska pokazuje miejsce wstawienia, upuszczenie poza siatką anuluje; dwuklik w obraz = okno dużego podglądu (ok. 80% ekranu: obraz, nazwa, stan, czas nagrania, przyciski REC / PAUSE / STOP / SPLIT, mierniki audio; zamyka je Esc lub dwuklik).
- **Klawiatura** (kanał aktywny): `R` Rec, `P` Pause/Resume, `S` Stop, `X` Split, `Del` usuń, `Ctrl+↑/↓` przesuń, strzałki = zmiana aktywnego; `Ctrl+A` zaznacz wszystkie; z `Shift` — to samo dla zaznaczonych (`Shift+R` itd.).

### 5.3 Inspektor

Zakładki Input / Output / Scheduler dotyczą **kanału aktywnego** (nie zaznaczonych). Sekcje jak w mockupie; pod zakładką Input zwinięte streszczenia Output i Scheduler (klik = przejście do zakładki). Pomarańczowa kropka w nagłówku sekcji = sekcja ma wartości inne niż domyślne. Podczas nagrywania pola Input/Output są zablokowane; Scheduler pozostaje edytowalny.

### 5.4 Panel sterowania i pasek stanu

- Przyciski grupowe działają na **zaznaczonych**; gdy nic nie jest zaznaczone — nieaktywne. `REC` świeci, gdy nagrywa co najmniej jeden zaznaczony.
- Skróty zaznaczenia: `Ctrl+A` wszystkie, `Ctrl+Shift+A` żaden, `Ctrl+I` odwróć.
- `MOVE UP` / `MOVE DOWN` przesuwają kanał aktywny i dlatego stoją w osobnej grupie „Active channel”.
- Licznik `n / 16` stoi w wierszu tytułu grupy Channel list. Legenda stanów (REC, PAUSED, PREVIEW, WAITING, ERROR tylko gdy > 0) jest w pasku stanu, po prawej — razem z autozapisem i komunikatami.
- **Zarządzanie zaznaczonymi** odbywa się w wierszu Selection: `ALL`, `NONE`, `INVERT` zmieniają zaznaczenie, `DUPLICATE` i `REMOVE` działają na zaznaczonych. Przyciski tego wiersza są mniejsze (26 px) niż transport.
  - `REMOVE` zawsze pyta o potwierdzenie przy wielu kanałach; kanał nagrywający jest najpierw zatrzymywany i finalizowany, dopiero potem usuwany.
  - `DUPLICATE` wstawia kopię (z ustawieniami Input i Output) zaraz za oryginałem, do limitu 16.
  - To samo jest w menu pod prawym przyciskiem na kaflu: na kaflu zaznaczonym działa na wszystkie zaznaczone, na niezaznaczonym — na ten jeden.
  - Skróty: `Shift+Del` usuń zaznaczone, `Ctrl+D` duplikuj zaznaczone, `Del` usuń kanał aktywny, `R` / `S` Rec / Stop kanału aktywnego.
  - Kopiowanie ustawień zapisu na wiele kanałów: przycisk „Apply to selected channels” w zakładce Output (kopiuje Output kanału aktywnego na zaznaczone).
- Stop dla zaznaczonych pyta o potwierdzenie, gdy zatrzymuje co najmniej 2 nagrania.
- Przyciski w wierszu dzielą szerokość kolumny po równo (nie węziej niż napis).
- Koszt układu: przy wysokości 900 px na treść zakładek inspektora zostaje ok. 150 px, dlatego jest przewijana; przy 1080 px — ok. 330 px.
- Pasek stanu: czas ostatniego autozapisu, dwa ostatnie zdarzenia z dziennika, komunikat kontekstowy po prawej.

### 5.5 Sesja i listy kanałów

- **Sesja** = nazwa bieżącego zestawu nagrań. Trafia do nazw plików i folderów przez token `{session}` oraz jest nazwą pliku listy. „New session” oznacza listę jeszcze niezapisaną.
- **Lista kanałów** = plik `<sesja>.isorec.json` (rozdz. 8 `instrukcja.md`): wszystkie kanały z ustawieniami Input, Output i Scheduler, kolejność, nazwa sesji, foldery docelowe. Nie zawiera haseł.
- **Interfejs** (wersja z listami — patrz rozdz. 6):
  - nazwa sesji w nagłówku jest edytowalna (klik → pole tekstowe); gwiazdka przy nazwie = niezapisane zmiany;
  - grupa **Channel list**: `LOAD` (okno wyboru pliku), `SAVE` (zapis do bieżącego pliku; przy nowej sesji pyta o miejsce);
  - menu ☰: New list, Load list…, Save list, Save as…, Recent (ostatnie 8 plików);
  - autozapis do `%LOCALAPPDATA%\IsoRecorder\autosave.json` i propozycja przywrócenia po awarii;
  - przy starcie program otwiera ostatnio używaną listę (opcja w ustawieniach).
- Wczytanie listy przy trwających nagraniach wymaga potwierdzenia i najpierw je finalizuje.
- Stan po 1.2: zaimplementowane jak wyżej. Schemat pliku: `isorec.list`, wersja 1 — `session`, `channels[]` z `name`, `selected`, `input` (generator testowy: wzór, format, dźwięk), `output` (`dir`, `nameTemplate`, `container`, `video`, `audio`, `autoSplit`), `live` (`type`, `ndiName`, `srtAddress`, `bitrateKbps`). Autozapis usuwa się przy poprawnym wyjściu, więc jego obecność przy starcie oznacza awarię. Opcja „otwieraj ostatnią listę” jest w menu ☰ (osobnego okna ustawień jeszcze nie ma). Harmonogram dojdzie do pliku razem ze Schedulerem.

### 5.6 Zapis na kilku dyskach

- **Folder docelowy jest ustawieniem kanału** (zakładka Output), z jednym folderem domyślnym sesji. Kanały można więc rozłożyć na dowolną liczbę dysków — przy 16 kanałach to zwykle konieczność.
- Program grupuje kanały według wolumenu i liczy dla każdego osobno: wolne miejsce, sumę bitrate'ów kanałów, czas do zapełnienia.
- **Nagłówek**: komórka „Disk” pokazuje wolumen z **najkrótszym** czasem do zapełnienia i liczbę używanych wolumenów („DISK D: · 3 DISKS”); podpowiedź wymienia wszystkie, klik otwiera okno **Disks** (też ☰ → Disks…). Mockup zakładał jeden dysk.
- **Okno Disks**: dla każdego wolumenu wolne miejsce, liczba kanałów (i ile nagrywa), potrzebny strumień, czas do zapełnienia, wynik ostatniego testu zapisu i przyciski testu.
- Czas do zapełnienia liczy się z ustawionych bitrate'ów kanałów nagrywających; gdy nikt nie nagrywa — z bitrate'ów wszystkich kanałów wolumenu („gdyby nagrywały wszystkie”).
- Progi: ostrzeżenie poniżej 30 min lub 10% wolnego miejsca (czas w nagłówku robi się bursztynowy), kontrolowany Stop z finalizacją plików poniżej 1 GB.
- Ostrzeżenia i kontrolowany Stop przy braku miejsca działają **per wolumen** — zatrzymywane są tylko kanały z dysku, który się kończy.
- Folder zapasowy (opcja kanału): gdy wolumen podstawowy osiągnie próg krytyczny albo zniknie, następna część (split) trafia do folderu zapasowego zamiast kończyć nagranie.
- Ostrzeżenie przy wyborze folderu: FAT32 (limit 4 GB), dysk sieciowy lub USB, ten sam dysk co system.

### 5.7 Test prędkości zapisu

Potrzebny — przy wielu kanałach to dysk, nie procesor, najczęściej gubi klatki, a widać to dopiero po kilkunastu minutach.

- **Gdzie**: okno Disks (klik w komórkę Disk w nagłówku albo ☰ → Disks…), przyciski „Test 10 s” i „Test 60 s” przy każdym wolumenie; z wiersza poleceń `IsoRecorder.exe --disk-test <folder> [sekundy]`.
- **Jak**: zapis pliku tymczasowego w folderze docelowym przez 10 s (tryb szybki) lub 60 s (tryb pełny), bez bufora systemowego (`FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH`), blokami 1 MB, z tyloma równoległymi strumieniami, ile kanałów jest przypisanych do wolumenu. Plik jest usuwany po teście. Test wymaga wolnego miejsca (maks. 8 GB) i nie startuje podczas nagrywania na tym wolumenie.
- Stan po 0.6: zaimplementowane jak niżej; wynik jest pamiętany do zamknięcia programu (zapis wyników między uruchomieniami dojdzie z listami).
- **Wynik**: średnia MB/s, **najgorsza sekunda** (ważniejsza niż średnia — dyski SSD z pamięcią podręczną zwalniają po jej zapełnieniu, co wykrywa dopiero tryb pełny), najdłuższy pojedynczy zapis w ms.
- **Werdykt**: porównanie najgorszej sekundy z sumą bitrate'ów kanałów na wolumenie: zielony ≥ 3×, żółty 1,5–3×, czerwony < 1,5×. Wynik zapisany przy wolumenie z datą i pokazywany na liście dysków.
- **W trakcie nagrywania** miarą jest zapełnienie kolejek przed muxerem (rozdz. 12.30 `instrukcja.md`): > 50% przez 5 s = ostrzeżenie „disk too slow” na kaflu i w nagłówku.

### 5.8 Języki

- Interfejs występuje zawsze w jednym języku. Domyślny jest angielski; dodatkowe: polski, niemiecki, hiszpański, włoski, francuski.
- Wybór: ☰ → Language; zapamiętywany w ustawieniach użytkownika; zmiana działa od razu, bez restartu i bez przerywania kanałów (okno jest budowane od nowa, pipeline'y zostają).
- Kluczem tłumaczenia jest tekst angielski (`T("...")`, tablica w `src/core/Lang.cpp`); brak tłumaczenia = tekst angielski. Każdy nowy napis interfejsu musi dostać wiersz we wszystkich pięciu językach.
- Nie tłumaczy się terminów branżowych i nazw własnych: REC, PAUSE, STOP, SPLIT, Input, Output, Scheduler, Timecode, Autosave, CPU, GPU, Drop, nazw protokołów i urządzeń.
- Przyciski mają szerokość dopasowaną do napisu; układ musi mieścić się przy 1600 px szerokości w najdłuższym języku (niemiecki, francuski).
- Raport `--check-env` i dziennik techniczny są tylko po angielsku.

### 5.9 Wyjście na żywo (dodane po uwagach właściciela, wersja 1.1)

W `instrukcja.md` słowo „passthrough” oznacza zapis strumienia bez ponownego kodowania. Tu chodzi o coś innego: kanał ma równocześnie nagrywać się do pliku i wychodzić na zewnątrz.

- Ustawienie per kanał w zakładce Output, sekcja **Live output**: Off / NDI / SRT. Działa niezależnie od nagrywania (także w samym podglądzie); pauza i split go nie dotykają.
- **NDI**: `tee → errorignore → queue (leaky) → videoconvert → UYVY → ndisinkcombiner → ndisink`, dźwięk F32LE do `ndisinkcombiner`. Wymaga środowiska NDI na komputerze (NDI Tools / Runtime).
- **SRT**: osobny enkoder H.264 (wybór jak przy nagrywaniu) + AAC 128 kb/s → `mpegtsmux` → `srtsink` (`wait-for-connection=false`). Sam port = tryb listener, `host:port` = caller. Osobny enkoder kosztuje drugą sesję enkodera na kanał, ale strumień ma własny bitrate i nie zależy od pauzy ani splitu nagrania.
- Od wersji 1.3 gałąź wyjścia jest osobnym binem dopinanym do działającego pipeline'u (`Channel::attachLive` / `detachLive`), tak jak gałąź nagrywania: stan → żądanie padów `tee` → połączenie; odpięcie: rozłączenie → zwolnienie padów → `NULL` → usunięcie. Dzięki temu wyjście można zmieniać podczas nagrywania i pauzy. `errorignore` na wejściu gałęzi izoluje awarię w trakcie pracy, a błąd ze źródłem wewnątrz bina (rozpoznawany po przodku, z numerem podpięcia) wyłącza tylko wyjście: źródło, podgląd i nagranie działają dalej, użytkownik dostaje komunikat.
- Narzut zmierzony na maszynie deweloperskiej (8 × 720p25 nagrywanych przez Quick Sync, 6 rdzeni logicznych): samo nagrywanie 2,2 rdzenia i 537 MB; z NDI na każdym kanale 2,9 rdzenia i 632 MB (bez podłączonego odbiornika); z SRT 6 Mb/s 2,5 rdzenia i 825 MB. SRT zajmuje drugą sesję enkodera na kanał — istotne przy limicie sesji NVENC.
- **Wirtualna kamera**: Windows 10 nie ma systemowego API (MFCreateVirtualCamera jest od Windows 11). Na dziś: wyjście NDI + „Webcam” z NDI Tools. Możliwy następny krok: zapis klatek do pamięci współdzielonej wirtualnej kamery OBS (jedno urządzenie, tylko obraz, wymaga zainstalowanego OBS).
- Do zrobienia później: SRT z hasłem i `streamid`, RTMP, ponawianie połączenia w trybie caller.

---

## 6. Plan wersji

Każda wersja: działający program, commit, tag, push. Kryterium = to, co da się sprawdzić na maszynie deweloperskiej.

| Wersja | Zakres | Kryterium |
|---|---|---|
| **0.1** | Repo, CMake, `--check-env`, okno wg mockupu (nagłówek, siatka, inspektor, panel, pasek stanu), kafel malowany, kanały `testsrc` z podglądem i VU, dodawanie/usuwanie/zaznaczanie/kanał aktywny, widoki 2×2/3×3/4×4 | **wydana** |
| **0.2** | Języki (EN domyślnie + PL, DE, ES, IT, FR), nowy podział panelu sterowania, SELECT NONE i INVERT, paczka samodzielna `dist\` | **wydana** |
| **0.3** | Kadr zawsze 16:9 (inne proporcje wpisane z pasami), wyraźne oznaczenie kanału aktywnego i zaznaczonych, wybór formatu generatora testowego | **wydana** |
| **0.4** | Panel sterowania przeniesiony pod inspektor; blok wideo o proporcjach wyliczonych pod siatkę 4×4 kafli 16:9 z równymi marginesami | **wydana** |
| **0.5** | Nagrywanie: Rec/Stop → MKV (H.264 + AAC), enkoder z próbą, szablon nazw, folder per kanał, zakładka Output, finalizacja z limitem czasu, zamknięcie czeka na pliki; DUPLICATE i REMOVE dla zaznaczonych, menu kontekstowe, legenda w pasku stanu | **wydana** — `tests\rec_test.ps1` |
| **0.6** | Split ręczny i automatyczny (czas/rozmiar), numeracja części, plik opisu JSON, MPEG‑TS; monitor wolumenów, okno Disks i test prędkości zapisu (5.6, 5.7) | **wydana** — `tests\rec_test.ps1 -SplitEvery/-AutoSplit/-Container` |
| **0.7** | Zmiana kolejności kanałów przeciąganiem kafla (znacznik miejsca wstawienia, anulowanie poza siatką) | **wydana** — `--drag-test A B` |
| **0.8** | Duży podgląd kanału po dwukliku w obraz: okno ok. 80% ekranu, nazwa, stan, czas nagrania, mierniki audio; podgląd 1280×720 na czas otwarcia | **wydana** — `--shot-big K plik.png` |
| **0.9** | Przyciski REC / PAUSE / STOP / SPLIT w oknie dużego podglądu | **wydana** — `--shot-big K plik.png --big-rec` |
| **1.0** | Pauza wg 3.1: PAUSE/wznowienie w tym samym pliku, wspólne przesunięcie czasu obrazu i dźwięku, lista pauz w pliku opisu, skrót `P` | **wydana** — `tests\rec_test.ps1 -PauseAt/-PauseFor/-PauseCount` |
| **1.1** | Wyjście na żywo kanału równolegle do nagrywania: NDI i SRT (5.9) | **wydana** — `tests\live_test.ps1 -Type srt/ndi` |
| **1.2** | Sesja i listy (5.5): zapis/wczytanie, edytowalna nazwa sesji, autozapis i przywracanie po awarii, ostatnie pliki, otwieranie ostatniej listy | **wydana** — `tests\list_test.ps1` |
| **1.3** | Wyjście na żywo przełączane w trakcie nagrywania (gałąź dopinana w locie) | **wydana** — `tests\rec_test.ps1 -LiveCycle`, `tests\live_test.ps1 -EnableAfter` |
| **1.4** | Operacje grupowe ze wspólnym `T` (wspólny moment startu, pauzy i stopu wielu kanałów) | **wydana** — `tests\sync_test.ps1`: 16 × testsrc startuje, pauzuje i kończy w ±1 klatce |
| **1.5** | Inspektor Input/Output w pełni edytowalny; wejścia lokalne: Screen grab (DXGI/WGC), UVC/MF, WASAPI (urządzenie, loopback) | nagranie ekranu z dźwiękiem systemowym |
| **1.6** | SRT, RTSP, passthrough, `fallbacksrc` + polityka zaniku sygnału, stan RECONNECTING | SRT/RTSP z lokalnego `ffmpeg`/MediaMTX; zerwanie i powrót strumienia bez utraty pliku |
| **1.7** | NDI (discovery), DeckLink; kodeki H.265/AV1/ProRes/DNxHR wg dostępności; MOV/MP4 robust | NDI z NDI Tools; DeckLink — niesprawdzone na sprzęcie |
| **1.8** | Scheduler (reguły, catch‑up, DST), blokada uśpienia, logi rotowane, widok Lista, folder zapasowy | testy jednostkowe schedulera; kontrolowany Stop przy braku miejsca |
| **1.9** | WHEP, WHIP, magazyn poświadczeń, odzyskiwanie po awarii, instalator Inno Setup | instalacja na czystym Windows; test 24 h |

Każda runda poprawek po uwagach właściciela dostaje własny numer, więc pozycje poniżej bieżącej wersji przesuwają się o jedną. Numery wersji, od których zapowiadane funkcje będą dostępne, są w kodzie w jednym miejscu: `Roadmap` w `src/core/Lang.h`.

---

## 7. Struktura repozytorium (korekta 5.1)

```
iso-recorder/
├─ CMakeLists.txt, CMakePresets.json
├─ instrukcja.md, instrukcja_koncowa.md, rozmowa.md, CHANGELOG.md
├─ mockup/, media/, interfejs.png        # wzorzec interfejsu
├─ src/
│  ├─ main.cpp
│  ├─ app/      Theme.h, MainWindow, HeaderBar, ChannelGrid, ChannelTile, Inspector, ControlPanel
│  ├─ core/     Channel, ChannelManager, EnvCheck, Lang   (+ Scheduler, SystemHealth później)
│  ├─ media/    PipelineBuilder, InputFactory, PreviewSink, GstBusBridge   (+ RecordBin, EncoderFactory z nagrywaniem)
│  └─ util/     GstPtr.h, FileNameTemplate
├─ resources/   czcionki, ikony, app.manifest, app.rc
├─ tests/       (z nagrywaniem: testy z ffprobe)
├─ scripts/     build.ps1, run.ps1
└─ installer/   (ostatnia wersja planu)
```

---

## 8. Otwarte pytania do właściciela projektu

1. Docelowy sprzęt nagrywający (GPU, karty HDMI) — od tego zależy, czy 1.7 (DeckLink) i profil 16 kanałów da się w ogóle potwierdzić.
2. Czy WHIP jako serwer w aplikacji jest wymagany, czy wystarczy WHEP + SRT/RTSP (wtedy 1.9 jest wyraźnie prostsze).
