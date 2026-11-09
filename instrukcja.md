# Video ISO Recorder — instrukcja napisania i kompilacji natywnej aplikacji Windows

> Dokument opisuje **co** zbudować, **czym**, **jak** to skompilować i wdrożyć oraz **jakie problemy** wystąpią i jak je rozwiązać.
> Docelowo: natywna aplikacja Windows x64 nagrywająca jednocześnie od 1 do 16 niezależnych źródeł wideo/audio („ISO recording” — każde źródło do osobnego pliku).

---

## Spis treści

1. [Cel i zakres funkcjonalny](#1-cel-i-zakres-funkcjonalny)
2. [Wybór technologii](#2-wybór-technologii)
3. [Wymagania sprzętowe i systemowe](#3-wymagania-sprzętowe-i-systemowe)
4. [Przygotowanie środowiska](#4-przygotowanie-środowiska)
5. [Architektura aplikacji](#5-architektura-aplikacji)
6. [Specyfikacja interfejsu](#6-specyfikacja-interfejsu)
7. [Szczegóły zakładek kanału: Input / Output / Scheduler](#7-szczegóły-zakładek-kanału)
8. [Format pliku listy kanałów](#8-format-pliku-listy-kanałów)
9. [Kroki implementacji (milestone’y) + kluczowy kod](#9-kroki-implementacji)
10. [Kompilacja, pakowanie, instalator](#10-kompilacja-pakowanie-instalator)
11. [Wymagania niefunkcjonalne](#11-wymagania-niefunkcjonalne)
12. [Potencjalne problemy i rozwiązania](#12-potencjalne-problemy-i-rozwiązania)
13. [Testy i kryteria akceptacji](#13-testy-i-kryteria-akceptacji)

---

## 1. Cel i zakres funkcjonalny

### 1.1 Funkcje główne

| # | Funkcja | Opis |
|---|---------|------|
| F1 | Kanały 1–16 | Dodawanie kanałów („Add source”) do limitu 16. Każdy kanał to niezależny tor: wejście → podgląd → nagrywanie. |
| F2 | Okno preview | Każdy kanał ma okno podglądu (wideo + wskaźnik poziomu audio VU, status, czas nagrania, rozmiar pliku). |
| F3 | Zakładka **Input** | HDMI (karty przechwytujące), NDI, SRT, RTSP, WebRTC (WHIP – serwer ingest, WHEP – klient pull), Screen grab; wybór audio (osadzone / osobne urządzenie / brak). |
| F4 | Zakładka **Output** | Folder, szablon nazwy, kontener, kodek wideo/audio, bitrate, auto-split, passthrough. |
| F5 | Zakładka **Scheduler** | Harmonogram startu/stopu/splitu: jednorazowy i cykliczny. |
| F6 | Przyciski kanału | **Rec**, **Pause**, **Stop**, **Split**, **Usuń kanał**. |
| F7 | Pasek globalny | **Rec / Pause / Stop / Split** dla zaznaczonych kanałów, **Select all**, **Add source**. |
| F8 | Lista kanałów | Przenoszenie (zmiana kolejności: drag & drop + skróty), **Wczytaj listę**, **Zapisz listę** (JSON). |

### 1.2 Definicje

- **Kanał** – konfiguracja (input + output + scheduler) + działający pipeline multimedialny.
- **Split** – zamknięcie bieżącego pliku i natychmiastowe rozpoczęcie kolejnego (bez utraty klatek, cięcie na klatce kluczowej).
- **Pause** – wstrzymanie zapisu do pliku bez zamykania go; po wznowieniu oś czasu w pliku jest ciągła (bez „dziury”).
- **Stop** – poprawne sfinalizowanie pliku (EOS → zamknięcie muxera → zapis indeksu/moov).

---

## 2. Wybór technologii

### 2.1 Rekomendacja

| Warstwa | Wybór | Wersja |
|--------|-------|--------|
| Język | **C++20** | MSVC 2022 (v143), x64 |
| GUI | **Qt 6 Widgets** | Qt 6.8 LTS (lub nowszy LTS) |
| Silnik multimedialny | **GStreamer** (MSVC build) + **gst-plugins-rs** | 1.26.x |
| Build | **CMake + Ninja**, CMake Presets | CMake ≥ 3.28 |
| Opcjonalne runtime/SDK | NDI Runtime / NDI SDK 6, Blackmagic Desktop Video, sterowniki Magewell | aktualne |
| Instalator | Inno Setup 6 lub WiX 4 | — |
| Testy | Qt Test + CTest, `ffprobe` do weryfikacji plików | — |

### 2.2 Dlaczego GStreamer

GStreamer jako jedyny dojrzały, otwarty framework ma **w jednym ekosystemie wszystkie wymagane wejścia, enkodery sprzętowe i mechanizm splitu**:

| Wymaganie | Element GStreamer |
|-----------|-------------------|
| HDMI – Blackmagic DeckLink | `decklinkvideosrc`, `decklinkaudiosrc` (plugins-bad; nagłówki API są w pluginie, potrzebny tylko sterownik Desktop Video) |
| HDMI – Magewell / UVC / AVerMedia / Elgato | `mfvideosrc` (Media Foundation), audio `wasapi2src` |
| NDI | `ndisrc` + `ndisrcdemux` (gst-plugins-rs, plugin `ndi`; ładuje dynamicznie NDI Runtime) |
| SRT | `srtsrc` (caller/listener/rendezvous, szyfrowanie) |
| RTSP | `rtspsrc` (TCP/UDP, auth) |
| WebRTC WHEP (pull) | `whepclientsrc` / `whepsrc` (gst-plugins-rs `webrtchttp`/`rswebrtc`) |
| WebRTC WHIP (ingest – aplikacja jako serwer) | `whipserversrc` (gst-plugins-rs `rswebrtc`) |
| Screen grab | `d3d11screencapturesrc` (DXGI Desktop Duplication / Windows Graphics Capture, monitor lub okno), `d3d12screencapturesrc` |
| Audio systemowe / mikrofon | `wasapi2src` (w tym `loopback=true`) |
| Odporne źródła sieciowe | `fallbacksrc` (gst-plugins-rs `fallbackswitch`) – auto-reconnect + obraz zastępczy |
| Enkodery HW | NVIDIA: `nvh264enc`/`nvh265enc`/`nvav1enc` (lub `nvd3d11h264enc`), Intel: `qsvh264enc`/`qsvh265enc`/`qsvav1enc`, AMD: `amfh264enc`/`amfh265enc`/`amfav1enc`, MF: `mfh264enc` |
| Enkodery SW (licencja LGPL-friendly) | `openh264enc`, `avenc_prores_ks` (gst-libav), `avenc_dnxhd` |
| Audio enc | `avenc_aac`, `mfaacenc`, `opusenc`, PCM bez kompresji |
| Split / rotacja plików | `splitmuxsink` (`split-now`, `split-after`, `max-size-time`, `max-size-bytes`) |
| Kontenery | `matroskamux`, `qtmux`/`mp4mux` (tryb „robust”), `isofmp4mux` (fragmented MP4, rs) |
| Zero-copy GPU | D3D11/D3D12 memory: `d3d11h264dec`, `d3d11convert`, `d3d11scale` |

### 2.3 Dlaczego Qt 6 Widgets

- Natywne okna Win32, dojrzałe layouty, drag & drop, model/view, wysoki DPI, dobra integracja z C++.
- Licencja LGPLv3 przy linkowaniu dynamicznym (wystarczająca dla aplikacji zamkniętej, przy spełnieniu warunków LGPL).
- Widgets zamiast QML: prostsza integracja podglądu z GStreamerem (natywny HWND lub `QImage`), mniejsze ryzyko problemów z RHI.

### 2.4 Odrzucone alternatywy

| Alternatywa | Powód odrzucenia |
|-------------|------------------|
| C# / WPF / WinUI 3 + FFmpeg (FFmpeg.AutoGen) | Brak gotowych WHIP/WHEP i NDI w FFmpeg (NDI usunięte z FFmpeg), ręczne zarządzanie wątkami/timestampami, trudniejsza pauza/split bez utraty klatek. |
| Czysty Media Foundation / DirectShow | Brak SRT, NDI, WebRTC, RTSP – wszystko trzeba pisać od zera. |
| libobs (silnik OBS) | Zaprojektowany do jednej sceny/jednego wyjścia; 16 niezależnych nagrań ISO wymaga wielu instancji, licencja GPLv2 obejmuje całą aplikację. |
| Electron / web | Niewydajne dla 16 strumieni, brak dostępu do kart HDMI bez natywnych modułów. |
| Rust + gtk-rs/egui + GStreamer | Technicznie świetne, ale słabsze natywne GUI na Windows i mniejszy rynek programistów; dobra opcja zastępcza. |

---

## 3. Wymagania sprzętowe i systemowe

### 3.1 System

- Windows 10 22H2 x64 / Windows 10 IoT LTSC 2021 / Windows 11 23H2+ (x64).
- Windows Graphics Capture (przechwytywanie okien) wymaga Win10 1903+; obramowanie żółte wyłączalne od Win11 / Win10 20348+.
- Visual C++ Redistributable 2015–2022 x64.
- Uprawnienia: zwykły użytkownik; wyjątki zapory sieciowej dla NDI (mDNS 5353/UDP, 5960+ TCP), SRT (porty listenerów UDP), WHIP (port HTTP/HTTPS + zakres UDP ICE).

### 3.2 Sprzęt – profil referencyjny (16 × 1080p50)

| Komponent | Minimum (≤ 4 kanały) | Zalecane (16 kanałów) |
|-----------|----------------------|------------------------|
| CPU | 6 rdzeni, AVX2 | 12–16 rdzeni (Ryzen 9 / Core i9 / Xeon W) |
| RAM | 16 GB | 32–64 GB |
| GPU | dowolne z NVENC/QSV/AMF | NVIDIA RTX A2000+/RTX 4000 Ada (bez limitu sesji NVENC) **lub** GeForce + Intel iGPU QSV (rozkład obciążenia) |
| Dysk nagrań | SSD SATA | NVMe Gen4, osobny od systemowego, ≥ 4 TB; dla ProRes RAID0 NVMe |
| Sieć | 1 GbE | 10 GbE (NDI Full Bandwidth) |
| Karty HDMI | 1 × UVC | DeckLink Quad HDMI / Magewell Pro Capture Quad (PCIe) |

### 3.3 Budżet przepustowości (orientacyjnie)

| Format | 1 kanał | 16 kanałów | Zapis na 1 h (16 kan.) |
|--------|---------|------------|------------------------|
| H.264 1080p50, 20 Mb/s | 2,5 MB/s | 40 MB/s | ~144 GB |
| H.265 1080p50, 12 Mb/s | 1,5 MB/s | 24 MB/s | ~86 GB |
| ProRes 422 HQ 1080p50 | ~55 MB/s | ~880 MB/s | ~3,2 TB |
| NDI Full (wejście sieciowe) | ~150 Mb/s | ~2,4 Gb/s | — |
| NDI\|HX / SRT H.264 | 10–20 Mb/s | 160–320 Mb/s | — |

Wniosek: kodeki intra (ProRes/DNxHR) dla 16 kanałów wymagają macierzy NVMe; domyślnie stosować H.264/H.265 HW.

---

## 4. Przygotowanie środowiska

### 4.1 Narzędzia

1. **Visual Studio 2022** (Community/Professional) – workload „Desktop development with C++”, komponenty: MSVC v143, Windows 11 SDK (10.0.22621+), C++ CMake tools.
2. **CMake ≥ 3.28** i **Ninja** (w VS są wbudowane; można też `winget`).
3. **Git**.
4. **Qt 6.8 LTS** – Qt Online Installer → komponent „MSVC 2022 64-bit”, opcjonalnie „Qt Debug Information Files”.
5. **GStreamer 1.26.x MSVC x86_64** – ze strony gstreamer.freedesktop.org pobrać **runtime** i **development** (MSVC 64-bit), instalacja typu **Complete** (zawiera m.in. plugins-bad, libav, rs).
6. (opcjonalnie) **NDI Runtime / NDI Tools** (do odbioru NDI) lub **NDI SDK** (do testów i redystrybucji).
7. (opcjonalnie) **Blackmagic Desktop Video**, **sterowniki Magewell**, **sterowniki NVIDIA/Intel/AMD** w aktualnych wersjach.
8. `ffmpeg`/`ffprobe` – tylko do testów plików wynikowych.

Przykład (PowerShell, winget):

```powershell
winget install Kitware.CMake Ninja-build.Ninja Git.Git Gyan.FFmpeg
winget install Microsoft.VisualStudio.2022.Community --override "--add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended --passive"
```

### 4.2 Zmienne środowiskowe

Instalator GStreamera ustawia `GSTREAMER_1_0_ROOT_MSVC_X86_64` (np. `C:\Program Files\gstreamer\1.0\msvc_x86_64\`). Dodatkowo:

```powershell
setx Qt6_DIR "C:\Qt\6.8.3\msvc2022_64\lib\cmake\Qt6"
setx PATH "$env:PATH;C:\Program Files\gstreamer\1.0\msvc_x86_64\bin"
```

### 4.3 Weryfikacja wtyczek

```powershell
gst-inspect-1.0 --version
gst-inspect-1.0 decklinkvideosrc mfvideosrc ndisrc srtsrc rtspsrc whepclientsrc whipserversrc d3d11screencapturesrc wasapi2src splitmuxsink nvh264enc qsvh264enc amfh264enc fallbacksrc
gst-device-monitor-1.0 Video/Source Audio/Source
```

Jeśli brak któregoś pluginu rs (np. `ndisrc`), zbudować **gst-plugins-rs** (`cargo install cargo-c`, `cargo cbuild -p gst-plugin-ndi --release`) i dołożyć DLL do katalogu pluginów aplikacji.

### 4.4 Szybki test pipeline'u z konsoli (sanity check)

```powershell
gst-launch-1.0 -e videotestsrc is-live=true ! video/x-raw,width=1920,height=1080,framerate=50/1 ! tee name=t `
  t. ! queue ! d3d11upload ! d3d11videosink `
  t. ! queue ! nvh264enc bitrate=20000 gop-size=50 ! h264parse ! splitmuxsink muxer-factory=matroskamux location=test_%03d.mkv max-size-time=60000000000
```

---

## 5. Architektura aplikacji

### 5.1 Struktura repozytorium

```
iso-recorder/
├─ CMakeLists.txt
├─ CMakePresets.json
├─ cmake/                    # FindGStreamer.cmake, deploy.cmake
├─ src/
│  ├─ main.cpp               # init GStreamer + Qt, ustawienia procesu
│  ├─ app/                   # MainWindow, ChannelListWidget, ChannelWidget, dialogi
│  ├─ core/
│  │  ├─ ChannelManager.*    # kolekcja kanałów, operacje grupowe, limit 16
│  │  ├─ Channel.*           # konfiguracja + maszyna stanów
│  │  ├─ ChannelConfig.*     # struktury Input/Output/Schedule + (de)serializacja JSON
│  │  ├─ Scheduler.*         # harmonogram
│  │  ├─ DeviceEnumerator.*  # GstDeviceMonitor + NDI discovery
│  │  ├─ SystemHealth.*      # wolne miejsce, CPU/GPU, zapobieganie uśpieniu
│  │  └─ Logger.*
│  ├─ media/
│  │  ├─ PipelineBuilder.*   # budowa pipeline'u z konfiguracji
│  │  ├─ InputFactory.*      # src-biny dla każdego typu wejścia
│  │  ├─ EncoderFactory.*    # wybór enkodera HW/SW + fallback
│  │  ├─ RecordBin.*         # gałąź nagrywania: pauza, split, stop
│  │  ├─ PreviewSink.*       # appsink -> QImage / overlay
│  │  └─ GstBusBridge.*      # przekazywanie wiadomości busa do wątku GUI
│  └─ util/                  # RAII dla GstObject (GstPtr<T>), formatowanie nazw plików
├─ resources/                # ikony, qss, obraz „No signal”
├─ tests/
└─ installer/                # Inno Setup / WiX
```

### 5.2 Warstwy i odpowiedzialności

```
┌──────────────────────── GUI (wątek główny Qt) ────────────────────────┐
│ MainWindow ─ ChannelListWidget ─ ChannelWidget[1..16] ─ GlobalToolbar │
└───────────────▲───────────────────────────────┬───────────────────────┘
       sygnały Qt (QueuedConnection)       komendy (start/pause/stop/split)
┌───────────────┴───────────────────────────────▼───────────────────────┐
│ ChannelManager  ─  Scheduler  ─  Persistence(JSON)  ─  SystemHealth   │
├────────────────────────────────────────────────────────────────────────┤
│ Channel (state machine)  →  PipelineBuilder / RecordBin / PreviewSink │
├────────────────────────────────────────────────────────────────────────┤
│ GStreamer: 1 GstPipeline na kanał, wspólny zegar, własne wątki         │
└────────────────────────────────────────────────────────────────────────┘
```

Zasady:
- **Jeden `GstPipeline` na kanał** – awaria/rekonfiguracja jednego kanału nie wpływa na inne.
- **Wspólny zegar** (`GstSystemClock`, `GST_CLOCK_TYPE_MONOTONIC`) i wspólny `base-time` dla kanałów startowanych grupowo → spójne timestampy między plikami.
- **Wszystkie zmiany stanu pipeline'u wykonywane w dedykowanym wątku sterującym kanału** (`QThread` + kolejka komend) – nigdy w wątku streamingu GStreamera i nie blokująco w GUI.
- Wiadomości busa: `gst_bus_set_sync_handler` → `QMetaObject::invokeMethod(channel, ..., Qt::QueuedConnection)` (na Windows nie ma domyślnie pętli GLib zintegrowanej z Qt).

### 5.3 Maszyna stanów kanału

```
          configure           start preview
 [Empty] ──────────► [Idle] ───────────────► [Previewing]
                       ▲                          │  Rec
                       │ stop preview             ▼
                       │                    [Recording] ◄──── Resume ────┐
                       │                     │   │   │                    │
                       │               Split │   │   └── Pause ──► [Paused]
                       │           (zostaje) ◄┘   │ Stop                  │
                       │                          ▼                       │ Stop
                       └────────────────── [Finalizing] ◄─────────────────┘
                                                  │ EOS z muxera / timeout
                                                  ▼
                                            [Previewing]
 Dowolny stan ──błąd źródła──► [Reconnecting] ──ok──► poprzedni stan
 Dowolny stan ──błąd krytyczny──► [Error] (komunikat, przycisk Retry)
```

Reguły: przycisk/komenda niedozwolona w danym stanie jest nieaktywna (np. Split tylko w Recording; Usuń kanał w Recording → potwierdzenie, następnie Stop+finalizacja, potem usunięcie).

### 5.4 Pipeline kanału

```
[Input bin] ─► video: ─► (decode/convert, D3D11) ─► tee_v ─┬─► queue(leaky) ─► d3d11scale 480x270 ─► fps 15 ─► appsink (preview)
            │                                               └─► [RecordBin.video_sink]
            └► audio: ─► audioconvert/resample ─► level ─► tee_a ─┬─► fakesink (VU przez level)
                                                                    └─► [RecordBin.audio_sink]

[RecordBin] (dodawany przy Rec, usuwany po Stop)
  video_sink(ghost, pad-probe: pauza/offset) ─► queue ─► encoder (HW) ─► parser ─┐
  audio_sink(ghost, pad-probe: pauza/offset) ─► queue ─► audio enc ─► parser ──┤
                                                                               ▼
                                     splitmuxsink (muxer-factory, location/format-location-full,
                                                   max-size-time/bytes, async-finalize=true)
```

Tryb **passthrough** (SRT/RTSP/NDI|HX/WHEP z H.264/H.265): gałąź nagrywania podłączona do strumienia **przed dekoderem** (`parsebin` → `tee_es`), bez enkodera; preview z dekodowanej gałęzi.

### 5.5 Wątki

| Wątek | Zadanie |
|-------|---------|
| GUI (main) | Widgety, rysowanie preview (QImage), timery UI 4–10 Hz |
| ChannelControl ×N | Kolejka komend kanału, zmiany stanów GStreamer, timeouty finalizacji |
| GStreamer streaming | Tworzone przez `queue`/źródła – nie dotykać z nich GUI |
| Scheduler | `QTimer` w wątku głównym (lekkie), wysyła komendy do kanałów |
| Health | Co 1–2 s: wolne miejsce, rozmiary plików, statystyki `queue`/dropped |

---

## 6. Specyfikacja interfejsu

### 6.1 Okno główne

```
┌─ Menu: Plik [Nowa lista | Wczytaj listę… | Zapisz listę | Zapisz jako… | Ostatnie] Ustawienia  Pomoc ─┐
├────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│  Widok: [Siatka 2×2/3×3/4×4 | Lista]       Dysk D: 1,8 TB wolne (~12 h)    CPU 34%  GPU 41%          │
├────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│ ┌── Kanał 1 ─────────────── ☑ ≡ ✕ ┐ ┌── Kanał 2 ─────────────── ☐ ≡ ✕ ┐                              │
│ │ ┌─────────────────────┐ ▌▌ VU   │ │ ┌─────────────────────┐ ▌▌ VU   │      ☑ = zaznaczenie           │
│ │ │      PREVIEW        │ ▌▌      │ │ │   NO SIGNAL         │ ▌▌      │      ≡ = uchwyt przenoszenia   │
│ │ └─────────────────────┘         │ │ └─────────────────────┘         │      ✕ = usuń kanał           │
│ │ ● REC 01:12:33  part 003  4.2GB │ │ ■ IDLE                          │                              │
│ │ [Input][Output][Scheduler]      │ │ [Input][Output][Scheduler]      │                              │
│ │ ( ● Rec ) ( ❚❚ Pause ) ( ■ Stop ) ( ✂ Split )                      │                              │
│ └─────────────────────────────────┘ └─────────────────────────────────┘                              │
├────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│ [● REC sel.] [❚❚ PAUSE sel.] [■ STOP sel.] [✂ SPLIT sel.]   [☑ Select all]   [+ Add source]  (3/16) │
└────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

### 6.2 Karta kanału (`ChannelWidget`)

- Nagłówek: edytowalna nazwa kanału (używana w nazwie pliku), checkbox zaznaczenia, uchwyt drag, przycisk usunięcia, kolorowy wskaźnik stanu (szary Idle, zielony Preview, czerwony pulsujący Rec, żółty Pause, pomarańczowy Reconnecting, czerwony stały Error).
- Preview 16:9 z napisami: rozdzielczość/fps źródła, timecode, „NO SIGNAL”.
- VU: 2–8 słupków z `level` (peak + RMS, dBFS, kolor > −6 dB żółty, > −1 dB czerwony, clip-hold).
- Status: czas nagrania (bez pauz), numer części, rozmiar bieżącego pliku, bitrate, dropped frames.
- Zakładki `QTabWidget`: Input / Output / Scheduler (rozwijane lub w dialogu „Ustawienia kanału” – w widoku 4×4 karta jest za mała, więc zakładki otwierają się jako panel boczny/dock dla aktywnego kanału).
- Przyciski: Rec, Pause (toggle Pause/Resume), Stop, Split; skróty klawiaturowe dla aktywnego kanału.
- Zmiana ustawień Input/Output **zablokowana podczas nagrywania** (lub zastosowanie przy następnym splicie – opcja).

### 6.3 Pasek globalny

- **Rec / Pause / Stop / Split (selected)** – wykonanie na wszystkich zaznaczonych kanałach; Rec/Split wykonywane **synchronicznie** (wspólny znacznik czasu, patrz 9.6).
- **Select all** – toggle (zaznacz wszystkie / odznacz wszystkie), Ctrl+A.
- **Add source** – dodaje kanał z domyślną konfiguracją i otwiera zakładkę Input; nieaktywny przy 16 kanałach.
- Licznik `n/16`.
- Stop dla wielu kanałów → okno potwierdzenia (ochrona przed przypadkowym zakończeniem).

### 6.4 Lista kanałów

- Przenoszenie: drag & drop karty (MIME `application/x-isorec-channel-id`), Ctrl+↑/↓, menu kontekstowe „Przesuń w górę/w dół/na początek/na koniec”. Kolejność = numeracja kanałów i kolejność w zapisanym pliku.
- Wczytaj listę: jeśli są aktywne nagrania → zapytanie; wczytanie zatrzymuje preview obecnych kanałów i buduje nowe; walidacja wersji schematu i ścieżek.
- Zapis listy: JSON (rozdz. 8) + autosave co 30 s i przy każdej zmianie do `%LOCALAPPDATA%\IsoRecorder\autosave.json`.
- Duplikuj kanał (menu kontekstowe) – przydatne przy wielu podobnych źródłach.

---

## 7. Szczegóły zakładek kanału

### 7.1 Input

| Typ | Parametry w UI | Implementacja |
|-----|----------------|---------------|
| **HDMI – DeckLink** | urządzenie (lista), port/wejście, tryb (auto-detect lub 1080p50/59.94/2160p…), audio osadzone: liczba kanałów 2/8/16 | `decklinkvideosrc device-number=N mode=auto connection=hdmi` + `decklinkaudiosrc device-number=N channels=8`; format wideo v210/UYVY |
| **HDMI – UVC/Magewell/MF** | urządzenie (z `GstDeviceMonitor` klasa `Video/Source`), format (NV12/YUY2/MJPEG), rozdzielczość, fps; audio: powiązane urządzenie WASAPI | `mfvideosrc device-path=...` + `wasapi2src device=...`; MJPEG → `jpegdec`/`d3d11jpegdec` |
| **NDI** | źródło z listy discovery (odświeżanie), lub ręcznie nazwa/IP, bandwidth (highest/lowest/audio-only), receiver color format | `ndisrc ndi-name="PC (Source)" ! ndisrcdemux name=d d.video ! ... d.audio ! ...`; discovery przez `GstDeviceMonitor` (`Source/Network` z pluginu ndi) |
| **SRT** | tryb caller/listener/rendezvous, host, port, latency (ms), passphrase + pbkeylen, streamid | `srtsrc uri="srt://0.0.0.0:9000?mode=listener&latency=200" ! tsdemux/parsebin`; najlepiej opakowane w `fallbacksrc` |
| **RTSP** | URL, użytkownik/hasło (przechowywane w Credential Manager), transport TCP/UDP, latency | `rtspsrc location=... protocols=tcp latency=200 user-id= user-pw=` lub `fallbacksrc uri=` |
| **WebRTC WHEP (pull)** | URL endpointu WHEP, bearer token, STUN/TURN | `whepclientsrc` (konfiguracja signallera `whep-endpoint`, `auth-token`), wyjście dekodowane lub zakodowane |
| **WebRTC WHIP (ingest)** | port HTTP(S), ścieżka, token, certyfikat TLS, STUN/TURN, zakres portów UDP | `whipserversrc` – aplikacja jest serwerem WHIP; nadawca (np. OBS) wysyła do `http://host:port/whip/endpoint` |
| **Screen grab** | monitor (lista) lub okno (lista HWND z tytułami), obszar (crop), kursor on/off, fps, tryb DXGI/WGC | `d3d11screencapturesrc monitor-index=0 show-cursor=true capture-api=wgc` (okno: `window-handle=`), dalej `d3d11convert` bez kopiowania do CPU |
| **Audio (wspólne)** | Źródło: osadzone / urządzenie WASAPI / pętla systemowa (loopback) / brak; mapowanie kanałów; offset A/V (ms); gain | `wasapi2src device=... loopback=...`, `audiomixmatrix`/`audioconvert mix-matrix`, offset przez `gst_pad_set_offset` |

Wspólne dla wejść:
- Przycisk **Test / Podgląd** – uruchamia pipeline w trybie preview i pokazuje wykryty format.
- Wykrywanie formatu i zmian (`caps` events) – wyświetlane w statusie.
- Opcja **Auto-reconnect** (timeout, liczba prób, obraz zastępczy podczas zaniku, nagrywanie dalej czy pauza).
- Opcja **Timecode**: brak / zegar systemowy (RTC) / z sygnału (DeckLink RP188) – `timecodestamper`.

### 7.2 Output

| Parametr | Wartości | Uwagi |
|----------|----------|-------|
| Folder | wybór katalogu, test zapisu i wolnego miejsca | możliwy folder per kanał lub wspólny |
| Szablon nazwy | tokeny: `{channel}`, `{index}`, `{date}`, `{time}`, `{part}`, `{source}`, `{session}` np. `{session}_{index:02}_{channel}_{date}_{time}_{part:03}` | sanityzacja znaków niedozwolonych `<>:"/\|?*`, kolizje → sufiks |
| Kontener | **MKV** (domyślny, odporny na awarie), **MP4/MOV robust** (`qtmux reserved-max-duration`, `reserved-moov-update-period`), **fMP4** (`isofmp4mux`), **MPEG-TS** (`mpegtsmux`, najbardziej odporny) | MOV + ProRes dla postprodukcji |
| Kodek wideo | H.264 / H.265 / AV1 (HW: NVENC/QSV/AMF; SW: OpenH264), ProRes 422 Proxy/LT/Std/HQ (`avenc_prores_ks`), DNxHR, **Passthrough** | lista filtrowana dynamicznie wg dostępności (`gst_element_factory_find`) |
| Ustawienia wideo | rozdzielczość (źródłowa/skalowana), fps, rate control CBR/VBR/CQP, bitrate, max bitrate, GOP (s), B-frames, profil, preset (jakość/wydajność), 8/10 bit | GOP domyślnie 1 s – wpływa na precyzję splitu |
| Kodek audio | AAC-LC (128–320 kb/s), PCM 16/24 bit, Opus, brak | PCM w MOV/MKV dla postprodukcji |
| Auto-split | wył. / co N minut / co N GB / o pełnej godzinie | `max-size-time`, `max-size-bytes`, `send-keyframe-requests=true` |
| Dodatki | zapisywanie pliku `.json`/`.txt` z metadanymi (źródło, start, pauzy, timecode), sidecar CSV ze znacznikami | pomocne przy synchronizacji w montażu |

`EncoderFactory` – kolejność wyboru przy „Auto (HW)”: NVENC → QSV → AMF → MF → OpenH264; przy błędzie inicjalizacji (np. limit sesji) automatyczny fallback na kolejny i ostrzeżenie w UI.

### 7.3 Scheduler

- Lista reguł kanału, każda: **akcja** (Start / Stop / Split / Pause / Resume), **typ** (jednorazowa: data+godzina; cykliczna: dni tygodnia + godzina; „start + czas trwania”), **włączona/wyłączona**.
- Wygodny tryb: „Nagrywaj od HH:MM do HH:MM w dni: Pn–Pt”.
- Globalny scheduler (opcjonalnie) – reguła dla grupy kanałów → synchroniczny start.
- Implementacja: `Scheduler` przelicza najbliższe zdarzenie (`QDateTime` z `QTimeZone::systemTimeZone()`), `QTimer::singleShot` do tego momentu **plus** zabezpieczający tick co 1 s porównujący zegar ścienny (odporność na usypianie, zmianę czasu, DST).
- Po wybudzeniu/starcie aplikacji: jeśli bieżący czas mieści się w oknie „od–do” → start nagrania (polityka „catch-up”, konfigurowalna).
- Gdy zaplanowane jest nagranie: `SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED)`; opcjonalnie wybudzanie systemu przez `CreateWaitableTimer` + `SetWaitableTimer(fResume=TRUE)`.
- Konflikty (Start w trakcie nagrywania, Stop gdy Idle) – ignorowane z wpisem do logu.

---

## 8. Format pliku listy kanałów

Plik `*.isorec.json` (UTF-8, wersjonowany):

```json
{
  "schema": "isorec.channels",
  "version": 1,
  "session": "Konferencja_A",
  "createdWith": "IsoRecorder 1.0.0",
  "global": {
    "defaultOutputDir": "D:/Nagrania",
    "syncStart": true
  },
  "channels": [
    {
      "id": "8f1c2c0e-5d1b-4f0a-9a43-3a1c8d2b7e11",
      "name": "Kamera1",
      "input": {
        "type": "decklink",
        "deviceNumber": 0,
        "mode": "auto",
        "audio": { "source": "embedded", "channels": 2, "offsetMs": 0 },
        "reconnect": { "enabled": true, "timeoutMs": 3000, "fallbackImage": true },
        "timecode": "rtc"
      },
      "output": {
        "dir": "D:/Nagrania/{session}",
        "nameTemplate": "{index:02}_{channel}_{date}_{time}_{part:03}",
        "container": "mkv",
        "video": { "codec": "h264", "encoder": "auto-hw", "rateControl": "cbr", "bitrateKbps": 20000, "gopSeconds": 1, "bframes": 0 },
        "audio": { "codec": "aac", "bitrateKbps": 192 },
        "autoSplit": { "mode": "time", "minutes": 60 }
      },
      "schedule": [
        { "action": "start", "type": "weekly", "days": ["mon","tue","wed","thu","fri"], "time": "08:55", "enabled": true },
        { "action": "stop",  "type": "weekly", "days": ["mon","tue","wed","thu","fri"], "time": "17:05", "enabled": true }
      ]
    },
    {
      "id": "2b3e…",
      "name": "Zdalny_SRT",
      "input": { "type": "srt", "mode": "listener", "port": 9001, "latencyMs": 200, "passphraseRef": "cred:isorec/srt/9001" },
      "output": { "container": "ts", "video": { "codec": "passthrough" }, "audio": { "codec": "passthrough" } },
      "schedule": []
    }
  ]
}
```

Zasady:
- **Hasła/tokeny nie są zapisywane w JSON** – tylko referencja do Windows Credential Manager (`CredWriteW`/`CredReadW`), ewentualnie zaszyfrowane `CryptProtectData` (DPAPI) przy eksporcie.
- Wczytywanie odporne: nieznane pola ignorowane, brakujące → wartości domyślne, `version` > obsługiwana → ostrzeżenie; migracje `vN → vN+1` w `ChannelConfig::migrate()`.
- Zapis atomowy: zapis do `*.tmp` → `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`.
- Walidacja: max 16 kanałów, unikalne `id`, unikalne porty listenerów (SRT/WHIP), istniejące urządzenia (brak → kanał w stanie Error „urządzenie niedostępne”, konfiguracja zachowana).
- Odzyskiwanie po awarii: przy starcie, jeśli istnieje `autosave.json` z flagą `cleanShutdown=false` → propozycja przywrócenia listy i lista niesfinalizowanych plików do naprawy (rozdz. 12).

---

## 9. Kroki implementacji

### Milestone’y

| M | Zakres | Kryterium ukończenia |
|---|--------|----------------------|
| M1 | Szkielet: CMake, Qt okno, init GStreamera, logger, RAII dla GstObject | aplikacja startuje, `gst_version` w logu, deploy działa na czystej maszynie |
| M2 | Jeden kanał `videotestsrc`/`audiotestsrc`: preview (appsink → QImage), VU | płynny podgląd 15 fps, VU reaguje |
| M3 | RecordBin: Rec/Stop do MKV z HW enkoderem + finalizacja | plik poprawny w `ffprobe`, brak wycieków po 100 × Rec/Stop |
| M4 | Pause/Resume + Split (manual i auto) | ciągłość timestampów, brak utraconych klatek między częściami |
| M5 | ChannelManager: 16 kanałów, zaznaczanie, operacje grupowe, synchroniczny start, usuwanie, przenoszenie | 16 × testsrc nagrywa równocześnie, start w ±1 klatce |
| M6 | Wejścia: DeckLink, MF/UVC, Screen, WASAPI, NDI, SRT, RTSP, WHEP, WHIP + reconnect | każde wejście przetestowane z rzeczywistym źródłem |
| M7 | Output UI (kodeki, kontenery, passthrough), Scheduler, zapis/wczytanie listy, autosave | testy jednostkowe serializacji i schedulera |
| M8 | Hardening: monitoring dysku, zapobieganie uśpieniu, crash recovery, instalator, podpis | test 24 h × 16 kanałów bez błędów |

### 9.1 CMakeLists.txt (rdzeń)

```cmake
cmake_minimum_required(VERSION 3.28)
project(IsoRecorder VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Concurrent Test)

# GStreamer (MSVC) – pkg-config.exe jest dostarczany w bin\ GStreamera
set(GST_ROOT "$ENV{GSTREAMER_1_0_ROOT_MSVC_X86_64}")
set(ENV{PKG_CONFIG_PATH} "${GST_ROOT}/lib/pkgconfig")
set(PKG_CONFIG_EXECUTABLE "${GST_ROOT}/bin/pkg-config.exe")
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET
    gstreamer-1.0 gstreamer-base-1.0 gstreamer-app-1.0
    gstreamer-video-1.0 gstreamer-audio-1.0 gstreamer-pbutils-1.0)

file(GLOB_RECURSE SRC CONFIGURE_DEPENDS src/*.cpp src/*.h)
qt_add_executable(IsoRecorder WIN32 ${SRC} resources/resources.qrc resources/app.rc)

target_link_libraries(IsoRecorder PRIVATE Qt6::Widgets Qt6::Concurrent PkgConfig::GST
    Avrt Powrprof Advapi32 Crypt32)
target_compile_definitions(IsoRecorder PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(IsoRecorder PRIVATE /W4 /permissive- /utf-8 /MP)
# manifest: longPathAware, PerMonitorV2 DPI, supportedOS
target_sources(IsoRecorder PRIVATE resources/app.manifest)

enable_testing()
add_subdirectory(tests)
include(cmake/deploy.cmake)
```

`CMakePresets.json` – presety `msvc-debug`, `msvc-release` (generator Ninja, `CMAKE_PREFIX_PATH` = Qt).

### 9.2 main.cpp – inicjalizacja

```cpp
int main(int argc, char* argv[]) {
    // 1. Relokowalny GStreamer: pluginy z katalogu aplikacji, rejestr w LOCALAPPDATA
    const auto appDir = std::filesystem::path(argv[0]).parent_path();
    _wputenv_s(L"GST_PLUGIN_SYSTEM_PATH_1_0", (appDir / L"gstreamer-1.0").c_str());
    _wputenv_s(L"GST_PLUGIN_SCANNER_1_0", (appDir / L"gst-plugin-scanner.exe").c_str());
    _wputenv_s(L"GIO_MODULE_DIR", (appDir / L"gio" / L"modules").c_str());   // TLS dla WHIP/WHEP/RTSPS
    // GST_REGISTRY_1_0 -> %LOCALAPPDATA%\IsoRecorder\gst-registry.bin

    // 2. Proces: priorytet, wyłączenie EcoQoS/power throttling
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    PROCESS_POWER_THROTTLING_STATE pt{PROCESS_POWER_THROTTLING_CURRENT_VERSION,
        PROCESS_POWER_THROTTLING_EXECUTION_SPEED, 0};
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &pt, sizeof(pt));

    gst_init(&argc, &argv);
    QApplication app(argc, argv);
    // 3. Wspólny zegar dla wszystkich kanałów
    GstClock* clock = gst_system_clock_obtain();
    g_object_set(clock, "clock-type", GST_CLOCK_TYPE_MONOTONIC, nullptr);
    ChannelManager manager(clock);
    MainWindow w(&manager);
    w.show();
    int rc = app.exec();
    manager.shutdownAll(std::chrono::seconds(15)); // finalizacja plików
    gst_object_unref(clock);
    gst_deinit();
    return rc;
}
```

### 9.3 Mostek busa → Qt

```cpp
static GstBusSyncReply busSync(GstBus*, GstMessage* msg, gpointer user) {
    auto* ch = static_cast<Channel*>(user);
    gst_message_ref(msg);
    QMetaObject::invokeMethod(ch, [ch, msg] {
        ch->handleBusMessage(msg);   // ERROR, WARNING, EOS, ELEMENT(splitmuxsink-fragment-closed), STATE_CHANGED
        gst_message_unref(msg);
    }, Qt::QueuedConnection);
    return GST_BUS_DROP;
}
gst_bus_set_sync_handler(bus, busSync, channel, nullptr);
```

Uwaga: `handleBusMessage` nie może wykonywać blokujących `gst_element_set_state` w GUI – zleca je do wątku kontrolnego kanału.

### 9.4 Preview przez appsink (domyślnie)

```cpp
// ... ! tee ! queue max-size-buffers=2 leaky=downstream ! d3d11scale ! d3d11convert
//     ! video/x-raw(memory:D3D11Memory),width=480,height=270 ! d3d11download
//     ! videorate drop-only=true ! video/x-raw,format=BGRx,framerate=15/1
//     ! appsink name=preview sync=false max-buffers=1 drop=true emit-signals=false
GstAppSinkCallbacks cb{};
cb.new_sample = [](GstAppSink* s, gpointer user) -> GstFlowReturn {
    GstSample* sample = gst_app_sink_pull_sample(s);
    auto img = toQImage(sample);              // gst_video_frame_map → QImage::Format_RGB32 → copy()
    gst_sample_unref(sample);
    static_cast<PreviewSink*>(user)->publish(std::move(img)); // atomowa podmiana + update() w GUI
    return GST_FLOW_OK;
};
gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &cb, this, nullptr);
```

Alternatywa dla pełnoekranowego monitora jednego kanału: `d3d11videosink` + `gst_video_overlay_set_window_handle(sink, (guintptr)widget->winId())` (widget z `Qt::WA_NativeWindow`, `WA_PaintOnScreen`) – patrz problemy 12.x (airspace).

### 9.5 RecordBin – start, pauza, split, stop

**Start**: zbudować bin, `gst_bin_add` do pipeline'u, `gst_element_sync_state_with_parent`, podłączyć do `tee` (request pad `src_%u`). Dla trybu passthrough: probe odrzucający bufory do pierwszej klatki kluczowej (`!GST_BUFFER_FLAG_DELTA_UNIT`). Dla trybu z enkoderem: wysłać `force-key-unit`.

**Pauza** (ciągła oś czasu w pliku):

```cpp
// Probe na ghost sink padach RecordBin (wideo i audio)
GstPadProbeReturn RecordBin::onBuffer(GstPad* pad, GstPadProbeInfo* info) {
    auto* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    const GstClockTime pts = GST_BUFFER_PTS(buf);
    std::scoped_lock lk(m_mtx);
    if (m_paused) {
        if (!GST_CLOCK_TIME_IS_VALID(m_pauseStartPts)) m_pauseStartPts = pts;
        return GST_PAD_PROBE_DROP;
    }
    if (GST_CLOCK_TIME_IS_VALID(m_pauseStartPts)) {         // pierwsze wznowienie
        m_pausedTotal += pts - m_pauseStartPts;
        m_pauseStartPts = GST_CLOCK_TIME_NONE;
        gst_pad_set_offset(m_videoSink, -(gint64)m_pausedTotal);
        gst_pad_set_offset(m_audioSink, -(gint64)m_pausedTotal);
        requestKeyframe();                                   // force-key-unit do enkodera
    }
    return GST_PAD_PROBE_OK;
}
```

Wspólny `m_pausedTotal` liczony z **czasu bieżącego pipeline'u** (`gst_clock_get_time - base_time`), nie osobno per pad, aby audio i wideo dostały identyczny offset (inaczej dryf A/V).

**Split**:

```cpp
void RecordBin::split() {
    // 1) wymuś klatkę kluczową, aby cięcie nastąpiło natychmiast
    GstEvent* fku = gst_video_event_new_upstream_force_key_unit(GST_CLOCK_TIME_NONE, TRUE, ++m_fkuCount);
    gst_pad_send_event(m_encoderSrcPad, fku);
    // 2) splitmuxsink zamknie bieżący plik na najbliższej klatce kluczowej
    g_signal_emit_by_name(m_splitmux, "split-now");   // lub "split-after" dla trybu passthrough
}
```

Nazwy plików: sygnał `format-location-full` splitmuxsink → zwraca ścieżkę wygenerowaną z szablonu (UTF-8) i inkrementuje `{part}`. Zamknięcie części: wiadomość elementu `splitmuxsink-fragment-closed` (zawiera `location`, `running-time`) → wpis do sidecar i UI.

**Stop** (finalizacja bez zatrzymywania preview):

```cpp
void RecordBin::stop() {
    // zablokuj pady tee → odłącz → wyślij EOS do binu → czekaj na EOS z splitmuxsink
    gst_pad_add_probe(m_teeVideoPad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, &RecordBin::unlinkCb, this, nullptr);
    gst_pad_add_probe(m_teeAudioPad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, &RecordBin::unlinkCb, this, nullptr);
}
// unlinkCb: gst_pad_unlink(teePad, binSink); gst_pad_send_event(binSink, gst_event_new_eos());
// bin z message-forward=true → GstBinForwarded(EOS) z splitmuxsink → w wątku kontrolnym:
// gst_element_set_state(bin, GST_STATE_NULL); gst_bin_remove(pipeline, bin); release tee request pads.
// Timeout bezpieczeństwa (np. 10 s) → wymuszone NULL + oznaczenie pliku do naprawy.
```

### 9.6 Synchroniczne Rec/Split wielu kanałów

1. Wszystkie pipeline'y używają tego samego `GstClock` (`gst_pipeline_use_clock`) i tego samego `base-time` (`gst_element_set_base_time` + `gst_element_set_start_time(GST_CLOCK_TIME_NONE)`) ustawionego przy starcie preview grupy.
2. Komenda grupowa wyznacza **docelowy running-time** `T = now + 200 ms`.
3. Każdy RecordBin w probe zaczyna przepuszczać bufory dopiero od pierwszej klatki z `running_time ≥ T` (dla split: `split-at-running-time` w splitmuxsink lub `split-now` wysyłany przy przekroczeniu `T`).
4. Wynik: pliki wszystkich kanałów zaczynają się w tej samej klatce (±1 klatka przy różnych fps), dodatkowo zapis timecode RTC (`timecodestamper source=rtc`) i czasu startu w sidecar JSON.

### 9.7 Wybór enkodera z fallbackiem

```cpp
GstElement* EncoderFactory::createH264(const VideoCfg& c) {
    for (auto name : {"nvh264enc", "qsvh264enc", "amfh264enc", "mfh264enc", "openh264enc"}) {
        if (!c.allows(name)) continue;
        if (GstElement* e = gst_element_factory_make(name, nullptr)) {
            if (probeOpen(e, c)) { applyRateControl(e, c); return e; } // krótki test READY/PAUSED → wykrywa limit sesji
            gst_object_unref(e);
        }
    }
    return nullptr;
}
```

Mapowanie właściwości (bitrate, gop-size, rc-mode, b-frames) różni się między enkoderami – trzymać w tabeli per-fabryka.

### 9.8 Usuwanie i przenoszenie kanału

- **Usuń**: jeśli nagrywa → dialog „Zatrzymać i usunąć?” → `stop()` → po finalizacji `pipeline → NULL`, odłączenie sync-handlera busa, `gst_object_unref`, `deleteLater()` widgetu; zwolnienie portów (SRT/WHIP) i urządzeń.
- **Przenieś**: zmiana tylko kolejności w modelu (`ChannelManager::move(from, to)`) + przebudowa layoutu; pipeline nie jest restartowany. `{index}` w nazwie pliku aktualizowany przy następnym pliku.

---

## 10. Kompilacja, pakowanie, instalator

### 10.1 Kompilacja

Z „x64 Native Tools Command Prompt for VS 2022” (lub `Enter-VsDevShell`):

```powershell
cmake --preset msvc-release
cmake --build --preset msvc-release
ctest --preset msvc-release --output-on-failure
```

Bez presetów:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64"
cmake --build build --parallel
```

Debugowanie GStreamera: `set GST_DEBUG=2,splitmuxsink:5,ndisrc:4` oraz `GST_DEBUG_DUMP_DOT_DIR=C:\tmp\dot` (+ `gst_debug_bin_to_dot_file` z menu „Diagnostyka”).

### 10.2 Deploy (relokowalna paczka)

1. `windeployqt --release --no-translations --compiler-runtime build\IsoRecorder.exe --dir dist`
2. Skopiować z `%GSTREAMER_1_0_ROOT_MSVC_X86_64%`:
   - `bin\*.dll` → `dist\` (glib, gobject, gstreamer, orc, libnice, openssl, srt, ffmpeg-libav itd.),
   - `bin\gst-plugin-scanner.exe` → `dist\` (lub `libexec\gstreamer-1.0\`),
   - **wybrane** pluginy `lib\gstreamer-1.0\*.dll` → `dist\gstreamer-1.0\` (lista whitelist w `cmake/deploy.cmake`: coreelements, app, videoconvertscale, audioconvert, audioresample, level, d3d11, d3d12, nvcodec, qsv, amfcodec, mediafoundation, wasapi2, decklink, ndi, srt, rtsp, rtp, rtpmanager, webrtc, rswebrtc, webrtchttp, nice, dtls, srtp, sctp, fallbackswitch, matroska, isomp4, fmp4, mpegtsmux, mpegtsdemux, videoparsersbad, audioparsers, libav, openh264, opus, timecode, …),
   - `lib\gio\modules\gioopenssl.dll` → `dist\gio\modules\` (HTTPS/TLS).
3. Uruchomić `gst-inspect-1.0` z `dist` (z ustawionym `GST_PLUGIN_SYSTEM_PATH_1_0`) w CI, aby zweryfikować, że wszystkie wymagane elementy się ładują.
4. NDI: nie dołączać runtime bez umowy licencyjnej NDI – instalator sprawdza obecność `Processing.NDI.Lib.x64.dll` (zmienna `NDI_RUNTIME_DIR_V6`) i proponuje pobranie NDI Runtime.

Alternatywa prostsza: wymagać zainstalowanego systemowo GStreamer Runtime MSI (instalator łańcuchowo instaluje MSI) – mniej plików, ale ryzyko konfliktu wersji z innymi aplikacjami.

### 10.3 Instalator i podpis

- Inno Setup: instalacja do `Program Files\IsoRecorder`, skróty, skojarzenie `.isorec.json`, reguły zapory (`netsh advfirewall firewall add rule … program=IsoRecorder.exe`), VC++ Redist, opcjonalnie NDI Runtime (link).
- Podpis Authenticode (`signtool sign /fd SHA256 /tr <timestamp> /td SHA256`) dla EXE, DLL i instalatora – ogranicza SmartScreen i fałszywe alarmy AV.
- Crash dumps: `SetUnhandledExceptionFilter` + `MiniDumpWriteDump` do `%LOCALAPPDATA%\IsoRecorder\dumps`; zachować PDB z buildów release.

### 10.4 CI

GitHub Actions/Azure (runner `windows-2022`): instalacja Qt (`jurplel/install-qt-action`), GStreamer MSI (cicho: `msiexec /i … /qn ADDLOCAL=ALL`), build, testy z `videotestsrc`, deploy, artefakt instalatora.

---

## 11. Wymagania niefunkcjonalne

| Obszar | Wymaganie |
|--------|-----------|
| Stabilność | Praca ciągła ≥ 72 h z 16 kanałami; brak wzrostu pamięci > 5% po 24 h; wyciek uchwytów = 0. |
| Odporność | Awaria jednego wejścia nie wpływa na inne kanały; zanik sieci → reconnect, nagranie trwa (obraz zastępczy) lub automatyczny split. |
| Integralność | Plik zawsze odtwarzalny po awarii zasilania (MKV/TS/fMP4); utrata maks. ostatnich ~2 s. |
| Wydajność | 16 × 1080p50 H.264 HW: CPU < 50% na profilu zalecanym; brak dropped frames przy obciążeniu UI. |
| Latencja preview | < 300 ms dla wejść lokalnych; preview nie może blokować nagrywania (leaky queue). |
| Synchronizacja | A/V w pliku ±1 klatka; start grupowy ±1 klatka między kanałami. |
| UI | Responsywne (< 100 ms reakcji na przycisk); High-DPI PerMonitorV2; ciemny motyw; pełna obsługa klawiaturą. |
| Monitoring | Wolne miejsce + prognoza czasu nagrania; alarm < 10% / < 30 min; automatyczny stop przy < 1 GB. |
| Logi | Rotowane pliki (`%LOCALAPPDATA%\IsoRecorder\logs`), poziomy, log zdarzeń nagrania per kanał (start/pauza/split/stop z running-time i czasem ściennym). |
| Bezpieczeństwo | Hasła w Credential Manager, TLS dla WHIP/WHEP/RTSPS, token na endpoint WHIP, brak telemetrii zewnętrznej bez zgody. |
| Lokalizacja | PL/EN (Qt Linguist), ścieżki Unicode (znaki PL). |

---

## 12. Potencjalne problemy i rozwiązania

| # | Problem | Objaw | Rozwiązanie |
|---|---------|-------|-------------|
| 1 | **Limit sesji NVENC** na kartach GeForce (obecnie 8 równoczesnych) | 9. kanał nie startuje, błąd inicjalizacji enkodera | Karta profesjonalna (RTX A/Ada – bez limitu) lub rozkład: część kanałów na Intel QSV/AMF; `EncoderFactory` z fallbackiem; passthrough dla źródeł już skompresowanych. |
| 2 | Przepustowość / latencja dysku | Rosnące kolejki, dropped frames, przerwy | Osobny dysk NVMe na nagrania, duże `queue` przed muxerem (`max-size-time=3s`), monitor wolnego miejsca; wyłączyć indeksowanie Windows Search dla folderu; nie nagrywać na HDD/USB/sieć bez bufora. |
| 3 | **Uszkodzony MP4 po awarii** (brak `moov`) | Plik nieodtwarzalny | Domyślnie MKV/TS; dla MP4: `qtmux` z `reserved-max-duration` + `reserved-moov-update-period` lub fragmented MP4 (`isofmp4mux`); narzędzie naprawy (remux `ffmpeg -c copy`, `untrunc`). |
| 4 | Pauza tworzy „dziurę” w timestampach | W pliku czarny fragment / zamrożenie, dryf A/V | Pad offset liczony wspólnie dla A i V (9.5); po wznowieniu keyframe; w passthrough czekanie na keyframe. |
| 5 | Split gubi lub dubluje klatki | Brak ciągłości między częściami | `splitmuxsink` tnie tylko na keyframe; wymuszenie `force-key-unit` przed `split-now`; GOP ≤ 1 s; test ciągłości PTS w `ffprobe`. W passthrough split nastąpi na najbliższym IDR źródła (opóźnienie = GOP źródła – informować użytkownika). |
| 6 | Dryf A/V przy osobnym urządzeniu audio (UVC + WASAPI) | Audio rozjeżdża się po godzinach | Zegary różnych urządzeń są niezależne – `audiobasesrc` slave-method `resample` (`wasapi2src` + `audioresample`), stały offset A/V konfigurowalny; preferować audio osadzone w HDMI. |
| 7 | Różne zegary źródeł sieciowych (NDI/SRT/RTSP) | Wolne narastanie opóźnienia, przepełnienie kolejek | Tryb live z `sync=false` na sinkach nagrywania, `rtpjitterbuffer`/latency w SRT, `videorate`/`audiorate` do stałego fps, monitorowanie poziomu kolejek. |
| 8 | **Utrata sygnału / restart źródła** | Pipeline przechodzi w ERROR, nagranie przerwane | `fallbacksrc` (timeout, restart-timeout, fallback image) dla sieci; dla HDMI – obsługa „no signal” DeckLink (klatki zastępcze), polityka: kontynuuj / pauza / split po powrocie sygnału. |
| 9 | Zmiana rozdzielczości/fps źródła w trakcie | Błąd negocjacji caps, crash enkodera | Normalizacja przed `tee` (`videoscale`/`d3d11scale` + `videorate` do formatu docelowego) lub automatyczny split z rekonfiguracją enkodera przy nowym caps. |
| 10 | Wydajność 16 podglądów | Wysokie CPU, lagujący UI | Skalowanie w GPU (`d3d11scale`) do ≤ 480×270, 10–15 fps, `leaky` queue, `max-buffers=1 drop=true`; rysowanie tylko widocznych kart; podgląd off dla zminimalizowanego okna. |
| 11 | **Airspace** przy natywnym oknie wideo (overlay HWND) | Migotanie, artefakty przy przewijaniu/drag & drop, okna nad dialogami | Domyślnie appsink → `QImage`; overlay tylko w trybie pełnoekranowym jednego kanału. |
| 12 | Zmiany stanu GStreamera z niewłaściwego wątku | Zawieszenia (deadlock), crash | Nigdy `set_state` w callbacku probe/streamingu; dedykowany wątek kontrolny per kanał; `gst_element_call_async` gdy trzeba z wątku streamingu. |
| 13 | Usuwanie kanału / zamknięcie aplikacji w trakcie nagrania | Niesfinalizowane pliki, wycieki | Ścieżka „graceful stop” z timeoutem; `closeEvent` blokuje zamknięcie do finalizacji (z paskiem postępu); RAII i licznik żywych obiektów w debug. |
| 14 | **Uśpienie systemu / wygaszacz / Windows Update** | Nagranie przerwane, restart w nocy | `SetThreadExecutionState(ES_CONTINUOUS|ES_SYSTEM_REQUIRED)` podczas nagrań i oczekiwania na harmonogram; instrukcja: plan zasilania „Wysoka wydajność”, godziny aktywności / LTSC, wyłączenie uśpienia USB/PCIe (ASPM). |
| 15 | Power throttling / EcoQoS, niski priorytet w tle | Dropy, gdy okno zminimalizowane | `SetProcessInformation(ProcessPowerThrottling)` (wyłączenie), `ABOVE_NORMAL_PRIORITY_CLASS`, MMCSS („Capture”) dla wątków przechwytywania (WASAPI robi to sam). |
| 16 | Zapora / sieć dla NDI, SRT, WHIP | Brak źródeł NDI w discovery, brak połączeń | Reguły zapory w instalatorze; NDI Discovery Server (opcjonalny adres w ustawieniach), ręczne IP; dla WHIP konfigurowalny zakres portów UDP i STUN/TURN. |
| 17 | WebRTC: NAT, TLS, certyfikaty | WHEP/WHIP nie łączy się | Dołączyć `gioopenssl` (GIO modules) i certyfikaty CA; obsługa TURN; tryb lokalny bez TLS tylko w LAN; logi ICE (`GST_DEBUG=webrtcbin:5,nice:4`). |
| 18 | Znaki PL, długie ścieżki | Błąd otwarcia pliku | Przekazywać ścieżki UTF-8 (`QString::toUtf8()`) – GLib na Windows konwertuje do UTF-16; manifest `longPathAware`, sanityzacja nazw, limit długości składowej 255. |
| 19 | Screen capture: HDR, DRM, zabezpieczone okna, zmiana monitora | Czarny obraz, wyprane kolory, błąd po odłączeniu monitora | WGC dla okien, DXGI dla monitorów; konwersja HDR→SDR (tonemapping w `d3d11convert`/opcja); okna z `WDA_EXCLUDEFROMCAPTURE`/DRM nie da się nagrać – informacja w UI; reakcja na `WM_DISPLAYCHANGE` (restart źródła). |
| 20 | Windows Defender skanuje zapisywane pliki | Wysokie IO/CPU, mikroprzycięcia | Rekomendacja wykluczenia folderu nagrań (instrukcja dla admina; aplikacja nie zmienia ustawień bezpieczeństwa sama). |
| 21 | Konflikty DLL (inne instalacje GStreamera/Qt w PATH) | Crash przy starcie, „entry point not found” | Paczka relokowalna, `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)`, własny rejestr pluginów, ignorowanie systemowego `GST_PLUGIN_PATH`. |
| 22 | Pierwsze uruchomienie – długi skan pluginów | Start trwa > 10 s | Whitelist pluginów w paczce, rejestr w `LOCALAPPDATA`, splash z informacją; wygenerowanie rejestru w instalatorze. |
| 23 | **Licencje** | Ryzyko prawne | Qt i GStreamer LGPL – linkowanie dynamiczne, udostępnienie informacji/źródeł zmian LGPL; nie dołączać x264/x265 (GPL) – używać HW/OpenH264 (Cisco, binarka z licencją patentową); HEVC/AAC – kwestie patentowe (HW enkodery/MF systemowe); NDI – zgodnie z NDI SDK License (logo/nazwa, redystrybucja runtime); ProRes przez FFmpeg `prores_ks` – brak oficjalnej licencji Apple (zaznaczyć w dokumentacji). |
| 24 | Synchronizacja czasu między kanałami i z innymi rejestratorami | Trudny montaż multicam | Wspólny zegar i base-time (9.6), timecode RTC/RP188, sidecar JSON z czasem startu (UTC) i running-time; synchronizacja zegara systemowego przez NTP/PTP. |
| 25 | Brak lub zmiana urządzenia (USB odłączone, inny numer DeckLink) | Kanał w Error po wczytaniu listy | Identyfikacja urządzeń po stabilnym ID (`device-path`, numer seryjny/`persistent-id` DeckLink) zamiast indeksu; `GstDeviceMonitor` – hot-plug i automatyczny restart kanału. |
| 26 | Za mało miejsca w trakcie nagrania | Błąd zapisu, uszkodzony plik | Monitor co 2 s, ostrzeżenia, przy progu krytycznym kontrolowany Stop (finalizacja) wszystkich kanałów na danym wolumenie; opcja folderu zapasowego. |
| 27 | Duże pliki i system plików | FAT32 limit 4 GB, exFAT podatny na awarie | Wymagać NTFS/ReFS; ostrzeżenie dla FAT32; auto-split po rozmiarze. |
| 28 | Scheduler a zmiana czasu (DST), przestawienie zegara, sen | Pominięte lub podwójne zdarzenia | Obliczenia w `QDateTime` ze strefą, tick kontrolny co 1 s, zapamiętanie ostatnio wykonanego zdarzenia (idempotencja), polityka catch-up. |
| 29 | Kilka kanałów z tego samego urządzenia/źródła | Błąd „device busy” (UVC/WASAPI exclusive) | Wspólny „source hub” – jedno źródło, wiele konsumentów (`intervideosink/intervideosrc` lub rs `intersink/intersrc`); WASAPI w trybie shared. |
| 30 | Niekontrolowany wzrost kolejek przy przeciążeniu | Rosnąca pamięć, OOM po godzinach | Limity `queue` (`max-size-time`, `leaky` tylko w gałęzi preview – **nigdy** w gałęzi nagrywania), alarm przy zapełnieniu > 80%, statystyki w UI. |

---

## 13. Testy i kryteria akceptacji

### 13.1 Testy jednostkowe (Qt Test, CTest)

- `ChannelConfig`: serializacja ↔ deserializacja, migracja wersji, walidacja (16 kanałów, duplikaty portów).
- `FileNameTemplate`: tokeny, sanityzacja, kolizje, znaki PL.
- `Scheduler`: reguły jednorazowe/cykliczne, DST (np. 30.03 i 26.10), catch-up, idempotencja.
- `ChannelStateMachine`: dozwolone/niedozwolone przejścia, operacje grupowe.

### 13.2 Testy integracyjne pipeline'ów (bez sprzętu)

- `videotestsrc is-live=true pattern=ball` + `audiotestsrc wave=ticks` (tick co 1 s ułatwia weryfikację A/V).
- Scenariusze: Rec 10 s → Stop; Rec → Pause 5 s → Resume → Stop (oczekiwany czas pliku = czas bez pauzy); Rec → 5 × Split (suma długości części = czas nagrania, brak luk PTS); 16 kanałów równocześnie.
- Weryfikacja automatyczna: `ffprobe -v error -show_entries format=duration -show_frames -select_streams v` → ciągłość PTS, liczba klatek, pierwsza klatka = keyframe.

### 13.3 Testy sprzętowe / sieciowe

- Każdy typ wejścia z rzeczywistym źródłem (DeckLink, UVC, NDI z NDI Test Patterns, SRT z `ffmpeg -f lavfi … srt://`, RTSP z MediaMTX, WHIP z OBS → aplikacja, WHEP z MediaMTX → aplikacja, screen grab monitora i okna).
- Zrywanie połączenia (odłączenie kabla, restart nadajnika) → reconnect bez utraty pliku.
- Odłączenie zasilania w trakcie nagrania → pliki MKV/TS odtwarzalne.

### 13.4 Testy obciążeniowe

- 16 × 1080p50 przez 24 h (docelowo 72 h): monitorowanie RAM, uchwytów (Process Explorer), dropped frames, temperatury GPU.
- 100 cykli Rec/Stop i 1000 splitów na kanał – brak wycieków.

### 13.5 Checklist akceptacyjny funkcji

- [ ] Dodanie kanałów 1…16; przycisk Add source nieaktywny przy 16.
- [ ] Każdy kanał ma preview, VU, status.
- [ ] Zakładka Input: HDMI, NDI, SRT, RTSP, WebRTC WHIP i WHEP, Screen grab, audio – każda działa.
- [ ] Zakładka Output: folder, szablon nazwy, kontener, kodeki, auto-split, passthrough.
- [ ] Zakładka Scheduler: start/stop/split jednorazowo i cyklicznie.
- [ ] Przyciski kanału: Rec, Pause/Resume, Stop, Split, Usuń.
- [ ] Pasek: Rec/Pause/Stop/Split dla zaznaczonych, Select all, Add source.
- [ ] Przenoszenie kanałów (drag & drop i skróty), kolejność zachowana w zapisie.
- [ ] Zapis i wczytanie listy (JSON), autosave, przywracanie po awarii.
- [ ] Instalator działa na czystym Windows 10 LTSC / 11 bez zainstalowanego Qt/GStreamera.

---

### Kolejność prac w skrócie

1. Zainstaluj narzędzia (rozdz. 4) i sprawdź pluginy `gst-inspect-1.0`.
2. Zbuduj szkielet M1 i paczkę deploy od pierwszego dnia (wcześnie wykrywa problemy z DLL).
3. Zaimplementuj pełny cykl nagrania na `videotestsrc` (M2–M4) zanim dodasz prawdziwe wejścia.
4. Skaluj do 16 kanałów (M5), potem wejścia (M6), UI konfiguracji i scheduler (M7).
5. Hardening, testy 24 h, instalator, podpis (M8).
