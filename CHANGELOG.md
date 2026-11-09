# Historia wersji

Numeracja: 0.1, 0.2, … (plan wersji: `instrukcja_koncowa.md`, rozdz. 6).

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

Jeszcze nie działa (przyciski widoczne, ale nieaktywne lub z komunikatem) — numery po przesunięciu planu w 0.2:
nagrywanie (0.3), split (0.4), pauza (0.5), zapis/wczytanie listy (0.6), prawdziwe wejścia (0.7–0.9, 1.1),
scheduler i widok Lista (1.0).
