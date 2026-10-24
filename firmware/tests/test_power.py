#!/usr/bin/env python3
"""Compile and execute production sleep coordinator with hardware/RTOS fakes."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
shim=r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <assert.h>
#include <setjmp.h>
#include "power.h"
#define ESP_OK 0
#define ESP_RST_SW 3
#define ESP_SLEEP_WAKEUP_ALL 0
#define ESP_SLEEP_WAKEUP_GPIO 7
#define GPIO_NUM_41 41
#define GPIO_INTR_LOW_LEVEL 0
#define BOBER_OFF 0
extern bool g_session_enabled,g_ws_ready;
extern unsigned g_generation;
extern int g_phase;
#define RTC_NOINIT_ATTR
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGI(tag,...) ((void)(tag))
#define ESP_LOGE(tag,...) ((void)(tag))
#define ESP_LOGW(tag,...) ((void)(tag))
typedef int esp_err_t;
typedef unsigned *EventGroupHandle_t;
EventGroupHandle_t xEventGroupCreate(void);
unsigned xEventGroupGetBits(EventGroupHandle_t);
unsigned xEventGroupSetBits(EventGroupHandle_t,unsigned);
void vTaskSuspend(void *);
void vTaskDelay(unsigned);
int esp_reset_reason(void);
void esp_restart(void);
int gpio_get_level(int);
int gpio_wakeup_enable(int,int);
int esp_sleep_disable_wakeup_source(int);
int esp_sleep_enable_gpio_wakeup(void);
int esp_light_sleep_start(void);
int esp_sleep_get_wakeup_cause(void);
int64_t bober_now_ms(void);

bool bober_wifi_suspend(void);
bool bober_audio_suspend(void);
bool bober_imu_suspend(void);
'''
test=r'''
#include "bober.h"
static unsigned bits,sleep_calls,phase;
static int reset_reason,scenario;
static int64_t now;
static jmp_buf restart_jump,park_jump;
EventGroupHandle_t xEventGroupCreate(void){bits=0;return &bits;}
unsigned xEventGroupGetBits(EventGroupHandle_t g){return *g;}
unsigned xEventGroupSetBits(EventGroupHandle_t g,unsigned b){return *g|=b;}
void vTaskSuspend(void *p){(void)p;longjmp(park_jump,1);}
void vTaskDelay(unsigned t){
 now+=t;
 if(scenario!=1&&scenario!=5&&now>=100)bits=0x3f;
}
int esp_reset_reason(void){return reset_reason;}
void esp_restart(void){reset_reason=ESP_RST_SW;longjmp(restart_jump,1);}
int gpio_get_level(int pin){
 assert(pin==41);
 if(scenario==5)return now<200?1:now<240?0:1; /* second press cancels preparation */
 return now>=300; /* original hold released before actual sleep */
}
int gpio_wakeup_enable(int pin,int level){assert(pin==41&&level==0&&phase==4);phase=5;return scenario==3?-1:0;}
int esp_sleep_disable_wakeup_source(int source){assert(source==0&&phase==3);phase=4;return 0;}
int esp_sleep_enable_gpio_wakeup(void){assert(phase==5);phase=6;return 0;}
int esp_light_sleep_start(void){assert(phase==6&&now>=340&&bits==0x3f);sleep_calls++;return scenario==4?-1:0;}
int esp_sleep_get_wakeup_cause(void){return 7;}
int64_t bober_now_ms(void){return now;}
bool g_session_enabled=true,g_ws_ready=true;
unsigned g_generation=1;
int g_phase=3;
bool bober_wifi_suspend(void){assert(phase==0&&!g_session_enabled&&!g_ws_ready&&g_phase==BOBER_OFF);phase=1;return scenario!=2;}
bool bober_audio_suspend(void){assert(phase==1&&bits==0x3f);phase=2;return true;}
bool bober_imu_suspend(void){assert(phase==2);phase=3;return true;}
int main(void){
 for(scenario=0;scenario<=5;scenario++){
  now=0;phase=0;sleep_calls=0;reset_reason=0;
  assert(!bober_power_woke());assert(bober_power_init());
  bober_power_register(BOBER_POWER_MOTION);
  if(!setjmp(restart_jump))bober_power_sleep();
  assert(bober_power_woke());assert(!bober_power_woke()); /* one-shot cookie */
  if(scenario==0||scenario==4)assert(sleep_calls==1);else assert(!sleep_calls);
  if(scenario==1)assert(now>=45000); /* unparked worker cannot enter sleep */
 }
 assert(bober_power_init());bober_power_checkpoint(BOBER_POWER_MIC);assert(!bits);
 /* Parked flag cannot run the task past its safe boundary. */
 scenario=0;now=0;phase=0;
 if(!setjmp(restart_jump))bober_power_sleep();
 bits=0;
 if(!setjmp(park_jump))bober_power_checkpoint(BOBER_POWER_MIC);
 assert(bits==BOBER_POWER_MIC);
 puts("PASS: orderly shutdown, hold release, GPIO wake, one-shot reboot marker, timeout/cancel/failure recovery and task parking");
}
'''
with tempfile.TemporaryDirectory() as d:
 tmp=Path(d);(tmp/'bober.h').write_text(shim)
 for f in ['imu.h','freertos/event_groups.h','freertos/task.h','driver/gpio.h','esp_sleep.h','esp_system.h','esp_attr.h','esp_log.h']:
  p=tmp/f;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#include "bober.h"\n')
 (tmp/'power.c').write_text((root/'main/power.c').read_text());(tmp/'test.c').write_text(test)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(tmp),'-I'+str(root/'main'),str(tmp/'power.c'),str(tmp/'test.c'),'-o',str(tmp/'test')],check=True)
 subprocess.run([str(tmp/'test')],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
