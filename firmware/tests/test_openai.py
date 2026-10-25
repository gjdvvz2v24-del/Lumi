#!/usr/bin/env python3
"""Host execution of production event handling, resampling and USB validation."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
IDF=Path(os.environ.get('IDF_PATH', str(ROOT.parent/'esp-idf')))
source=(ROOT/'main/eleven.c').read_text()
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "openai_protocol.h"
#include "session_control.h"
#include <stdatomic.h>
#define BOBER_OFF 0
#define BOBER_WAKE_WAIT 4
static atomic_bool wake_wait_requested,end_after_reply;
#define BOBER_OPENAI_DIAGNOSTICS 1
#define BOBER_LISTENING 3
#define BOBER_SPEAKING 2
#define BOBER_CONNECTING 1
#define pdTRUE 1
#define ESP_LOGI(tag,fmt,...) ((void)snprintf(trace_last,sizeof(trace_last),fmt,##__VA_ARGS__))
static char trace_last[8192];
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
typedef struct {uint32_t generation;char text[4096];} reply_t;
static reply_t queued;
static bool configured,g_ws_ready,g_ws_reconnect,g_response_complete;
static bool g_session_enabled=true,g_wifi_ready=true,power;
static int g_phase,replies,queue_count;static uint32_t ws_generation=3,g_generation=3;
static long g_last_tts_begin_ms,g_last_pcm_ms;
static bool cue_push_ok=true;static unsigned cue_pushes;
static bool bober_pcm_push(const uint8_t *p,size_t n,uint32_t gen){assert(p&&n==2400&&gen==3);cue_pushes++;return cue_push_ok;}
static char message[32768];
static int64_t bober_now_ms(void){return 100;}
static bool bober_power_requested(void){return power;}
static void bober_pcm_reset(void){}
static int xQueueSend(int q,reply_t*r,int timeout){(void)q;(void)timeout;if(queue_count)return 0;queued=*r;queue_count++;return 1;}
'''
body=source[source.index('static bool current('):source.index('static bool send_text(')]
body+=source[source.index('static const char *str('):source.index('static void ws_event(')]
main=r'''
static void event(const char *s){strlcpy(message,s,sizeof(message));handle_message();}
int main(void){
 event("{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"turn-test\",\"transcript\":\"Bober\\nCześć\"}");
 assert(strstr(trace_last,"turn-test")&&strstr(trace_last,"Bober\\nCześć")&&!strchr(trace_last,'\n'));
 assert(current(3));assert(!current(2));power=true;assert(!current(3));power=false;
 g_wifi_ready=false;assert(!current(3));g_wifi_ready=true;g_session_enabled=false;assert(!current(3));g_session_enabled=true;
 event("{\"type\":\"response.created\"}");assert(g_phase==BOBER_SPEAKING&&!g_response_complete);
 event("{\"type\":\"response.done\",\"response\":{\"status\":\"completed\",\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"output_text\",\"text\":\"Żółw mówi po polsku.\"}]}]}}");
 assert(strstr(trace_last,"VOICE_TRACE")&&strstr(trace_last,"Żółw mówi po polsku."));
 assert(queue_count==1&&!strcmp(queued.text,"Żółw mówi po polsku.")&&queued.generation==3);
 assert(!g_response_complete); /* text completion is NOT playback completion */
 queue_count=0;
 event("{\"type\":\"response.done\",\"response\":{\"status\":\"incomplete\"}}");assert(!g_ws_reconnect&&queue_count==1&&!g_response_complete);queue_count=0;
 event("{\"type\":\"response.done\",\"response\":{\"status\":\"incomplete\",\"status_details\":{\"reason\":\"max_output_tokens\"},\"usage\":{\"output_tokens\":250}}}");
 assert(!g_ws_reconnect&&queue_count==1&&strstr(queued.text,"limit")&&strstr(trace_last,"max_output_tokens")&&strstr(trace_last,"250"));queue_count=0;
 event("{\"type\":\"response.done\",\"response\":{\"status\":\"incomplete\",\"status_details\":{\"reason\":\"content_filter\"},\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"output_text\",\"text\":\"PARTIAL_SENTINEL\"}]}]}}");
 assert(!g_ws_reconnect&&queue_count==1&&strstr(queued.text,"zmienić temat")&&!strstr(queued.text,"PARTIAL_SENTINEL"));queue_count=0;
 event("{\"type\":\"response.done\",\"response\":{\"status\":\"failed\"}}");assert(g_ws_reconnect);g_ws_reconnect=false;
 event("{\"type\":\"response.done\",\"response\":{\"status\":\"completed\",\"output\":[{\"type\":\"function_call\"}]}}");assert(g_ws_reconnect&&queue_count==0);g_ws_reconnect=false;
 event("{\"type\":\"response.done\",\"response\":{\"status\":\"completed\",\"output\":[]}}");assert(g_response_complete&&queue_count==0);
 event("{\"type\":\"error\"}");assert(g_ws_reconnect);g_ws_reconnect=false;
 event("broken");assert(g_ws_reconnect);g_ws_reconnect=false;
 event("{\"type\":\"session.updated\",\"session\":{\"output_modalities\":[\"text\"],\"tools\":[{\"type\":\"function\",\"name\":\"end_conversation\"}],\"audio\":{\"input\":{\"format\":{\"type\":\"audio/pcm\",\"rate\":24000},\"transcription\":{\"language\":\"pl\"}}}}}");assert(configured&&g_ws_ready&&!g_ws_reconnect);
 event("{\"type\":\"session.updated\",\"session\":{\"output_modalities\":[\"audio\"],\"tools\":[]}}");assert(!configured&&g_ws_reconnect);
 char too_long[4097];memset(too_long,'x',4096);too_long[4096]=0;g_ws_reconnect=false;queue_reply(too_long);assert(g_ws_reconnect&&queue_count==0);
 g_ws_reconnect=false;g_session_enabled=true;g_phase=BOBER_SPEAKING;
 event("{\"type\":\"response.done\",\"response\":{\"status\":\"completed\",\"output\":[{\"type\":\"function_call\",\"name\":\"end_conversation\",\"arguments\":\"{\\\"farewell\\\":\\\"Pa, Zosiu!\\\"}\"}]}}");
 assert(!g_ws_reconnect&&queue_count==1&&!strcmp(queued.text,"Pa, Zosiu!"));
 assert(g_session_enabled&&g_phase==BOBER_SPEAKING&&atomic_load(&end_after_reply));
 event("{\"type\":\"response.created\"}"); /* late callback must not reset goodbye */
 assert(queue_count==1&&atomic_load(&end_after_reply));queue_count=0;
 reply_playback_finished(); /* production invokes this only after speaker drain */
 assert(!g_session_enabled&&g_phase==BOBER_OFF&&atomic_load(&wake_wait_requested));
 finish_wake_wait();assert(g_phase==BOBER_WAKE_WAIT&&!g_session_enabled);
 g_session_enabled=true;g_phase=BOBER_SPEAKING;reply_playback_finished();
 assert(g_session_enabled&&g_phase==BOBER_SPEAKING&&mic_ready_cue_active&&g_response_complete);
 reply_playback_finished();assert(g_phase==BOBER_LISTENING&&!mic_ready_cue_active);
 cue_push_ok=false;reply_playback_finished();assert(g_phase==BOBER_LISTENING&&!mic_ready_cue_active);cue_push_ok=true;
 assert(cue_pushes==2); /* one cue per reply, no repeated cue on cue completion */
 event("{\"type\":\"response.done\",\"response\":{\"status\":\"completed\",\"output\":[{\"type\":\"function_call\",\"name\":\"end_conversation\",\"arguments\":\"{\\\"farewell\\\":42}\"}]}}");
 assert(g_ws_reconnect&&!atomic_load(&end_after_reply)&&!queue_count);g_ws_reconnect=false;
 event("{\"type\":\"response.done\",\"response\":{\"status\":\"completed\",\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"output_text\",\"text\":\"Pa, Zosiu!\"}]},{\"type\":\"function_call\",\"name\":\"end_conversation\",\"arguments\":\"{\\\"farewell\\\":\\\"Pa, Zosiu!\\\"}\"}]}}");
 assert(queue_count==1&&!strcmp(queued.text,"Pa, Zosiu!"));queue_count=0;
 reply_playback_finished();finish_wake_wait();assert(g_phase==BOBER_WAKE_WAIT);
 const char *yes[]={"Pa!","Pa, pa, Lumi!","Do widzenia.","DO USŁYSZENIA!","Dobranoc, Lumi.","Lumi, do zobaczenia!","Dobra, Lumi, na razie.","Zakończ rozmowę","Dzięki, to wszystko."};
 for(size_t i=0;i<sizeof(yes)/sizeof(yes[0]);i++)assert(lumi_is_goodbye(yes[i]));
 const char *no[]={"cześć","powiedz pa","nie kończymy rozmowy","jak powiedzieć do widzenia po angielsku","do zobaczenia to tytuł filmu","pa ale jeszcze jedno pytanie","","Lumi","stop","papaja","Do widzenia 123"};
 for(size_t i=0;i<sizeof(no)/sizeof(no[0]);i++)assert(!lumi_is_goodbye(no[i]));
 g_session_enabled=true;g_phase=BOBER_LISTENING;g_ws_ready=true;g_ws_reconnect=false;
 event("{\"type\":\"conversation.item.input_audio_transcription.completed\",\"transcript\":\"Pa, Lumi!\"}");
 assert(!g_session_enabled&&!g_ws_ready&&g_phase==BOBER_OFF&&atomic_load(&wake_wait_requested));
 assert(!current(3)); /* stale callbacks/uplink/TTS cannot continue */
 finish_wake_wait();assert(g_phase==BOBER_WAKE_WAIT&&!g_ws_reconnect&&!g_session_enabled);
 finish_wake_wait();assert(g_phase==BOBER_WAKE_WAIT&&!g_session_enabled); /* no automatic reconnect */
 request_wake_wait();power=true;finish_wake_wait();assert(g_phase==BOBER_OFF);power=false;
 request_wake_wait();finish_wake_wait();assert(g_phase==BOBER_WAKE_WAIT);
 bober_resampler_t a={0},b={0};int16_t input[320],one[480],split[480];
 for(int i=0;i<320;i++)input[i]=(int16_t)(i*200-32000);
 assert(bober_resample(&a,input,320,one)==480);
 size_t n=0;for(int i=0;i<320;i++)n+=bober_resample(&b,input+i,1,split+n);
 assert(n==480&&!memcmp(one,split,sizeof(one)));
 a=(bober_resampler_t){0};for(int i=0;i<320;i++)input[i]=-32768;
 assert(bober_resample(&a,input,320,one)==480);for(int i=0;i<480;i++)assert(one[i]==-32768);
 puts("PASS: event handling, Polish text, no early mic resume, failures, end-conversation tool and drain, unknown tools rejection, network/mode gate, streaming resampling");
}
'''
with tempfile.TemporaryDirectory() as d:
    c=Path(d)/'test.c';c.write_text(prefix+body+main)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I'+str(ROOT/'main'),'-I'+str(IDF/'components/json/cJSON'),str(c),str(IDF/'components/json/cJSON/cJSON.c'),'-o',str(Path(d)/'test')],check=True)
    subprocess.run([str(Path(d)/'test')],check=True)
spec=importlib.util.spec_from_file_location('usb',ROOT/'configure_usb.py');usb=importlib.util.module_from_spec(spec);spec.loader.exec_module(usb)
# Never read actual local credentials in unit tests.
class UsbTest(unittest.TestCase):
    def setUp(self):
        self.home=tempfile.TemporaryDirectory();self.addCleanup(self.home.cleanup)
        self.home_patch=patch.object(usb.Path,'home',return_value=Path(self.home.name));self.home_patch.start();self.addCleanup(self.home_patch.stop)
    def test_saved_key(self):
        folder=Path(self.home.name)/'.config/bober';folder.mkdir(parents=True)
        (folder/'openai.key').write_text('local-test-key')
        with patch('builtins.input',side_effect=['wifi','']),patch.object(usb.getpass,'getpass',side_effect=['password','test-eleven']):
            values=usb.ask_values()
        self.assertEqual(values['openai'],'local-test-key')
    def test_keys_and_voice(self):
        with patch('builtins.input',side_effect=['wifi','']),patch.object(usb.getpass,'getpass',side_effect=['password','test-openai','test-eleven']):
            values=usb.ask_values()
        self.assertEqual(set(values),{'ssid','pass','openai','key','voice'})
        self.assertEqual(values['voice'],usb.DEFAULT_VOICE)
    def test_header_injection_rejected(self):
        with patch('builtins.input',side_effect=['wifi','']),patch.object(usb.getpass,'getpass',side_effect=['password','key\r\nInjected: yes','test-eleven']):
            with self.assertRaises(ValueError):usb.ask_values()
    def test_empty_key_rejected(self):
        with patch('builtins.input',side_effect=['wifi','']),patch.object(usb.getpass,'getpass',side_effect=['password','','test-eleven']):
            with self.assertRaises(ValueError):usb.ask_values()
    def test_voice_url_injection_rejected(self):
        with patch('builtins.input',side_effect=['wifi','voice?evil=1']),patch.object(usb.getpass,'getpass',side_effect=['password','test-openai','test-eleven']):
            with self.assertRaises(ValueError):usb.ask_values()
    def test_partition(self):self.assertEqual(usb.nvs_layout(),(0x9000,0x6000))
unittest.main()
