# Historia wersji

Numeracja: 0.1, 0.2, … (plan wersji: `instrukcja_koncowa.md`, rozdz. 6).

## 0.4

- Panel sterowania przeniesiony spod siatki do prawej kolumny, pod inspektor (cztery grupy jedna pod drugą).
- Blok wideo zajmuje całą wysokość okna, a jego szerokość jest wyliczana tak, żeby 16 kafli 16:9 (4×4)
  wypełniło go dokładnie, z równymi marginesami; prawa kolumna dostaje resztę szerokości (384–560 px).
- Większe kafle: przy 1600×900 kadr ma ok. 289×163 px zamiast 247×139.
- Treść zakładek inspektora jest przewijana; licznik kanałów stoi w tytule grupy Channel list.
- Plan wersji przesunięty o jedną: nagrywanie od 0.5.

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
