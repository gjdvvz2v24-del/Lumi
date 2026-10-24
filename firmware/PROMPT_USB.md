# Instrukcje Lumi przez USB

Główne źródło: prompt.txt, UTF-8, maksymalnie 15000 bajtów.
Powitanie first_message i głos tts.voice_id są w agent-profile.json; ich zmiana
wymaga uruchomienia tools/generate_profile.py oraz kompilacji aplikacji.

Przy pierwszym przejściu z Bobera wgraj aplikację i prompt przez
`bash dist/flash_prebuilt.sh PORT`. Skrypt sprawdza tablicę partycji i nie zapisuje NVS.

Późniejsze zmiany samego promptu:

```bash
source /Users/grpi/Work/lumi/esp-idf/export.sh
python3 update_prompt_usb.py PORT prompt.txt
```

Skrypt sprawdza układ partycji i zapisuje tylko promptfs (0x7e0000, 128 KiB).
Nieprawidłowy lub brakujący prompt powoduje użycie skompilowanej instrukcji Lumi.
Model nie otrzymuje bazy kawałów ani historycznych dokumentów spoza prompt.txt.
