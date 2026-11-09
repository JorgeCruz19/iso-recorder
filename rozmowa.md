# Rozmowa — ISO Recorder

- Data: 2026-11-09
- Katalog: `D:\opencode\iso recorder`
- Sesja: `ses_e440cc9afffeEKw48hyiBp1Hae`
- Zadanie: ISO Recorder plan

Zapis obejmuje treść poleceń użytkownika i odpowiedzi asystenta. Pominięto wstrzykiwane przez środowisko bloki systemowe (instrukcje harnessu, katalog narzędzi, tryb Plan), które nie są częścią rozmowy merytorycznej.

---

## 1. Użytkownik

> zapoznaj się z instrukcją w pliku instrukcja.md i przedstaw swoje propozycje.

**Kontekst wejściowy (pliki w katalogu):**

- `instrukcja.md` — 813 linii, specyfikacja aplikacji „Video ISO Recorder”: C++20, MSVC 2022 x64, Qt 6.8 LTS Widgets, GStreamer 1.26.x MSVC + gst-plugins-rs, CMake + Ninja, Inno Setup.
- `mockup/interfejs.html` — makieta UI 1920×1080, ciemny motyw, siatka 4×4 kanałów + prawy inspector + dolny pasek transportu.
- `interfejs.png` — zrzut makiety.
- `media/` — 16 zdjęć używanych jako klatki podglądu w makiecie.

Asystent przeczytał `instrukcja.md` w całości (rozdz. 1–13), przejrzał `mockup/interfejs.html` i `interfejs.png`.

---

## 2. Asystent

Dokument przeczytany. Wybór technologii (Qt 6 Widgets + GStreamer + C++20) trafny — nie zmieniałbym. Zakres w dokumencie to jednak 4–6 miesięcy dla jednej osoby, więc proponuję cięcie na pionowe wycinki i dowód działania na prawdziwym sprzęcie przed pisaniem 16 kanałów.

### Co bym zmienił w instrukcji

1. **Pauza** — najsłabszy punkt (9.5). `gst_pad_set_offset` na padach sink + drop w probe działa na papierze, ale w passthrough (skompresowany strumień) i przy enkoderze może dać niemonotoniczne PTS i zatrzymanie muxera. Propozycja: MVP bez pauzy, potem pauza jako „zamknij część → wznów nową część” (`{part}` już jest w szablonie). Prawdziwą pauzę z ciągłą osią czasu dopiero po teście PTS w `ffprobe`.
2. **WebRTC WHIP jako serwer** (`whipserversrc`) — młody plugin, dużo ryzyka, mało wartości na start. Taniej: MediaMTX jako gateway (WHIP/WHEP/SRT/RTSP wchodzi do aplikacji jako SRT/RTSP). Do aplikacji zostaje jeden typ wejścia sieciowego.
3. **Pause/Split/Stop a synchronizacja** — instrukcja mówi „start grupowy ±1 klatka” i wspólny base-time. Przy 16 pipeline'ach różne źródła mają różne zegary; ±1 klatka realnie wychodzi tylko przy `testsrc`. Dla prawdziwych źródeł obiecywałbym ±1 klatkę *w obrębie jednego źródła*, a między kanałami synchronizację po timecode RTC w sidecarze.
4. **Enkodery** — kolejność NVENC → QSV → AMF → MF → OpenH264 dobra, ale `amfh264enc` i `qsvh264enc` wymagają konkretnej negocjacji pamięci (D3D11) i psują się przy skalowaniu. `probeOpen()` z dok. tego nie wykryje. Potrzebna macierz „enkoder × rozdzielczość × format pamięci” testowana na sprzęcie, nie lista nazw.
5. **Deploy od dnia 1** — zgoda, to najważniejsze zdanie w dokumencie. Dodam tylko: przed M1 uruchomić `gst-inspect-1.0` na docelowej maszynie i spisać, które z 15 wymaganych elementów faktycznie się ładują (`ndisrc`, `whepclientsrc`, `whipserversrc`, `d3d11jpegdec`, `fallbacksrc`). To 30 minut, a przesądza M6.

### Proponuję

- **MVP**: 4 kanały, wejścia UVC/MF + screen grab + testsrc, WASAPI, MKV/TS, H.264 HW, Rec/Stop/Split, JSON + autosave, monitor dysku. Bez DeckLink, NDI, SRT, RTSP, WebRTC, schedulera, ProRes.
- **Kolejność**: najpierw sprawdzian środowiska i szkielet z paczką relokowalną, potem pełny cykl nagrania na `testsrc`, dopiero potem jedno prawdziwe wejście (UVC) — zgodnie z dok., tylko wcześniej sprawdzone.
- **Testy od P2, nie od M8**: `ffprobe` jako asercja (czas trwania, ciągłość PTS, pierwsza klatka = keyframe), pętla 100× Rec/Stop na wycieki.
- **UI**: mockup `interfejs.html` traktuję jako specyfikację wiążącą. Uwaga: chipy na klatce (format, timecode, VU) nie wyjdą z QSS — trzeba własnego malowania w `paintEvent` kafla. To osobny, realny koszt w P5.
- **Repozytorium**: katalog nie jest repo git. Proponuję `git init` + `.gitignore` (build, dist, *.isorec.json) jako pierwszy krok.

### Pytania

1. Jaki sprzęt jest do testów: DeckLink? NVIDIA (jaki model)? Intel iGPU? Ile wejść HDMI fizycznie?
2. Pierwsza wersja ma obsłużyć tylko UVC + screen, czy od razu któreś źródło sieciowe (SRT albo NDI)?
3. To prototyp do walidacji, czy produkt z terminem? Termin?
4. Mockup jest wiążący w 100% (ciemny motyw, 1920×1080, siatka 4×4), czy tylko kierunek?

Nie sprawdziłem: dostępności pluginów GStreamera na tej maszynie ani wersji Qt/MSVC — to pierwszy punkt sprawdzianu środowiska. Ryzyko: jeśli docelowa maszyna nie ma NVENC albo DeckLinka, zakres M6 i profil 16 kanałów trzeba przeliczyć.

### Załącznik: strona z planem prac (renderowana w czacie)

Strona pokazywała podział MVP / poza MVP, tabelę etapów P0–P7 z czasami oraz listę ryzyk do zamknięcia wcześnie. Treść stronu poniżej w formie tekstowej.

**MVP — dowód, że działa (≈5–6 tyg.)**

- 4 kanały (limit w kodzie 16)
- UVC/MF, screen grab, testsrc
- WASAPI embedded + shared
- MKV/TS · H.264 HW (NVENC→QSV→MF)
- Rec / Stop / Split
- JSON lista + autosave
- monitor dysku, logi

**Poza MVP**

- DeckLink, NDI, SRT, RTSP
- WebRTC WHIP/WHEP
- Pause z ciągłą osią czasu
- ProRes / DNx / AV1 / passthrough
- Scheduler, 16 kanałów, sync start
- Instalator, podpis, crash recovery

**Etapy**

| Etap | Zakres | Czas |
|------|--------|------|
| P0 | Szkielet CMake/Qt + GStreamer · **relokowalna paczka od dnia 1** | 2–3 d |
| P1 | Preview appsink→QImage, VU z `level` | 2–3 d |
| P2 | RecordBin: Rec/Stop → MKV, finalizacja, testy ffprobe | 3–5 d |
| P3 | Split (manual + auto), PTS continuity test | 3–5 d |
| P4 | Wiele kanałów, operacje grupowe, zapis/wczytanie JSON | 5–7 d |
| P5 | UI z mockupu: siatka, inspector, drag&drop | 5–7 d |
| P6 | Realne wejścia: UVC/WASAPI/screen → DeckLink → sieć | 2–3 tyg. |
| P7 | Scheduler, hardening, instalator, test 24 h | 1–2 tyg. |

Czas dla jednej osoby wspieranej AI; bez czasu na zakup sprzętu i testy z prawdziwymi źródłami.

**Ryzyka, które trzeba zamknąć wcześnie**

| Ryzyko | Uwaga |
|--------|-------|
| Deploy GStreamera | P0 — paczka musi działać na czystym Windows bez Qt/GST w PATH |
| Pause z „ciągłą” osią czasu | najtrudniejszy fragment; test PTS przed obietnicą funkcji |
| Limit sesji NVENC (8) | decyzja sprzętowa, nie kod |
| WHIP server (`whipserversrc`) | młody plugin; alternatywa: MediaMTX jako gateway |
| Zapis intra (ProRes 16×) | wymaga NVMe RAID — inaczej odciąć w UI |

---

## 3. Użytkownik

> zapisz całą tę rozmowę do pliku.

**Asystent:** zapisano do `rozmowa.md` (ten plik). Odpowiedź na to polecenie jest techniczna i nie wnosi treści merytorycznej, więc nie jest tu powtarzana.
