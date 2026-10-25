# Sol 6.1 Ultrafast — eksperyment na komputerze

Gałąź `test/sol-ultrafast`, baza: `d1092ec` z `origin/main`.
Źródłem promptu, transkrypcji, narzędzia kończenia rozmowy, głosu i modelu
ElevenLabs jest aktualne repozytorium (`prompt.txt`, `agent-profile.json`).
Model rozmowy w eksperymencie: `gpt-6.1-sol`, reasoning `low`.

## Co można sprawdzić

`tools/check_sol_ultrafast.py` wykonuje pojedynczą turę:

1. Opcjonalnie transkrybuje mono PCM16 WAV przez model z profilu, po polsku.
2. Przesyła tekst do Responses API z `service_tier=ultrafast` i odbiera SSE.
3. Sprawdza zakończenie odpowiedzi i faktycznie zwrócony service tier.
4. Opcjonalnie generuje WAV przez obecny głos i model TTS Lumi.
5. Zapisuje odpowiedź, zużycie i czasy do `report.json`.

Nie ma zewnętrznych zależności Pythona. Klucze są pobierane wyłącznie ze zmiennych
`OPENAI_API_KEY` i, dla TTS, `ELEVENLABS_API_KEY`. Nie przekazuj ich jako argumentów
wiersza poleceń ani nie dodawaj do repozytorium. Wywołania usług są płatne.

Z katalogu `firmware/`, po ustawieniu zmiennych środowiskowych:

```sh
python3 tools/check_sol_ultrafast.py \
  --text 'Lumi, jakie kolory pasują do zielonej bluzy?' \
  --tts --output-dir sol-test-output/text

python3 tools/check_sol_ultrafast.py \
  --audio /absolutna/sciezka/nagranie.wav \
  --tts --output-dir sol-test-output/audio

# Standard Sol jako punkt odniesienia, ten sam plik wejściowy:
python3 tools/check_sol_ultrafast.py \
  --audio /absolutna/sciezka/nagranie.wav --tier default \
  --tts --output-dir sol-test-output/standard

python3 tests/test_sol_ultrafast.py
```

Pliki wejściowe są wysyłane do OpenAI, tekst odpowiedzi do ElevenLabs.
Skrypt nie nagrywa mikrofonu i nie odtwarza dźwięku automatycznie.

## Interpretacja pomiarów

- `asr_s`: przesłanie gotowego nagrania i transkrypcja; bez czasu nagrywania/VAD.
- `first_delta_s`: czas od wysłania żądania do pierwszego fragmentu tekstu lub
  argumentów funkcji. Dla pożegnania nie oznacza czasu do słyszalnej wypowiedzi.
- `generation_s`: czas do odebrania kompletnego strumienia odpowiedzi.
- `tts_first_audio_s`: czas od żądania TTS do pierwszych bajtów PCM.
- `pipeline_first_audio_s`: ASR + kompletna odpowiedź Sol + pierwsze bajty TTS.
  To dostępność audio na komputerze, bez odtwarzania i bufora urządzenia.

TTS celowo czeka na pełną, poprawnie zakończoną odpowiedź. Dzięki temu
nie wypowiada tekstu z odpowiedzi niekompletnej ani podwójnego pożegnania.
`end_conversation` jest interpretowane jako jedno pożegnanie; w tym teście
nie steruje fizycznym urządzeniem. Transkrypcja jest wykonywana osobno dla
każdego uruchomienia. Pojedynczy pomiar nie dowodzi przewagi szybkości.

## Wyniki usług z 25.10.2026

Syntetyczne nagranie z ElevenLabs: „Lumi, jakie kolory pasują do zielonej bluzy?”.
Obie transkrypcje zgodne z wejściem. Model TTS `eleven_v4_turbo`, głos
`eXpIbVcVbLo8ZJQDlDnl` z profilu repozytorium. Jedna próba na tryb:

| Pomiar | Ultrafast | Standard |
| --- | ---: | ---: |
| Transkrypcja | 0,698 s | 0,592 s |
| Pierwszy fragment Sol | 1,298 s | 1,070 s |
| Pełna odpowiedź Sol | 1,437 s | 1,993 s |
| Pierwsze audio od żądania TTS | 0,221 s | 0,201 s |
| Pierwsze audio całego toru | 2,356 s | 2,786 s |
| Tokeny wejścia / wyjścia | 2353 / 40 | 2353 / 40 |
| Tokeny wejścia z cache | 2350 | 0 |

API potwierdziło odpowiednio `service_tier=ultrafast` i `default`.
Ultrafast nie miał w tej próbie krótszego czasu do pierwszego fragmentu tekstu,
ale szybciej ukończył odpowiedź. Różnica cache i pojedyncza para prób uniemożliwiają
przypisanie całej różnicy samemu trybowi. Nie porównano jeszcze z Realtime.

Osobna próba „Dzięki, Lumi, do usłyszenia!” zwróciła `end_conversation` i
pożegnanie „Do usłyszenia!” w 1,424 s. Cztery testy offline przeszły: parser SSE,
odrzucenie niekompletnej odpowiedzi, pojedyncze pożegnanie oraz błędne narzędzia.

## Granice eksperymentu i dalsza integracja

Skrypt Pythona to prototyp hostowy przez HTTPS/SSE. Nie ma ciągłego mikrofonu,
VAD, przerwań, odtwarzania na ESP32 ani pełnej sesji wieloturowej w CLI.
Generator profilu, `dist/` i paczka produkcyjna pozostają bez zmian.

Do wariantu sprzętowego potrzebna jest obsługa transkrypcji i Responses w firmware
lub jawnie skonfigurowany most na serwerze, a następnie zachowanie istniejących
bramek mikrofonu, wybudzania, sesji, kolejki audio i kończenia rozmowy.
Nie wystarczy podmiana nazwy modelu w sesji Realtime.

## Eksperymentalny firmware USB

Opcja CMake `LUMI_SOL_ULTRAFAST` jest domyślnie wyłączona. Jej włączenie dodaje
wariant hybrydowy: obecna sesja Realtime dostarcza transkrypcję i VAD, a
`create_response=false` blokuje generowanie odpowiedzi przez model Realtime.
Odpowiedź generuje Sol 6.1 przez HTTPS Responses, z wymaganym tierem Ultrafast.
To nie jest pełne usunięcie Realtime i nie jest wariantem oszczędnościowym.

```sh
# Z katalogu firmware/, po aktywacji ESP-IDF 5.4.3:
idf.py -DLUMI_SOL_ULTRAFAST=ON build
# Powrót do standardowego backendu wymaga jawnego OFF w tym samym build/:
idf.py -DLUMI_SOL_ULTRAFAST=OFF build
```

Backend działa w istniejącym zadaniu sieciowym, poza callbackiem WebSocket.
Na czas Sol/TTS blokuje uplink mikrofonu. Zawartość promptfs jest przekazywana
do Sol, a `previous_response_id` utrzymuje historię do końca sesji/ponownego
połączenia. Używany jest OpenAI key z NVS urządzenia, który musi mieć również
uprawnienia Responses i Ultrafast. Klucz testowy z komputera nie jest osadzany
w binarce ani przenoszony do NVS. Głos i klucz ElevenLabs pozostają z NVS.
TTS czeka na pełną odpowiedź. Firmware sprawdza status i zwrócony tier,
obsługuje pożegnanie jeden raz i odrzuca wynik po zmianie generacji sesji.
Błąd API nie uruchamia automatycznego ponawiania ani modelu zastępczego.

Test bez połączenia do usług, wykonujący rzeczywisty kod C z atrapą HTTP:

```sh
SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk \
  IDF_PATH=/sciezka/do/esp-idf python3 tests/test_sol_firmware.py
```

Przed USB należy zachować kopię flasha poza repo, porównać tablicę partycji
urządzenia z buildem i zapisać wyłącznie aplikację pod `0x10000`.
NVS, bootloader, tablica partycji i promptfs nie wymagają zmiany.
Po restarcie urządzenie lokalnie oczekuje na wybudzenie; test rozmowy uruchamia
płatne usługi. Domyślne logi nie zawierają kluczy ani treści rozmowy.

### Walidacja firmware 25.10.2026

- Build ESP-IDF 5.4.3 z `LUMI_SOL_ULTRAFAST=ON` przeszedł.
- Test rzeczywistego backendu C z atrapą HTTP i sanitizerami przeszedł:
  konfiguracja ASR/VAD bez odpowiedzi Realtime, blokowanie nakładających się tur,
  historia, tekst, pojedyncze pożegnanie, anulowanie, błędy HTTP, niewłaściwy tier
  i odpowiedzi niekompletne. Istniejące testy OpenAI, profilu i promptfs przeszły.
- Wykonano pełną prywatną kopię flasha 8 MB; tablica partycji pasuje do buildu.
- Zapisano przez USB wyłącznie aplikację pod `0x10000`, hash zapisu zweryfikowany.
  SHA-256 aplikacji: `62b1ab4b535a4b041cad0a7451be75fe5bd1b938437422a21c32993823f65a04`.
- Odczyt po zapisie potwierdził identyczne bajty tablicy partycji, NVS, PHY i promptfs.
- Start sprzętu potwierdził log `EXPERIMENT: Sol Ultrafast responses; Realtime only ASR/VAD`,
  bez paniki i bez rozpoczęcia sesji API podczas kontroli startu.
- Pierwsza próba rozmowy użytkownika zwróciła HTTP 401: istniejący klucz urządzenia
  nie ma `api.responses.write`. Potrzebna zmiana Responses → Write w uprawnieniach
  tego klucza; nie wymaga ponownego flashowania. Po zgodzie użytkownika zapisano
  Responses → Write; panel ostrzega o kilkuminutowej propagacji uprawnień.
  Udanej rozmowy Sol na urządzeniu jeszcze nie potwierdzono.

Dokumentacja: [Ultrafast](https://developers.openai.com/api/docs/guides/ultrafast-mode),
[transkrypcja](https://developers.openai.com/api/docs/guides/speech-to-text),
[strumieniowanie Responses](https://developers.openai.com/api/docs/guides/streaming-responses).
