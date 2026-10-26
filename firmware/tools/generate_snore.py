#!/usr/bin/env python3
"""Synthesize a short nasal snore locally, mono PCM16/16 kHz; no paid API."""
import argparse,math,random,struct,wave
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--preview',type=Path);args=p.parse_args()
rate=16000;duration=.65;rng=random.Random(20261026);phase=0;noise=0;audio=[]
for i in range(round(rate*duration)):
 t=i/rate;u=t/duration
 phase+=2*math.pi*(83+15*math.sin(math.pi*u)+3*math.sin(2*math.pi*19*t))/rate
 noise=.75*noise+.25*rng.uniform(-1,1)
 breath=math.sin(math.pi*u)**1.25
 rattle=.55+.45*math.sin(2*math.pi*(23*t+6*t*t))**2
 voiced=sum(math.sin(k*phase)/(k**.7) for k in range(1,11))
 nasal=math.sin(2*math.pi*690*t)*(.4+.6*max(0,math.sin(phase)))
 value=breath*rattle*(1050*voiced+1400*noise+320*nasal)
 audio.append(max(-32768,min(32767,round(value))))
raw=struct.pack('<'+'h'*len(audio),*audio)
(root/'main/assets/lumi_snore.pcm').write_bytes(raw)
if args.preview:
 with wave.open(str(args.preview),'wb') as w:
  w.setparams((1,2,rate,0,'NONE','not compressed'));w.writeframes(raw)
print('Generated local snore: 650 ms, peak',max(map(abs,audio)))
