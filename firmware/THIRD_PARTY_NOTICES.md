# BMI270 configuration

The 8192-byte array in main/bmi270_config.h is copied verbatim from Bosch Sensortec BMI270 SensorAPI bmi270.c v2.86.1 (BSD-3-Clause). Full copyright and license are retained in that header.

Source: https://github.com/boschsensortec/BMI270_SensorAPI/blob/master/bmi270.c
SHA-256 of array: 2d75e68e343a13ff99be98261dfbd99d9e8c6f267da9e3c5aeb883276ad178db

AtomS3R sensor axis mapping: M5Stack M5Unified src/utility/IMU_Class.inl (board X = sensor Y, board Y = -sensor X, board Z = sensor Z).
https://github.com/m5stack/M5Unified/blob/master/src/utility/IMU_Class.inl
Board wiring and axes: https://docs.m5stack.com/en/core/AtomS3R


## Lokalny detektor „Hej Lumi”

Model: wynik lokalnej próby treningu microWakeWord; SHA-256 2787469c037f631f6792563cea5134cf1505e2d8ce07e96ae891d19f269800f9.
Trener: https://github.com/interkelstar/microwakeword-trainer — 8fdbadc3b969a8c1993108c0d655910440dfb762.
microWakeWord: https://github.com/kahrendt/microWakeWord — a70bd740d4e79ee8a8bb3db843fe862b88d5d6b0.
Frontend: https://github.com/rhasspy/pymicro-features — 96bd69cfad79aa67697e176570d3dd87052c3def. Apache-2.0, pełna licencja w components/lumi_wake/FRONTEND-LICENSE; informacje o prawach autorskich zachowano w plikach.
KissFFT: BSD-3-Clause, Mark Borgerding; pełna treść licencji w components/lumi_wake/KISSFFT-LICENSE i nagłówkach źródeł.
Runtime: https://github.com/espressif/esp-tflite-micro, komponent 1.4.1; TensorFlow Lite Micro Apache-2.0. Zależności i hashe: dependencies.lock.
Głosy syntezy treningowej: pl_PL-gosia-medium i pl_PL-darkman-medium z https://huggingface.co/rhasspy/piper-voices/tree/main/pl/pl_PL.

Protokół funkcji kończącej sesję: https://developers.openai.com/api/docs/guides/realtime-conversations oraz https://developers.openai.com/api/docs/guides/realtime-mcp.
