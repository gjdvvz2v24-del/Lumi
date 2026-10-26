#!/usr/bin/env python3
"""Generate the cached Music announcement once (one paid TTS request)."""
import json,os,urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=root/'main/assets/music_wait.pcm'
if p.exists():
 print('Cached Music announcement already exists; no API request made.')
 raise SystemExit(0)
profile=json.loads((root/'agent-profile.json').read_text())['tts']
text='Dobra, daj mi chwilę.'
req=urllib.request.Request('https://api.elevenlabs.io/v1/text-to-speech/'+profile['voice_id']+'/stream?output_format=pcm_16000',data=json.dumps({'text':text,'model_id':profile['model_id'],'language_code':'pl'}).encode(),headers={'xi-api-key':os.environ['ELEVENLABS_API_KEY'],'Content-Type':'application/json'})
with urllib.request.urlopen(req,timeout=60) as r:
 if r.headers.get('Content-Type','').split(';')[0]!='audio/pcm':raise ValueError('Expected raw PCM')
 audio=r.read()
if not audio or len(audio)%2 or len(audio)>128000:raise ValueError('Invalid announcement PCM')
p.write_bytes(audio)
p.with_suffix('.json').write_text(json.dumps({'text':text,'voice_id':profile['voice_id'],'model_id':profile['model_id'],'format':'pcm_s16le_16000_mono','duration_ms':len(audio)//32},ensure_ascii=False,indent=2)+'\n')
print('Cached Music announcement:',len(audio)//32,'ms')
