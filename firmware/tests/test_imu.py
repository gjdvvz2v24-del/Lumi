#!/usr/bin/env python3
"""Compile the actual IMU driver against a fake BMI270/I2C bus. Not hardware QA."""
import pathlib, subprocess, tempfile
root = pathlib.Path(__file__).resolve().parents[1]
files = {
"driver/i2c_master.h": r'''
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
typedef struct { int dev_addr_length,device_address,scl_speed_hz; } i2c_device_config_t;
#define ESP_OK 0
#define I2C_ADDR_BIT_LEN_7 0
int i2c_master_probe(void *,int,int);
int i2c_master_bus_add_device(void *,const i2c_device_config_t *,void **);
int i2c_master_bus_rm_device(void *);
int i2c_master_transmit(void *,const uint8_t *,size_t,int);
int i2c_master_transmit_receive(void *,const uint8_t *,size_t,uint8_t *,size_t,int);
''',
"freertos/FreeRTOS.h": '#pragma once\ntypedef unsigned TickType_t;\n#define pdMS_TO_TICKS(x) (x)\n',
"freertos/task.h": '#pragma once\nvoid vTaskDelay(unsigned);\n',
"esp_log.h": '#pragma once\n#define ESP_LOGI(tag,...) ((void)(tag))\n#define ESP_LOGW(tag,...) ((void)(tag))\n',
"test.c": r'''
#include "imu.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "bmi270_config.h"
static uint8_t regs[256],config[8192];
static size_t address,written;
static int fail_probe,fail_read,fail_write,bad_status,removed,reads;
void vTaskDelay(unsigned ms) { (void)ms; }
int i2c_master_probe(void *bus,int addr,int timeout) {
    assert(bus && addr==0x68 && timeout<=20);return fail_probe;
}
int i2c_master_bus_add_device(void *b,const i2c_device_config_t *c,void **d) {
    assert(b && c->device_address==0x68 && c->scl_speed_hz==400000);
    *d=(void *)2;return 0;
}
int i2c_master_bus_rm_device(void *d) { assert(d);removed++;return 0; }
int i2c_master_transmit(void *d,const uint8_t *p,size_t n,int timeout) {
    assert(d && n>=2 && n<=129 && timeout<=20);
    if(fail_write)return -1;
    if(p[0]==0x7e) { assert(p[1]==0xb6);written=0; }
    else if(p[0]==0x5b) { assert(n==3);address=2*(p[1]+16*p[2]); }
    else if(p[0]==0x5e) {
        assert(regs[0x7c]==0 && regs[0x59]==0 && address==written);
        assert(address+n-1<=sizeof(config));
        memcpy(config+address,p+1,n-1);written+=n-1;
    } else {
        if(p[0]==0x59 && p[1]==1) {
            assert(written==8192 && !memcmp(config,bober_bmi270_config,8192));
            regs[0x21]=bad_status?2:1;
        }
        memcpy(regs+p[0],p+1,n-1);
    }
    return 0;
}
int i2c_master_transmit_receive(void *d,const uint8_t *p,size_t n,uint8_t *out,size_t len,int timeout) {
    assert(d && n==1 && timeout<=10);reads++;
    if(fail_read)return -1;
    memcpy(out,regs+*p,len);return 0;
}
int main(void) {
    assert(!bober_imu_init(NULL));
    fail_probe=1;assert(!bober_imu_init((void *)1));fail_probe=0;
    assert(!bober_imu_init((void *)1));assert(removed==1); /* wrong chip */
    regs[0]=0x24;fail_write=1;assert(!bober_imu_init((void *)1));fail_write=0;
    bad_status=1;assert(!bober_imu_init((void *)1));bad_status=0;
    assert(bober_imu_init((void *)1));
    assert(regs[0x40]==0xa8 && regs[0x41]==2 && regs[0x7d]==4);
    const uint8_t sample[]={0,0xc0,0xff,0x7f,0,0x80};
    memcpy(regs+0x0c,sample,6);int16_t x,y,z;
    assert(bober_imu_read(&x,&y,&z));assert(x==-16384 && y==32767 && z==-32768);
    fail_read=1;
    for(int i=0;i<3;i++)assert(!bober_imu_read(&x,&y,&z));
    int before=reads;
    for(int i=0;i<100;i++)assert(!bober_imu_read(&x,&y,&z));
    assert(before==reads); /* dead bus cannot stall every display frame */
    fail_read=0;assert(bober_imu_suspend());
    assert(regs[0x7d]==0&&regs[0x7c]==1);
    assert(!bober_imu_read(&x,&y,&z));
    fail_write=1;assert(!bober_imu_suspend());
    puts("PASS: BMI270 init/config bytes/accel registers/signed data/failures/bounded retries");
}
'''}
with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    for name, content in files.items():
        p = tmp/name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(content)
    binary=tmp/'test'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                    '-I'+str(tmp),'-I'+str(root/'main'),str(root/'main/imu.c'),
                    str(tmp/'test.c'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
