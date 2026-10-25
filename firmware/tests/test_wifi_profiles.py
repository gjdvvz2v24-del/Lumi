#!/usr/bin/env python3
"""Validate the real profile parser/USB setter and the host's no-secret protocol."""
import importlib.util,json,os,subprocess,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
idf=Path(os.environ.get('IDF_PATH','/Users/grpi/Work/lumi/esp-idf'))
spec=importlib.util.spec_from_file_location('wifi_usb',root/'configure_wifi_usb.py')
host=importlib.util.module_from_spec(spec);spec.loader.exec_module(host)
valid=[{'ssid':'Dom','password':'password1'},{'ssid':'Podróż','password':'password2'},{'ssid':'Otwarte','password':''}]
invalid=[[],valid+[{'ssid':'four','password':'password4'}],valid+[valid[0]],
         [{'ssid':'x'*33,'password':''}],[{'ssid':'Dom','password':'short'}],
         [{'ssid':'Dom','password':'x'*64}],[{'ssid':'Duplikat','password':''}]*2,
         [{'ssid':'null\0suffix','password':''}],[{'ssid':'new\nline','password':''}],
         [{'ssid':'a','password':7}],{'ssid':'wrong-root','password':''}]
assert len(host.validate_profiles(valid))<host.MAX_JSON
for value in invalid:
    try: host.validate_profiles(value)
    except ValueError: pass
    else: raise AssertionError('Invalid profiles accepted')
source=(root/'main/wake_capture.c').read_text()
setter=source[source.index('static void set_wifi_profiles'):source.index('#ifdef LUMI_SOL_ULTRAFAST')]
prefix=r'''
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "wifi_profiles.h"
#define ESP_OK 0
#define NVS_READWRITE 1
#define BOBER_WAKE_WAIT 4
typedef int nvs_handle_t;
typedef int esp_err_t;
static bool g_session_enabled;
static int g_phase=BOBER_WAKE_WAIT,writes,commits,fail_commit;
static char saved[LUMI_WIFI_JSON_MAX+1];
static int nvs_open(const char *ns,int mode,nvs_handle_t *n){assert(!strcmp(ns,"bober")&&mode==NVS_READWRITE);*n=1;return 0;}
static int nvs_set_str(int n,const char *key,const char *json){assert(n==1&&!strcmp(key,"wifi_profiles"));strcpy(saved,json);writes++;return 0;}
static int nvs_commit(int n){assert(n==1);commits++;return fail_commit?-1:0;}
static void nvs_close(int n){assert(n==1);}
'''
body='\nint main(void){lumi_wifi_profile_t profiles[3]={0};\n'
def literal(value): return json.dumps(value,ensure_ascii=False)
for value in invalid:
    body+='strcpy(profiles[0].ssid,"sentinel");assert(!lumi_wifi_profiles_parse('+literal(json.dumps(value,ensure_ascii=False))+',profiles));assert(!strcmp(profiles[0].ssid,"sentinel"));\n'
payload=host.validate_profiles(valid).decode()
body+='const char *valid='+literal(payload)+';\n'
body+='assert(lumi_wifi_profiles_parse(valid,profiles));assert(!strcmp(profiles[1].ssid,"Podróż"));\n'
body+='assert(!lumi_wifi_profiles_parse("[] junk",profiles));assert(!lumi_wifi_profiles_parse("[[[[[]]]]]",profiles));\n'
body+='g_session_enabled=true;set_wifi_profiles(valid);assert(!writes);g_session_enabled=false;\n'
body+='g_phase=0;set_wifi_profiles(valid);assert(!writes);g_phase=BOBER_WAKE_WAIT;\n'
body+='set_wifi_profiles("[]");assert(!writes);set_wifi_profiles(valid);assert(writes==1&&commits==1&&!strcmp(saved,valid));\n'
body+='fail_commit=1;set_wifi_profiles(valid);assert(writes==2&&commits==2);\n'
body+='puts("PASS: actual C parser rejects malformed/duplicate/oversized profiles, USB refuses active sessions and only writes the Wi-Fi key");}\n'
with tempfile.TemporaryDirectory() as d:
    p=Path(d);(p/'test.c').write_text(prefix+setter+body)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I'+str(root/'main'),'-I'+str(idf/'components/json/cJSON'),str(p/'test.c'),str(idf/'components/json/cJSON/cJSON.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
# Handshake must work before any credentials are sent; busy/old firmware fail first.
class FakeSerial:
    def __init__(self,lines): self.lines=iter(lines)
    def readline(self): return next(self.lines,b'')
host.wait_for(FakeSerial([b'boot log',b'LUMI_WIFI_READY']),b'LUMI_WIFI_READY')
try: host.wait_for(FakeSerial([b'LUMI_WIFI_BUSY']),b'LUMI_WIFI_READY')
except RuntimeError: pass
else: raise AssertionError('Busy device accepted')
print('PASS: host profile validation and protocol handshake, no USB or network calls')

# Execute the uploader with a fake serial module: never send secrets before READY.
import sys,types
from unittest.mock import patch
class Connection:
    def __init__(self,lines): self.lines=iter(lines);self.sent=[];self.opened=False
    def __enter__(self): return self
    def __exit__(self,*args): pass
    def open(self): assert self.port=='fake-port' and not self.dtr and not self.rts;self.opened=True
    def reset_input_buffer(self): pass
    def write(self,data): self.sent.append(data)
    def flush(self): pass
    def readline(self): return next(self.lines,b'')
for ready in (False,True):
    connection=Connection([b'LUMI_WIFI_READY',b'LUMI_WIFI_SAVED_RESTART_REQUIRED'] if ready else [b'LUMI_WIFI_BUSY'])
    with patch.dict(sys.modules,{'serial':types.SimpleNamespace(Serial=lambda **kwargs: connection)}):
        try: host.upload('fake-port',payload.encode())
        except RuntimeError: assert not ready
        else: assert ready
    assert connection.opened and connection.sent[0]==b'LUMI_WIFI_READY?\n'
    assert len(connection.sent)==(2 if ready else 1)
    if ready: assert connection.sent[1]==b'LUMI_WIFI_SET '+payload.encode()+b'\n'
print('PASS: real uploader sends only handshake to busy firmware; full update requires READY and saved acknowledgement')
