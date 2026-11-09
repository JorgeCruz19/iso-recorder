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
| D5 | **WHIP/WHEP zostają w zakresie**, ale jako ostatni typ wejścia (wersja 1.1) i tylko gdy wtyczka się ładuje. MediaMTX jako brama jest udokumentowaną alternatywą, nie zamiennikiem. |
| D6 | **Typy wejść i kodeki są bramkowane dostępnością**: przy starcie aplikacja sprawdza, które elementy GStreamera faktycznie działają; niedostępne pozycje są w UI wyszarzone z powodem („brak wtyczki ndi”, „brak enkodera HW”). |
| D7 | Wersjonowanie: **0.1, 0.2, … co 0.1** — każda wersja to commit + tag `v0.N` + push do prywatnego repo `studiokrause/iso-recorder`. |
| D8 | Testy plików (`ffprobe`) od wersji 0.3 (pierwszej z nagrywaniem), nie na końcu. |
| D9 | Interfejs w jednym języku naraz: domyślnie angielski, do wyboru PL, DE, ES, IT, FR (5.5). |

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

Test akceptacyjny (od wersji 0.5): Rec 10 s → Pause 5 s → Resume → 10 s → Stop; `ffprobe`: czas ≈ 20 s (±2 klatki), PTS wideo ściśle rosnące bez luki > 1,5 klatki, różnica początku/końca A i V < 40 ms.

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
┌ natywny pasek tytułu Windows: „ISO Recorder — <sesja>” ┐
├ NAGŁÓWEK 72 px: Sesja │ Czas nagrania aktywnego kanału + REC │ Timecode RTC │
│                  Dysk (czas do zapełnienia, pasek) │ Zapis · CPU · GPU · ENK. HW · Drop │ ☰ │ Widok 2×2 3×3 4×4 Lista
├ SIATKA KAFLI (tło #0b0b0b, odstęp 6 px)              │ INSPEKTOR 500 px
│                                                      │  nagłówek „Inspektor — Kanał NN · nazwa”
│                                                      │  podgląd 16:9, mierniki L/R ze skalą dBFS
│                                                      │  zakładki Input / Output / Scheduler
│                                                      │  sekcje zwijane (Źródło, Audio, Utrata sygnału…)
├ PANEL STEROWANIA 86 px: Selected channels (n): [REC][PAUSE][STOP][SPLIT] │
│     Selection: [SELECT ALL][SELECT NONE][INVERT] │ Active channel: [MOVE UP][MOVE DOWN] │
│     Channel list: [ADD SOURCE][LOAD][SAVE] │ licznik n/16 + legenda stanów
└ PASEK STANU 22 px: Autosave hh:mm:ss · ostatnie zdarzenia · komunikat po prawej
```

Odstępstwa od mockupu (świadome):

| Mockup | Program | Powód |
|---|---|---|
| Stały rozmiar 1920×1080 | Układ elastyczny, minimum 1280×720; inspektor 500 px przy ≥ 1600 px szerokości, 400 px poniżej; zwijany klawiszem `Tab` | Program musi działać na innych monitorach i przy skalowaniu DPI |
| Własny pasek tytułu | Natywny pasek Windows (ciemny przez `DWMWA_USE_IMMERSIVE_DARK_MODE`) | Okno bez ramki traci przyciąganie, cień i obsługę wielu monitorów |
| Brak menu | Przycisk **☰** w nagłówku: Nowa lista, Wczytaj, Zapisz, Zapisz jako, Ostatnie, Ustawienia, Diagnostyka, O programie | Mockup nie ma miejsca na funkcje z F8 i ustawienia |
| „NVENC 12/∞” | „ENK. HW n” | Patrz 3.3 |
| Czcionka Open Sans z Google Fonts | Open Sans dołączona do zasobów (licencja OFL), zapas: Segoe UI | Program nie może zależeć od sieci |
| Grupa „Wybór”: SELECT ALL + ADD SOURCE; grupa „Lista kanałów”: W GÓRĘ, W DÓŁ, WCZYTAJ, ZAPISZ | Cztery grupy: **Selected channels** (transport), **Selection** (SELECT ALL / SELECT NONE / INVERT), **Active channel** (MOVE UP / MOVE DOWN), **Channel list** (ADD SOURCE / LOAD / SAVE) | Decyzja właściciela: dodawanie źródła nie jest wyborem, a przesuwanie dotyczy jednego kanału, nie listy |
| Etykiety mieszane PL/EN | Jeden język naraz: domyślnie **angielski**, do wyboru PL, DE, ES, IT, FR (☰ → Language) | Decyzja właściciela; patrz 5.5 |
| Widok „Lista” niezaprojektowany | Tabela: nr, nazwa, stan, źródło, format, czas, rozmiar, VU poziomy, przyciski | — |

Kolory, wymiary i typografia — dokładnie z bloku `:root` i reguł CSS mockupu (jedno źródło prawdy: `src/app/Theme.h`).

### 5.2 Kafel kanału (`ChannelTile`)

Jeden widget, całość malowana w `paintEvent` (`QPainter`), bez widgetów potomnych — 16 kafli × kilkanaście kontrolek byłoby zbyt kosztowne w odświeżaniu 15 Hz.

- **Obraz** 16:9, `object-fit: cover`; ramka: 3 px czerwona (REC), bursztynowa (PAUZA), zielona (PREVIEW), 2 px szara #3c3c3c (bezczynny/czeka). Kanał aktywny: dodatkowa biała linia 1 px wewnątrz.
- **Nakładki**: pole zaznaczenia (lewy górny), format `1080p50`, czas nagrania z kropką stanu (prawy górny; przy zaplanowanym starcie „START 11:00”), miernik audio pionowy 2 słupki (prawa krawędź), etykieta `NN NAZWA` (dół, środek), komunikat pełnoekranowy (`RECONNECTING`, `NO SIGNAL`, `BŁĄD`) z przyciemnieniem i odbarwieniem obrazu.
- **Pasek** 26 px: `REC` `II` `STOP` `SPLIT` (podświetlenie i wyszarzenie wg stanu), opis źródła, uchwyt `⋮⋮`, `✕`.
- **Mysz**: klik w obraz = kanał aktywny (inspektor); klik w pole zaznaczenia = zaznaczenie; Ctrl+klik = przełącz zaznaczenie; Shift+klik = zakres; dwuklik w etykietę = zmiana nazwy; przeciąganie za `⋮⋮` = zmiana kolejności; dwuklik w obraz = powiększenie kanału na całą siatkę.
- **Klawiatura** (kanał aktywny): `R` Rec, `P` Pause/Resume, `S` Stop, `X` Split, `Del` usuń, `Ctrl+↑/↓` przesuń, strzałki = zmiana aktywnego; `Ctrl+A` zaznacz wszystkie; z `Shift` — to samo dla zaznaczonych (`Shift+R` itd.).

### 5.3 Inspektor

Zakładki Input / Output / Scheduler dotyczą **kanału aktywnego** (nie zaznaczonych). Sekcje jak w mockupie; pod zakładką Input zwinięte streszczenia Output i Scheduler (klik = przejście do zakładki). Pomarańczowa kropka w nagłówku sekcji = sekcja ma wartości inne niż domyślne. Podczas nagrywania pola Input/Output są zablokowane; Scheduler pozostaje edytowalny.

### 5.4 Panel sterowania i pasek stanu

- Przyciski grupowe działają na **zaznaczonych**; gdy nic nie jest zaznaczone — nieaktywne. `REC` świeci, gdy nagrywa co najmniej jeden zaznaczony.
- `SELECT ALL` / `SELECT NONE` / `INVERT` zmieniają zaznaczenie (skróty: `Ctrl+A`, `Ctrl+Shift+A`, `Ctrl+I`).
- `MOVE UP` / `MOVE DOWN` przesuwają kanał aktywny i dlatego stoją w osobnej grupie „Active channel”.
- Licznik `n / 16` i legenda: REC, PAUZA, PREVIEW, CZEKA (zaplanowany start), BŁĄD (tylko gdy > 0).
- Pasek stanu: czas ostatniego autozapisu, dwa ostatnie zdarzenia z dziennika, komunikat kontekstowy po prawej.

### 5.5 Języki

- Interfejs występuje zawsze w jednym języku. Domyślny jest angielski; dodatkowe: polski, niemiecki, hiszpański, włoski, francuski.
- Wybór: ☰ → Language; zapamiętywany w ustawieniach użytkownika; zmiana działa od razu, bez restartu i bez przerywania kanałów (okno jest budowane od nowa, pipeline'y zostają).
- Kluczem tłumaczenia jest tekst angielski (`T("...")`, tablica w `src/core/Lang.cpp`); brak tłumaczenia = tekst angielski. Każdy nowy napis interfejsu musi dostać wiersz we wszystkich pięciu językach.
- Nie tłumaczy się terminów branżowych i nazw własnych: REC, PAUSE, STOP, SPLIT, Input, Output, Scheduler, Timecode, Autosave, CPU, GPU, Drop, nazw protokołów i urządzeń.
- Przyciski mają szerokość dopasowaną do napisu; układ musi mieścić się przy 1600 px szerokości w najdłuższym języku (niemiecki, francuski).
- Raport `--check-env` i dziennik techniczny są tylko po angielsku.

---

## 6. Plan wersji

Każda wersja: działający program, commit, tag, push. Kryterium = to, co da się sprawdzić na maszynie deweloperskiej.

| Wersja | Zakres | Kryterium |
|---|---|---|
| **0.1** | Repo, CMake, `--check-env`, okno wg mockupu (nagłówek, siatka, inspektor, panel, pasek stanu), kafel malowany, kanały `testsrc` z podglądem i VU, dodawanie/usuwanie/zaznaczanie/kanał aktywny, widoki 2×2/3×3/4×4 | **wydana** |
| **0.2** | Języki (EN domyślnie + PL, DE, ES, IT, FR), nowy podział panelu sterowania, SELECT NONE i INVERT, paczka samodzielna `dist\` | **wydana** |
| **0.3** | `RecordBin`: Rec/Stop → MKV, enkoder z próbą mini‑pipeline'u, AAC, szablon nazw, finalizacja z timeoutem, zamknięcie aplikacji czeka na finalizację | `ffprobe`: plik poprawny, pierwsza klatka kluczowa; 50 × Rec/Stop bez wzrostu uchwytów |
| **0.4** | Split ręczny i automatyczny (czas/rozmiar), numeracja części, sidecar JSON, MPEG‑TS | suma długości części = czas nagrania ±1 klatka, brak luk PTS |
| **0.5** | Pauza wg 3.1 | test z 3.1 |
| **0.6** | Operacje grupowe ze wspólnym `T`, przenoszenie przeciąganiem, zapis/wczytanie listy, autosave, ostatnie pliki | 16 × testsrc startuje w ±1 klatce; lista wraca identyczna po wczytaniu |
| **0.7** | Inspektor Input/Output w pełni edytowalny; wejścia lokalne: Screen grab (DXGI/WGC), UVC/MF, WASAPI (urządzenie, loopback) | nagranie ekranu z dźwiękiem systemowym |
| **0.8** | SRT, RTSP, passthrough, `fallbacksrc` + polityka zaniku sygnału, stan RECONNECTING | SRT/RTSP z lokalnego `ffmpeg`/MediaMTX; zerwanie i powrót strumienia bez utraty pliku |
| **0.9** | NDI (discovery), DeckLink; kodeki H.265/AV1/ProRes/DNxHR wg dostępności; MOV/MP4 robust | NDI z NDI Tools; DeckLink — niesprawdzone na sprzęcie |
| **1.0** | Scheduler (reguły, catch‑up, DST), monitor dysku i prognoza, blokada uśpienia, logi rotowane, widok Lista | testy jednostkowe schedulera; kontrolowany Stop przy braku miejsca |
| **1.1** | WHEP, WHIP, magazyn poświadczeń, odzyskiwanie po awarii, instalator Inno Setup | instalacja na czystym Windows; test 24 h |

Numery wersji, od których zapowiadane funkcje będą dostępne, są w kodzie w jednym miejscu: `Roadmap` w `src/core/Lang.h`.

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
│  ├─ core/     Channel, ChannelManager, EnvCheck, Lang   (+ Scheduler, SystemHealth od 1.0)
│  ├─ media/    PipelineBuilder, InputFactory, PreviewSink, GstBusBridge   (+ RecordBin, EncoderFactory od 0.3)
│  └─ util/     GstPtr.h, FileNameTemplate
├─ resources/   czcionki, ikony, app.manifest, app.rc
├─ tests/       (od 0.3: testy z ffprobe)
├─ scripts/     build.ps1, run.ps1
└─ installer/   (1.1)
```

---

## 8. Otwarte pytania do właściciela projektu

1. Docelowy sprzęt nagrywający (GPU, karty HDMI) — od tego zależy, czy 0.8 (DeckLink) i profil 16 kanałów da się w ogóle potwierdzić.
2. Czy WHIP jako serwer w aplikacji jest wymagany, czy wystarczy WHEP + SRT/RTSP (wtedy 1.0 jest wyraźnie prostsze).
