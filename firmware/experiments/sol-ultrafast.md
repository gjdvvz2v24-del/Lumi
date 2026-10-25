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
połączenia. Realtime używa istniejącego klucza `bober/openai`, a Sol osobnego
klucza testowego `bober/sol` w NVS. Brak klucza testowego nie uruchamia zastępczo
klucza Bobera. Głos i klucz ElevenLabs pozostają z NVS. Żaden sekret nie jest
osadzony w kodzie, binarce aplikacji ani paczce GitHuba.

Klucz Sol zapisuje się przez USB komendą `LUMI_SOL_KEY <sekret>` zakończoną LF,
tylko gdy rozmowa jest wyłączona. Potwierdzenie `LUMI_SOL_KEY_SAVED` nie zawiera
sekretu. Operacja aktualizuje wyłącznie wpis `bober/sol` przez NVS API;
nie używa `configure_usb.py`, który nadpisuje całą partycję. Komenda jest
dostępna wyłącznie w buildzie eksperymentalnym i nie powinna trafić do logów hosta.
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
  tego klucza ujawnił błędne współdzielenie klucza eksperymentu. Po doprecyzowaniu
  użytkownika cofnięto Responses w kluczu Bobera i rozdzielono klucze w firmware.
  Klucz „Sol Ultrafast test 2026-10-25” ma już Responses → Write.
  Udanej rozmowy Sol na urządzeniu jeszcze nie potwierdzono.
- Poprawiony build z rozdzielonymi kluczami ponownie zapisano przez USB,
  z weryfikacją hasha. Klucz testowy zapisano jako osobny wpis `bober/sol`;
  urządzenie potwierdziło `LUMI_SOL_KEY_SAVED`. SHA-256 nowej aplikacji:
  `7caacb016984896190f1685d301f14d8b3af5e89d1f40d91e4094c30032fbb10`.

### Ciągłość rozmowy po reconnect

Użytkownik potwierdził działanie Sol na urządzeniu po rozdzieleniu kluczy,
ale zgłosił częste resetowanie rozmowy. Znaleziono niezależny błąd: reconnect
transportu ASR zerował `previous_response_id` Sol i ponownie uruchamiał powitanie.
Historia i znacznik powitania są teraz przypisane do generacji rozmowy użytkownika,
a nie do pojedynczego połączenia WebSocket. Nowe wybudzenie nadal tworzy nową
rozmowę. Test C sprawdza oba przypadki. Dodatkowe logi zawierają kod błędu API,
typ zdarzenia transportu i błąd TTS, bez sekretów i treści rozmowy.
Nie potwierdzono jeszcze przyczyny samych rozłączeń ani skuteczności poprawki
w dłuższej rozmowie na sprzęcie.

### Powrót mikrofonu po TTS

Po zgłoszeniu zbyt późnego włączania mikrofonu skrócono w wariancie Sol pauzę
po ostatniej porcji odtwarzanego PCM z >200 ms do >=120 ms. Usunięto dodatkowy
warunek 350 ms od odbioru PCM z sieci: przy `g_response_complete=true` producent
już zakończył pracę. Nadal wymagane są pusty bufor PCM i brak aktywnej reakcji.
120 ms obejmuje domyślną kolejkę I2S (6 * 240 ramek / 16 kHz = 90 ms), ostatni
blok 20 ms i margines. To nie włącza przerywania w trakcie mowy ani AEC.
Testy bramki, rzeczywistego zadania głośnika i protokołu przeszły. Ocena
szybkości przejęcia głosu i ewentualnego echa pozostaje testem użytkownika.
Domyślny wariant Realtime zachowuje stare progi.

### Wariant mniejszego opóźnienia

**Aktualizacja po teście użytkownika:** próg 350 ms zbyt wcześnie kończył
wypowiedzi i ucinał zdania. Przywrócono **650 ms**; reuse HTTPS, krótka pauza
mikrofonu po odtwarzaniu i limit bezczynności pozostają. Poniżej opis próby 350 ms.

- Tylko w eksperymencie Sol: server VAD czeka 350 ms ciszy zamiast 650 ms.
  Skraca to okno oczekiwania o 300 ms, ale może wcześniej uznawać dłuższą pauzę
  wewnątrz zdania za koniec tury.
- Klient HTTPS Sol pozostaje otwarty po poprawnej odpowiedzi, zamiast zamykać
  transport po każdej turze. Po 20 s przerwy tworzony jest nowy klient; błędy,
  koniec sesji i zmiana generacji też zamykają stary. Jeśli otwarcie żądania na
  zachowanym połączeniu zawiedzie, wolno raz ponowić samo otwarcie przed wysłaniem
  treści POST. Po wysłaniu body/generowaniu nie ma automatycznego retry.
  Log `reused_client` oznacza użycie tego samego klienta, nie dowód braku handshake.
- Limit 10 minut w wariancie Sol dotyczy teraz bezczynności, odnawianej przez
  rozpoznaną turę i poprawną odpowiedź; nie kończy aktywnej rozmowy po 10 minutach
  od połączenia ASR. Limit 1024 tokenów jednej odpowiedzi pozostaje bez zmian.
- Log `TTS first PCM` mierzy czas TTS i czas od przekazania transkrypcji do Sol
  do pierwszych bajtów PCM; nie obejmuje oczekiwania VAD ani samej transkrypcji.

Test C sprawdza zachowanie jednego klienta w kolejnych turach, bezpieczne ponowienie
samego otwarcia starego socketu, zamykanie po błędzie, VAD 350 ms i limit bezczynności.
Nadal jest to tor szeregowy ASR → pełna odpowiedź Sol → TTS. Nie wdrożono jeszcze
strumieniowego przekazywania tekstu do TTS. W pierwszych krótkich próbach hostowych
różnica między pierwszym fragmentem tekstu a końcem odpowiedzi wynosiła ok. 0,1–0,2 s,
więc w tym wariancie najpierw skracamy VAD i narzut połączeń. Przyspieszenie całego
toru i zachowanie przy trzech połączeniach TLS wymagają oceny na urządzeniu.

Dokumentacja: [Ultrafast](https://developers.openai.com/api/docs/guides/ultrafast-mode),
[transkrypcja](https://developers.openai.com/api/docs/guides/speech-to-text),
[strumieniowanie Responses](https://developers.openai.com/api/docs/guides/streaming-responses).
