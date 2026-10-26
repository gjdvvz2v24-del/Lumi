# Lumi

Firmware dla M5Stack ATOM S3R z Atomic Voice Base (ES8311), ESP32-S3, 8 MB flash/PSRAM. Rozmowa przez OpenAI Realtime, mowa przez ElevenLabs.

## Układ

- `firmware/` — źródła, prompt, testy i gotowe pliki w `dist/`.
- `packages/lumi-firmware.zip` — paczka do pobrania; zawsze zawiera katalog `firmware/`.

## Uruchomienie

W katalogu `firmware/` uruchom `bash dist/flash_prebuilt.sh PORT_USB`. Skrypt wymaga Pythona z esptool, sprawdza tablicę partycji oraz sumy plików i zachowuje NVS. Jest przeznaczony do aktualizacji urządzenia z pasującym układem partycji; konfiguracja nowego urządzenia wymaga ESP-IDF i pełnego flashowania.

Klucze usług i Wi-Fi konfiguruj lokalnie przez `configure_usb.py --help`. Nie są częścią repozytorium.

## Trzy sieci Wi-Fi

Firmware obsługuje do trzech różnych sieci 2,4 GHz: skanuje przy starcie i po
rozłączeniu, wybierając najsilniejszy dostępny sygnał. Konfigurację samych sieci
obsługuje `firmware/configure_wifi_usb.py`; zachowuje klucze usług i prompt.
Bez nowych profili nadal korzysta z dotychczas zapisanej sieci. Zmiany stosuje
po restarcie; narzędzie wymaga firmware z obsługą tego polecenia USB.

## Klucze obu backendów

Kod i konfiguracja modeli są w Git; sekrety są w NVS urządzenia i w prywatnej
kopii diagnostycznej poza repozytorium. W wariancie Sol działają równolegle:

- `bober/openai` — dotychczasowy klucz Realtime, używany do ASR/VAD.
- `bober/sol` — osobny klucz Responses dla Sol Ultrafast; nie ma fallbacku do
  klucza Realtime. Od 26.10.2026 używany jest „Lumi Sol Ultrafast — stały”,
  Restricted / Responses Write, bez daty wygaśnięcia.
- `bober/key` — ElevenLabs TTS.

Testowy klucz Sol był fizycznie zachowany po flashowaniu, lecz 26.10.2026 OpenAI
odrzucał go jako `401 invalid_api_key`. Wymieniono wyłącznie `bober/sol` przez
istniejące polecenie USB `LUMI_SOL_KEY <sekret>` w trybie oczekiwania. Odczyt NVS
potwierdził nowy klucz i brak zmian w Realtime/TTS oraz profilach Wi-Fi. Udana
rozmowa na urządzeniu potwierdziła Sol Ultrafast, pełne TTS i powrót mikrofonu.
Nie zapisuj wartości kluczy ani haseł Wi-Fi do Gita ani do logów.

## Prompt i budowanie

Źródłem instrukcji jest `firmware/prompt.txt`, a powitania i ustawień `firmware/agent-profile.json`.

```sh
cd firmware
python3 tools/generate_profile.py
# W środowisku ESP-IDF 5.4.3:
idf.py build
```

Sam prompt można wgrać przez `python3 update_prompt_usb.py PORT_USB`. Aktualizacja promptu nie zastępuje aplikacji. Przy budowaniu po zmianie promptu uruchom generator, aby zaktualizować również zapasowe instrukcje w aplikacji.

## Zachowanie

Bez połączenia Wi-Fi Atom odtwarza lokalne, krótkie chrapnięcie co 5 sekund. Po połączeniu z siecią dźwięk ustaje.

Po starcie Lumi czeka na hasło „Hej Lumi”, „Luuu-miiii” lub mocne potrząśnięcie. Po zakończeniu rozmowy wraca do oczekiwania. Potrząsanie nie odtwarza efektów dźwiękowych. Ekran i podświetlenie gasną po 60 s od startu. Krótkie naciśnięcie przy wygaszonym ekranie włącza go na kolejne 60 s, bez przerwania rozmowy. Przy włączonym ekranie krótki przycisk restartuje urządzenie do oczekiwania; długie przytrzymanie usypia urządzenie.

Aktualny prompt wymaga krótkiej, prostej rozmowy bez nieproszonych porad, komentarzy do emocji, schlebiania i uzasadniania własnych reakcji. Użytkownik potwierdził poprawę stylu 24.10.2026.

Modele wybudzania są eksperymentalne; skuteczność dla różnych osób nie jest gwarantowana. W wariancie Sol prośba o zaśpiewanie czegoś lub piosenkę wywołuje ElevenLabs Music: losowy temat i styl, nowy polski tekst, wokal i akompaniament, 60 sekund. To oddzielny generator muzyki, bez gwarancji głosu używanego w TTS. Zwykła rozmowa o piosenkach nie powinna uruchamiać generowania. Każdy pełny utwór jest zapisywany jako WAV w Zasobach ElevenLabs (nazwa Lumi + data + unikalny numer), około 1,92 MB na minutę. Przy problemie z wysyłaniem Atom zachowuje jeden oczekujący plik w RAM i blokuje nową generację do zapisu; odcięcie zasilania przed zapisem może go utracić.

## Eksperyment Sol Ultrafast

Prototyp hostowy toru WAV → polska transkrypcja → Sol 6.1 Ultrafast → obecny głos
Lumi opisuje [instrukcja eksperymentu](firmware/experiments/sol-ultrafast.md).
Na tej gałęzi jest też opcjonalny firmware USB `LUMI_SOL_ULTRAFAST`: Realtime
obsługuje ASR/VAD, Sol generuje odpowiedzi. Domyślny build źródeł bez tej flagi używa standardowego backendu Realtime.
Gotowa paczka na tej gałęzi zawiera wariant Sol `2.11.1-diverse`.

## Pakowanie firmware

`python3 firmware/tools/package_firmware.py firmware packages/lumi-firmware.zip`

Przed pakowaniem odśwież `firmware/dist/` z wyników kompilacji i sumy `SHA256SUMS.txt`. Archiwum zawsze ma stały katalog główny `firmware/`.
