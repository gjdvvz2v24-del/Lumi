Po idf.py build w katalogu firmware:

```sh
cmake -S tests/wake_host -B /tmp/lumi-wake-host
cmake --build /tmp/lumi-wake-host -j 4
/tmp/lumi-wake-host/host_check ../model/pl_PL-gosia-medium_preview.wav ../model/pl_PL-darkman-medium_preview.wav
```

Na Macu w pierwszym poleceniu można dodać -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk. Test korzysta z przenośnych jąder TFLite Micro; nie mierzy wydajności jąder ESP-NN na ESP32.

Regresja szumu/brumu: `/tmp/lumi-wake-host/noise_check`. Oczekiwane: False wakes: 0 / 50.
