# ISO Recorder

Program dla Windows do równoczesnego nagrywania od 1 do 16 kanałów wideo z dźwiękiem — każdy kanał do własnego pliku
(„ISO recording”). Przydaje się przy realizacji wielokamerowej, rejestracji konferencji i streamów oraz wszędzie tam,
gdzie materiał z każdego źródła ma zostać w całości do późniejszego montażu.

![ISO Recorder — 12 kanałów w trakcie nagrywania](docs/zrzut_2.0.png)

## Co potrafi

**Źródła**
- ekran lub pojedyncze okno programu,
- kamera USB / karta przechwytująca widoczna w systemie jako kamera,
- strumienie sieciowe SRT i RTSP,
- dźwięk systemowy, mikrofon lub wejście liniowe,
- generator testowy (obraz i ton) do prób.

**Nagrywanie**
- H.264 + AAC do plików MKV albo MPEG‑TS; enkoder sprzętowy wybierany automatycznie
  (NVIDIA NVENC, Intel Quick Sync, AMD AMF), z zapasowym programowym,
- pauza i wznowienie w tym samym pliku, bez śladu w osi czasu,
- podział na części ręcznie albo automatycznie (co N minut / co N GB),
- REC, PAUSE i STOP dla grupy kanałów w tej samej chwili — pliki mają wspólny punkt zero,
- plik opisu `.json` obok każdego nagrania (czasy, części, pauzy, zaniki sygnału) do synchronizacji w montażu.

**Wyjście na żywo**
- każdy kanał może równolegle do nagrania wychodzić na NDI albo SRT; można to włączać i wyłączać w trakcie nagrywania.

**Odporność**
- zanik sygnału sieciowego nie przerywa nagrania; do wyboru: nagrywaj dalej, pauza albo nowa część pliku,
- źródło, które padło, jest uruchamiane ponownie automatycznie, a przerwane nagrywanie wznawia się w nowym pliku,
- nadzór wolnego miejsca na każdym dysku z osobna i test prędkości zapisu,
- autozapis listy kanałów i przywracanie po awarii,
- log zdarzeń w pliku.

**Interfejs**
- widok kafli (2×2, 3×3, 4×4) i widok listy, duży podgląd kanału, mierniki dźwięku,
- listy kanałów zapisywane do pliku i wczytywane,
- języki: angielski, polski, niemiecki, hiszpański, włoski, francuski.

## Wymagania

- Windows 10 (wersja 1903 lub nowsza) albo Windows 11, 64 bity,
- karta graficzna z enkoderem H.264 (zalecane); bez niej działa wolniejszy enkoder programowy,
- dla wyjścia NDI: zainstalowane NDI Tools lub NDI Runtime.

Wydajność zależy od enkodera. Na komputerze testowym (6 wątków, Intel Quick Sync) 8 kanałów 720p25 zajmuje
ok. 2,3 rdzenia procesora, a 16 kanałów to granica możliwości.

## Instalacja

1. Pobierz `IsoRecorder-<wersja>-win64.zip` z zakładki **Releases**.
2. Rozpakuj w dowolnym miejscu.
3. Uruchom `IsoRecorder.exe`. Niczego nie trzeba instalować — Qt i GStreamer są w paczce.

Gdyby program nie chciał wystartować z ostatnio używaną listą kanałów, przytrzymaj **Shift** przy uruchamianiu —
otworzy się z pustą listą.

## Pierwsze kroki

1. **ADD SOURCE** dodaje kanał. W zakładce **Input** wybierz typ źródła i urządzenie albo adres.
2. W zakładce **Output** ustaw folder nagrań, kontener i bitrate; tam też jest wyjście na żywo (**Live output**).
3. **REC** na kaflu nagrywa jeden kanał; zaznacz kilka kanałów i użyj panelu **Selected channels**, żeby ruszyły razem.
4. **SAVE** zapisuje listę kanałów do pliku — przy następnym uruchomieniu program otworzy ją sam.

Skróty: `R` nagrywaj, `P` pauza, `S` stop, `X` podział, `Ctrl+A` zaznacz wszystkie, `Ctrl+S` zapisz listę,
strzałki — zmiana kanału aktywnego. Dwuklik w obraz otwiera duży podgląd.

## Budowanie ze źródeł

Potrzebne: Visual Studio Build Tools (C++ i CMake), Qt 6 w `C:\Qt\<wersja>\msvc*_64`,
GStreamer 1.28 MSVC x64 (pakiety runtime i development).

```powershell
scripts\build.ps1      # buduje do build\
scripts\run.ps1        # uruchamia z wlasciwymi sciezkami
scripts\package.ps1    # sklada samodzielna paczke w dist\IsoRecorder\
```

Testy (PowerShell, wymagają `ffprobe`) są w katalogu `tests\`; opis w [AGENTS.md](AGENTS.md).

## Dokumentacja

- [CHANGELOG.md](CHANGELOG.md) — historia wersji,
- [instrukcja_koncowa.md](instrukcja_koncowa.md) — specyfikacja i opis mechanizmów,
- [AGENTS.md](AGENTS.md) — stan projektu, zasady pracy i plan dalszych etapów.

## Autorzy

Studio Krause + Claude Fable 5.2

Program korzysta z bibliotek [Qt](https://www.qt.io/) i [GStreamer](https://gstreamer.freedesktop.org/) (LGPL).
