"""Verify microphone cannot open over queued PCM or an undrained DMA tail."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
code = r'''
#include <assert.h>
#include "playback_gate.h"
int main(void){
 assert(!lumi_sol_mic_can_resume(false,0,false,1000,0));
 assert(!lumi_sol_mic_can_resume(true,640,false,1000,0));
 assert(!lumi_sol_mic_can_resume(true,0,true,1000,0));
 assert(!lumi_sol_mic_can_resume(true,0,false,90,0));
 assert(!lumi_sol_mic_can_resume(true,0,false,119,0));
 assert(lumi_sol_mic_can_resume(true,0,false,120,0));
 assert(lumi_sol_mic_can_resume(true,0,false,1000120,1000000));
 assert(!lumi_sol_mic_can_resume(true,0,false,100,101));
}
'''
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    (p / "test.c").write_text(code)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I" + str(root / "main"),
                    str(p / "test.c"), "-o", str(p / "test")], check=True)
    subprocess.run([str(p / "test")], check=True)
print("PASS: microphone stays gated during PCM/reactions/DMA tail; opens at 120 ms")
