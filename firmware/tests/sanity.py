#!/usr/bin/env python3
"""Fast packaging assertions; DOES NOT replace ESP-IDF compilation or device tests."""
from pathlib import Path
import csv
root = Path(__file__).resolve().parents[1]
sources = {p.name: p.read_text() for p in (root/'main').glob('*.c')}
combined = '\n'.join(sources.values())
assert 'WIFI_MODE_STA' in sources['wifi.c']
assert 'WIFI_MODE_AP' not in combined
assert 'MIC TEST' not in combined
assert 'mic_click' not in combined and 'play_click' not in combined
assert 'GPIO_NUM_41' in sources['button.c']
for token in ('GPIO_NUM_8','GPIO_NUM_6','GPIO_NUM_5','GPIO_NUM_7','GPIO_NUM_38','GPIO_NUM_39'):
    assert token in sources['audio.c'], token
for token in ('GPIO_NUM_42','GPIO_NUM_48'):
    assert token in sources['screen.c'], token
assert 'BOBER_PCM_CAPACITY (2u * 1024u * 1024u)' in (root/'main/bober.h').read_text()
assert 'response.done' in sources['eleven.c']
assert 'input_audio_buffer.append' in sources['eleven.c']
assert 'pcm_16000' in sources['eleven.c']
assert '--chip", "esp32s3"' in (root/'configure_usb.py').read_text()
assert '115200' in (root/'flash.sh').read_text()
with (root/'partitions.csv').open() as f:
    parts = [[x.strip() for x in row] for row in csv.reader(f) if row and not row[0].startswith('#')]
assert any(x[0]=='nvs' and x[3]=='0x9000' for x in parts)
assert '8MB' in (root/'sdkconfig.defaults').read_text()
print('PASS: static configuration and packaging sanity checks; NOT a hardware compile/test')

assert 'espressif/esp_codec_dev: "1.5.9"' in (root/"main/idf_component.yml").read_text()
print("PASS: pin esp_codec_dev 1.5.9 for ESP-IDF 5.4.3")

assert 'LP5562 brightness=255' in sources['screen.c']
assert 'no boot color test' in sources['screen.c']
print('PASS: LCD init assertions (OpenAI transport tested in test_openai.py)')

# v1.6: test that codec gain is increased, actual mono slot fixed,
# silence sends zero PCM at constant rate, GC9107 y offset corrected.
assert 'esp_codec_dev_set_in_gain(dev,24.0f)' in sources['audio.c']
assert 'frames[2*i]>>16' in sources['audio.c']
assert 'pcm[i]=pass?limit_pcm(y*2):0' in sources['audio.c']
assert 'floor_q8=12u<<8' in sources['audio.c']
assert 'const uint16_t xoff = 0, yoff = 32;' in sources['screen.c']
assert 'const uint16_t xoff = 2, yoff = 1;' in sources['screen.c']
assert 'LCD_CLOCK_HZ (10 * 1000 * 1000)' in sources['screen.c']
assert 'i2c_master_probe(bus, 0x30, 200)' in sources['screen.c']
print('PASS: v1.7 audio preserved, correct LCD window offsets and LP5562 probe')

# v1.7 LCD manual-CS regression checks
assert 'LCD_HOST SPI3_HOST' in sources['screen.c']
assert '.spics_io_num = -1' in sources['screen.c']
assert 'lcd_select();' in sources['screen.c'] and 'lcd_deselect();' in sources['screen.c']
assert 'bool ok = BOBER_LCD_ST7735S ? st7735_init() : gc9107_init()' in sources['screen.c']
assert 'fill(0xffff)' not in sources['screen.c']
assert 'LCD_MADCTL_COLOR' in sources['screen.c']
assert '#define BOBER_LCD_BGR 1' in sources['screen.c']
print('PASS: v1.7 LCD hardware-SPI/manual-CS, full LCD init, BGR colors and no boot color test')
