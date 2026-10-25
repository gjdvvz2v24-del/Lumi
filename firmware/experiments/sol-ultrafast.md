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

To prototyp hostowy przez HTTPS/SSE, a nie nowy backend firmware.
Nie zmienia `main/eleven.c`, generatora profilu, `dist/` ani paczki produkcyjnej.
Nie ma ciągłego mikrofonu, VAD, przerwań, odtwarzania na ESP32 ani pełnej sesji
wieloturowej w CLI. Nie przeprowadzono flashowania ani testu rozmowy na urządzeniu.

Do wariantu sprzętowego potrzebna jest obsługa transkrypcji i Responses w firmware
lub jawnie skonfigurowany most na serwerze, a następnie zachowanie istniejących
bramek mikrofonu, wybudzania, sesji, kolejki audio i kończenia rozmowy.
Nie wystarczy podmiana nazwy modelu w sesji Realtime.

Dokumentacja: [Ultrafast](https://developers.openai.com/api/docs/guides/ultrafast-mode),
[transkrypcja](https://developers.openai.com/api/docs/guides/speech-to-text),
[strumieniowanie Responses](https://developers.openai.com/api/docs/guides/streaming-responses).
