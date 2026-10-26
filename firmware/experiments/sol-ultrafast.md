# Sol 6.1 Ultrafast — eksperyment na komputerze

Gałąź `test/sol-ultrafast`, baza: `d1092ec` z `origin/main`.
Źródłem promptu, transkrypcji, narzędzia kończenia rozmowy, głosu i modelu
ElevenLabs jest aktualne repozytorium (`prompt.txt`, `agent-profile.json`).
Model rozmowy w eksperymencie: `gpt-6.1-sol`, reasoning `medium`, budżet 2048 tokenów odpowiedzi (razem z rozumowaniem).

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
  od połączenia ASR. W tej wcześniejszej próbie limit wynosił 1024 tokeny; aktualnie Sol ma 2048.
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

### Cached thinking fillers

Six PCM clips generated once with the profile's voice/model are embedded only in
Sol builds: Hmm…, Niech pomyślę…, Chwileczkę…, Daj mi sekundę…, Już myślę…,
Moment…. The manifest records voice/model/text; `tools/generate_fillers.py`
requires ELEVENLABS_API_KEY and skips existing files to prevent duplicate charges.
Local playback makes no API call. It is eligible for transcripts of at least 20
words, on one third of eligible turns, at most once per minute. It starts only
if Sol is still pending after 1200 ms. A temporary 8 KB worker owns Sol HTTP;
the existing network owner feeds filler PCM into the same FIFO as the final
answer. Clips finish before the answer plays, microphone stays gated until the
whole answer drains, and session cancellation joins the worker before cleanup.
If task allocation fails, ordinary synchronous generation remains available.
Consecutive filler clips do not repeat. Policy and assets have offline checks;
physical listening is needed to judge naturalness and timing.

The prompt now identifies Lumi as an axolotl created by Grzegorz and allows
consistent fictional personal anecdotes/preferences. Direct questions about
whether anecdotes really happened receive an honest explanation of the persona.

### Microphone-ready cue

A locally synthesized, quiet 75 ms metallic double tap precedes each return to
listening (after greeting/reply and after an ASR reconnect). It uses the existing
PCM FIFO and speaker owner; uplink stays gated through the cue and the ordinary
DMA/echo tail, then the phase changes to LISTENING. Completion of the cue does
not queue another cue. Farewell/session shutdown skips it; failure to enqueue
the optional cue falls back to listening. No API call is required. Regenerate
`main/mic_ready_sound.h` with `tools/generate_mic_ready.py` (also writes a local WAV
preview to the diagnostics folder). Native event tests cover cue completion,
failure fallback and farewell, with existing playback-gate/speaker tests.


### Prepared only: reasoning medium and three Wi-Fi profiles

This revision has not been flashed or provisioned to the Atom. Sol remains
`gpt-6.1-sol` / `ultrafast`, now with `reasoning.effort=medium` and a 2048-token
output budget, configured in `agent-profile.json` under `sol`. The increased
budget includes reasoning, while the prompt still asks for short spoken answers.
This may increase latency and billed output tokens; offline tests validate the
request, not a quality or latency improvement. No paid API test was run.
Official supported efforts: https://developers.openai.com/api/docs/models/gpt-6.1-sol

Wi-Fi now scans asynchronously at STA startup and after disconnect. Among up to
three visible configured 2.4 GHz networks, it chooses the strongest compatible
RSSI and pins the selected BSSID/channel. Equal RSSI prefers the earlier profile.
It does not interrupt a healthy connection to roam. Scan timeout is 10 s;
association/DHCP timeout is 30 s; retry/rescan delay is 3 s. Profiles that fail
association/DHCP are skipped for 30 s, so a stronger AP with an invalid password
cannot indefinitely starve a working alternative. Retry timer events are posted
to the default event loop, serializing selection with Wi-Fi driver events.

Without the new `bober/wifi_profiles` NVS string, the original `ssid`/`pass`
configuration is used as the sole profile. The USB command replaces the complete
profile list in one NVS value, preserving all API keys, legacy fields and prompt.
SSID/passwords are never logged. It requires WAKE_WAIT and applies after restart.
The USB tool checks firmware readiness before sending any credentials.

When this firmware is eventually deployed, configure locally (do not run now):

```sh
python3 firmware/configure_wifi_usb.py PORT_USB
```

It asks for up to three different SSIDs and hidden password input. A local JSON
array can instead be supplied with `--file PATH`; `--check --file PATH` validates
without opening USB. Each entry has `ssid` and `password` fields. Open networks
use an empty password. Do not commit credential files. This is a Wi-Fi-only
updater; the older `configure_usb.py` is still full initial NVS provisioning.

The prompt now allows occasional true poop trivia when relevant. Native tests
execute the real C profile parser, USB setter, scan selection and reconnect
callbacks against fake NVS/radio/HTTP; physical three-network roaming remains
untested until deployment is requested.


### Prepared only: long-story TTS and output volume

The old producer rejected more than 45 seconds of *downloaded* PCM and abandoned
its queue on failure. Download can run ahead of playback, so this could cut the
audible story much earlier, including around ten seconds. This is a confirmed
code limit; it has not yet been confirmed as the cause of the user's specific
live incident. The old fixed 60 s stream deadline and 90 s turn-age playback
watchdog also cut long audio.

TTS now streams up to five minutes of PCM through the existing bounded 2 MiB
FIFO, with a six-minute total download budget. HTTP timeout/no-progress guard
is 30 s. FIFO backpressure refreshes progress only while the speaker consumes
PCM. Playback watchdog detects 90 s without network/playback progress rather
than 90 s since turn start. Cancellation and ASR reconnect still stop the
producer; no paid automatic retry is added. Logs distinguish HTTP status, I/O
errors, ASR reconnect, cancellation, byte limit and inactivity, without text or
credentials. Native fake-radio/HTTP tests stream full three/five-minute audio,
exercise backpressure and EOF/odd samples, and verify the mic stays gated until
PCM drains. These tests make no API or device calls.

Codec output volume is now 95 percent (previously 90). The changes are built
but **not flashed**; live audio, loudness and Wi-Fi selection await deployment.


### USB deployment — 2026-10-26

Deployed the prepared app from source commit `82ca320` and the matching promptfs
on the physical AtomS3R / Atomic Voice Base over `/dev/cu.usbmodem1101`.
Application SHA-256:
`c1527720aa494ba09a8c708e3590dfbd51191168390a4a59f70d86db9a7aae31`.
Both flash hashes were verified. Protected partition-table/NVS/PHY bytes were
compared before/after application flashing and matched; promptfs readback matched
the built image. Boot had the experimental backend marker, no panic and a DHCP
address.

After flashing, the user's three Wi-Fi profiles were saved through the new
Wi-Fi-only USB command. Credentials remain outside the repository. A subsequent
restart scanned networks, selected profile 1 at -52 dBm and obtained an IP address.
This verifies physical startup selection and profile provisioning. Re-selection
following an actual link loss, spoken response quality, physical volume and a
long live story have not yet been retested. The three/five-minute TTS evidence
above remains a native synthetic test, not a live ElevenLabs/device conversation.


### Permanent Sol credential — 2026-10-26

A fresh physical NVS read confirmed two distinct OpenAI credentials. A free model
lookup accepted the original Realtime key (HTTP 200) but rejected the stored Sol
test key (HTTP 401, `invalid_api_key`). The test key had not been removed by app
flashing. Its previous recorded expiration was October 26, but the current panel
had no expired/revoked record from which to prove who or what invalidated it.

At the user's explicit request and final confirmation, created
„Lumi Sol Ultrafast — stały” in the same project, Restricted / Responses Write,
with expiry Never. A deliberately invalid `max_output_tokens=0` Responses request
passed authentication and returned HTTP 400 on that parameter, without generation.
The existing USB setter saved only `bober/sol`. Physical NVS readback matched the
new secret; all other string values in namespace `bober`, including Realtime,
ElevenLabs and the three Wi-Fi profiles, matched their pre-update values.
The temporary transfer file was deleted. Credentials and private NVS backups
remain outside Git; no app/prompt reflash was needed for this repair.

The user's physical conversation then completed: Sol served tier `ultrafast`
in 4354 ms, TTS completed 3840 ms of audio, and the microphone returned to listening.
This verifies the credential repair and one short live turn. Long live stories
and physical disconnect/reselection still require their own device tests.

### Offline snore

A locally synthesized 650 ms nasal snore is stored in `assets/lumi_snore.pcm`.
The existing I2S speaker owner plays it every five seconds while `g_wifi_ready`
is false, starting five seconds after detecting the offline state. It also works
in local WAKE_WAIT, without an enabled conversation. Once Wi-Fi obtains an IP,
new snore PCM stops immediately; already queued DMA audio may take its ordinary
short hardware tail to settle. It never preempts TTS or reaction audio and is
silenced during power shutdown. Healthy Wi-Fi is silent.

The scheduler and 180 ms echo guard are speaker-owned. The wake classifier gets
zeroed input and ignores detections while the local snore/echo guard is active,
so the device cannot wake itself on its own effect. Explicit strong-shake wake
is still accepted. There is no API call or extra audio task. Regenerate with
`python3 firmware/tools/generate_snore.py`; optional `--preview PATH.wav` writes
a local listenable preview. Native tests execute the real scheduler and speaker
with Wi-Fi/session transitions, verify full clips five seconds apart, immediate
reconnect cancellation, audio priority, timer wrap, power and self-wake masking.


Offline-snore app source `e377c13` was flashed over USB on 2026-10-26. App hash
was verified; partition table/NVS/PHY bytes and promptfs were compared before and
after the app-only update and matched, preserving the permanent Sol credential
and all three Wi-Fi profiles. Physical startup obtained IP at 4821 ms, with no
snore on the healthy connection. Offline cadence/audio and self-wake suppression
were tested natively; a physical router outage was not induced.

A subsequent prompt-only USB update added the user's exact reply to questions
about the AI model: „model kupa dupa sraka by Tatuś”. The axolotl identity and
Grzegorz creator answer remain. The guarded updater verified the partition table
and prompt image hash; it did not rewrite the application or NVS. The generated
fallback header in Git also contains the updated prompt for subsequent builds.

## Random song module (2026-10-26, deployed app)

Sol exposes `play_random_song` only in this experimental backend. A request to
sing something or play a song invokes ElevenLabs Music (`music_v2`), with a random
combination of eight styles and eight topics, new Polish lyrics and female vocals.
It does not promise the configured TTS voice or reproduce a named existing song.
Ordinary discussion, quoted requests and negated requests must not invoke Music.

One `POST /v1/music?output_format=pcm_16000`, `music_length_ms=30000`, using the
existing ElevenLabs NVS `key`. No additional OpenAI scope and no key in Git.
A real host request returned HTTP 200: this key already has Music access. It took
7.6 s and returned 1,920,000 bytes of stereo signed PCM16 at 16 kHz (30 seconds).
The player downmixes stereo frames to mono for the existing FIFO. It preserves
partial frames across chunks, checks the exact duration, and rejects JSON/errors.
No automatic paid retries. Generation has a 120 s HTTP timeout / 180 s total
budget; after headers, no-progress timeout is 30 s. The mic resumes only after
the existing playback drain and cue. A song tool result joins the next user turn
in Responses history, without another model call solely for acknowledgement.

Validation: native `test_music_stream.py`, `test_sol_firmware.py`,
`test_tts_stream.py`, `test_fillers.py` and `test_lumi_profile.py` passed;
full ESP-IDF build passed. Published app and ZIP are `2.10.0-music`.
App SHA-256: `4901580a26f48536721ea9d315b50df62b5dcf96b497cdb35b1717249b4592d9`.
Flashed only app at `0x10000`; readback comparison confirmed identical protected
partitions (`0x8000..0xffff`, including NVS/keys/Wi-Fi) and promptfs. Boot reported
the new version, Wi-Fi IP ready at 4.9 s, and no panic. Host Music generation and
30 s PCM format are verified. A spoken request on the physical Atom invoked
Music: first PCM after 6,531 ms, complete 30 s audio downloaded after 14,965 ms.
The speaker FIFO drained and microphone cue/listening resumed afterwards. Audible
quality still requires user confirmation. Tests do not automatically retry or
generate additional songs.
