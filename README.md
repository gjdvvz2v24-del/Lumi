# Lumi

Firmware dla M5Stack ATOM S3R z Atomic Voice Base (ES8311), ESP32-S3, 8 MB flash/PSRAM. Rozmowa przez OpenAI Realtime, mowa przez ElevenLabs.

## Układ

- `firmware/` — źródła, prompt, testy i gotowe pliki w `dist/`.
- `packages/lumi-firmware.zip` — paczka do pobrania; zawsze zawiera katalog `firmware/`.

## Uruchomienie

W katalogu `firmware/` uruchom `bash dist/flash_prebuilt.sh PORT_USB`. Skrypt wymaga Pythona z esptool, sprawdza tablicę partycji oraz sumy plików i zachowuje NVS. Jest przeznaczony do aktualizacji urządzenia z pasującym układem partycji; konfiguracja nowego urządzenia wymaga ESP-IDF i pełnego flashowania.

Klucze usług i Wi-Fi konfiguruj lokalnie przez `configure_usb.py --help`. Nie są częścią repozytorium.

## Trzy sieci Wi-Fi (nowy kod, jeszcze niewgrany)

Firmware obsługuje do trzech różnych sieci 2,4 GHz: skanuje przy starcie i po
rozłączeniu, wybierając najsilniejszy dostępny sygnał. Konfigurację samych sieci
obsługuje `firmware/configure_wifi_usb.py`; zachowuje klucze usług i prompt.
Bez nowych profili nadal korzysta z dotychczas zapisanej sieci. Zmiany stosuje
po restarcie; narzędzie wymaga firmware z obsługą tego polecenia USB.

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

Po starcie Lumi czeka na hasło „Hej Lumi”, „Luuu-miiii” lub mocne potrząśnięcie. Po zakończeniu rozmowy wraca do oczekiwania. Potrząsanie nie odtwarza efektów dźwiękowych. Krótki przycisk wraca do oczekiwania, długie przytrzymanie usypia urządzenie.

Aktualny prompt wymaga krótkiej, prostej rozmowy bez nieproszonych porad, komentarzy do emocji, schlebiania i uzasadniania własnych reakcji. Użytkownik potwierdził poprawę stylu 24.10.2026.

Modele wybudzania są eksperymentalne; skuteczność dla różnych osób nie jest gwarantowana. Instrukcję śpiewu usunięto z promptu. W kodzie pozostała obsługa tagów śpiewu z wersji 2.9.3, lecz nie potwierdzono satysfakcjonującego śpiewu.

## Eksperyment Sol Ultrafast

Prototyp hostowy toru WAV → polska transkrypcja → Sol 6.1 Ultrafast → obecny głos
Lumi opisuje [instrukcja eksperymentu](firmware/experiments/sol-ultrafast.md).
Na tej gałęzi jest też opcjonalny firmware USB `LUMI_SOL_ULTRAFAST`: Realtime
obsługuje ASR/VAD, Sol generuje odpowiedzi. Domyślny build i gotowa paczka nadal
używają standardowego backendu Realtime.

## Pakowanie firmware

`python3 firmware/tools/package_firmware.py firmware packages/lumi-firmware.zip`

Przed pakowaniem odśwież `firmware/dist/` z wyników kompilacji i sumy `SHA256SUMS.txt`. Archiwum zawsze ma stały katalog główny `firmware/`.
