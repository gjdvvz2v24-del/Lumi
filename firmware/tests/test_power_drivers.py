#!/usr/bin/env python3
"""Production radio power gates and codec/I2S shutdown, no ESP32 hardware."""
from pathlib import Path
import subprocess,tempfile,runpy
root=Path(__file__).resolve().parents[1]
runpy.run_path(str(Path(__file__).with_name('test_wifi.py')),run_name='__main__')
a=(root/'main/audio.c').read_text()
a=a[a.index('bool bober_audio_suspend'):a.index('static inline int16_t clamp')]
prefix=r'''
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 3
#define ESP_CODEC_DEV_OK 0
typedef int esp_err_t;
static int amp=1,codec_dev=1,rx=2,tx=3,close_result,disable_result;
static unsigned audio_sequence;
static int ext_write(uint8_t reg,uint8_t value){assert(reg==5&&value==0xfe);audio_sequence=1;return 0;}
static int esp_codec_dev_close(int handle){assert(handle&&audio_sequence==1);audio_sequence=2;return close_result;}
static int i2s_channel_disable(int handle){
 if(handle==2){assert(audio_sequence==2);audio_sequence=3;}
 else {assert(handle==3&&audio_sequence==3);audio_sequence=4;}
 return disable_result;
}
'''
main=r'''
int main(void){
 assert(bober_audio_suspend()&&audio_sequence==4);
 disable_result=ESP_ERR_INVALID_STATE;assert(bober_audio_suspend());
 close_result=-1;assert(!bober_audio_suspend());
 close_result=0;disable_result=-1;assert(!bober_audio_suspend());
 puts("PASS: amplifier/codec/I2S shutdown order and failures");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(prefix+a+main)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
