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
