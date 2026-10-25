#!/usr/bin/env python3
"""Generate the six bundled fillers once. Requires ELEVENLABS_API_KEY; costs TTS credits."""
import json, os, urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
TEXTS=['Hmm…','Niech pomyślę…','Chwileczkę…','Daj mi sekundę…','Już myślę…','Moment…']
profile=json.loads((ROOT/'agent-profile.json').read_text())['tts']
folder=ROOT/'main/assets/fillers'; folder.mkdir(exist_ok=True)
for i,text in enumerate(TEXTS):
    target=folder/f'filler_{i}.pcm'
    if target.exists(): continue  # Never silently charge for existing assets again.
    req=urllib.request.Request('https://api.elevenlabs.io/v1/text-to-speech/'+profile['voice_id']+'/stream?output_format=pcm_16000',data=json.dumps({'text':text,'model_id':profile['model_id'],'language_code':'pl'}).encode(),headers={'xi-api-key':os.environ['ELEVENLABS_API_KEY'],'Content-Type':'application/json'})
    with urllib.request.urlopen(req,timeout=60) as response: audio=response.read()
    if not audio or len(audio)%2 or len(audio)>96000: raise ValueError('Invalid or excessive PCM duration')
    target.write_bytes(audio)
    print(i,text,len(audio),flush=True)
(folder/'manifest.json').write_text(json.dumps({'voice_id':profile['voice_id'],'model_id':profile['model_id'],'format':'pcm_s16le_16000_mono','texts':TEXTS},ensure_ascii=False,indent=2)+'\n')
