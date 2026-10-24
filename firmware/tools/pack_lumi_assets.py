#!/usr/bin/env python3
"""Rebuild display and local PCM assets from the Lumi project originals."""
from pathlib import Path
import sys,subprocess
from PIL import Image
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r))
from pack_animation import pack
frames=[Image.open(p).convert('RGB').resize((128,128),Image.Resampling.LANCZOS) for p in sorted((r.parent/'artwork/zwierzatko-8').glob('*.png'))]
if len(frames)!=8:raise ValueError('Expected all eight Lumi illustrations')
gif=r/'main/assets/lumi-slideshow.gif'
frames[0].save(gif,save_all=True,append_images=frames[1:],duration=3000,loop=0,optimize=False)
pack(gif,r/'main/assets/loop.banim',15) # render rate; slideshow cadence is 3s in C
for kind in ('impact','shake'):
 subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-i',str(r.parent/f'audio/{kind}-source.mp3'),'-ac','1','-ar','16000','-af','silenceremove=start_periods=1:start_duration=0.01:start_threshold=-45dB,areverse,silenceremove=start_periods=1:start_duration=0.05:start_threshold=-45dB,areverse,alimiter=limit=0.5:level=false,afade=t=in:d=0.01','-f','s16le',str(r/f'main/assets/lumi_{kind}.pcm')],check=True)
