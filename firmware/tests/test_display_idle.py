#!/usr/bin/env python3
"""Execute display timeout boundaries and production button routing without resets/audio changes."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "display_idle.h"
int main(void){
 assert(lumi_display_visible(0)&&lumi_display_visible(59999));assert(!lumi_display_visible(60000));
 assert(!lumi_display_visible(3600000));lumi_display_wake(3600000);
 assert(lumi_display_visible(3659999));assert(!lumi_display_visible(3660000));
 lumi_display_wake(UINT32_MAX-1000);assert(lumi_display_visible(1000));assert(!lumi_display_visible(60000));
 puts("PASS: 60 s since boot/wake, exact boundary and clock wrap");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(s)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'main'),str(p/'test.c'),str(root/'main/display_idle.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

# Compile the production click/hold routing, so a dark-screen click cannot reboot.
b=(root/'main/button.c').read_text()
route=b[b.index('  if(event==BOBER_BUTTON_HOLD)'):b.index('  vTaskDelay(pdMS_TO_TICKS(10));',b.index('  if(event==BOBER_BUTTON_HOLD)'))]
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "display_idle.h"
#define BOBER_BUTTON_HOLD 2
#define BOBER_BUTTON_CLICK 1
#define ESP_LOGI(...) ((void)0)
static uint32_t now;static int resets,sleeps;
static uint32_t bober_now_ms(void){return now;}
static void esp_restart(void){resets++;}
static void bober_power_sleep(void){sleeps++;}
static void dispatch(int event){
'''
main=r'''
}
int main(void){
 now=70000;assert(!lumi_display_visible(now));dispatch(BOBER_BUTTON_CLICK);
 assert(!resets&&!sleeps&&lumi_display_visible(now));
 now=129999;assert(lumi_display_visible(now));now=130000;assert(!lumi_display_visible(now));
 dispatch(BOBER_BUTTON_HOLD);assert(sleeps==1&&!resets&&!lumi_display_visible(now));
 dispatch(BOBER_BUTTON_CLICK);assert(lumi_display_visible(now)&&!resets);
 now++;dispatch(BOBER_BUTTON_CLICK);assert(resets==1);
 puts("PASS: production dark-screen click wakes only; lit-screen reset and long hold retained");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(prefix+route+main)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'main'),str(p/'test.c'),str(root/'main/display_idle.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
