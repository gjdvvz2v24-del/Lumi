# Weryfikacja 2.9.3-singing — 24.10.2026

Zmiany użytkownika w prompt.txt zachowane bajtowo przed akapitem „Wykonanie głosowe”. Zastąpiono tylko ten akapit instrukcją używania wyłącznie [sings]. Kopia lumi-zosia-prompt.txt i build/prompt_fs/prompt.txt są identyczne z aktualnym prompt.txt. SHA256 promptu: 5cedf793391555deeb4d4883422e92266c7e1d21a205c2613a8c5513a35a041f.

Test produkcyjnego tts_delivery.h pod ASan/UBSan: PASS. Normalna mowa pozostaje identyczna, zachowuje tagi emocji i model eleven_v4_turbo. Śpiew wybiera eleven_v4, usuwa konkurencyjne tagi, zachowuje treść i granice wierszy, nie wymusza language_code=pl. Sprawdzono dwa języki, CRLF, puste wersy, cytowany tag w zwykłym zdaniu, błędny nawias i maksymalną długość wejścia.

Testy test_openai.py i test_lumi_profile.py: PASS. ESP-IDF 5.4.3 build: PASS. Fizyczny flash aplikacji i promptfs: PASS po porównaniu partycji; esptool potwierdził oba hashe. NVS zachowane. Po restarcie log pokazuje WAKE_WAIT i autotest nowego modelu hasła z healthy=1.

Nie potwierdzono odsłuchem jakości śpiewu ani tego, że inne tagi były jedyną przyczyną problemu. Hipoteza konfliktu została zabezpieczona po stronie firmware, a nie tylko opisem dla modelu. Nie generowano osobnej płatnej próbki testowej. Nie nagrywano i nie zapisywano treści rozmowy.

Dokumentacja: https://elevenlabs.io/docs/help-center/product/core-capabilities/text-to-speech/how-do-audio-tags-work-with-eleven-v3-and-v4 oraz https://elevenlabs.io/docs/overview/models . [sings] pozostaje eksperymentalnym tagiem: https://elevenlabs.io/docs/overview/capabilities/text-to-speech/best-practices .

Urządzenie nadal obsługuje oba hasła oraz mocne potrząśnięcie bez nagranych reakcji ruchu. Wykonano pełny flash, więc aktualny prompt jest zarówno w promptfs, jak i w fallbacku aplikacji.
