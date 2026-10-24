"""Paid live test: requires websocket-client, ffmpeg and ELEVENLABS_API_KEY. No hardware tested."""
from pathlib import Path
import json,urllib.request,os,websocket,base64,subprocess,time
r=Path(__file__).resolve().parents[1]
key=(Path.home()/'.config/bober/openai.key').read_text().strip()
profile=json.loads((r/'agent-profile.json').read_text())
def tts(text):
 req=urllib.request.Request('https://api.elevenlabs.io/v1/text-to-speech/'+profile['tts']['voice_id']+'/stream?output_format=pcm_16000',data=json.dumps({'text':text,'model_id':profile['tts']['model_id'],'language_code':'pl'}).encode(),headers={'xi-api-key':os.environ['ELEVENLABS_API_KEY'],'Content-Type':'application/json'})
 with urllib.request.urlopen(req,timeout=30) as h:
  assert h.status==200 and 'pcm' in h.headers.get('Content-Type','');return h.read()
fixture=tts('Lumi, nie chcę teraz bajki ani zagadki. Chcę po prostu pogadać.')
audio=subprocess.run(['ffmpeg','-v','error','-f','s16le','-ar','16000','-ac','1','-i','pipe:0','-f','s16le','-ar','24000','-ac','1','pipe:1'],input=fixture,capture_output=True,check=True).stdout
line=next(x for x in (r/'main/openai_profile.h').read_text().splitlines() if x.startswith('static const char bober_session_update'))
update=json.loads(json.loads(line.split(' = ',1)[1][:-1]))
ws=websocket.create_connection('wss://api.openai.com/v1/realtime?model='+profile['model'],header=['Authorization: Bearer '+key],timeout=30)
report={'synthetic_input':True,'hardware_tested':False,'openai_model':profile['model'],'tts_model':profile['tts']['model_id'],'voice_id':profile['tts']['voice_id']}
try:
 ws.send(json.dumps(update))
 while True:
  e=json.loads(ws.recv())
  if e['type']=='error':raise RuntimeError(e['error']['code'])
  if e['type']=='session.updated':
   assert e['session']['output_modalities']==['text'] and [t['name'] for t in e['session']['tools']]==[t['name'] for t in profile['tools']]
   report['session_config_verified']=True;break
 payload=bytes(24000)+audio+bytes(48000)
 for pos in range(0,len(payload),960):
  ws.send(json.dumps({'type':'input_audio_buffer.append','audio':base64.b64encode(payload[pos:pos+960]).decode()}));time.sleep(.02)
 transcript=None;response=None
 for _ in range(250):
  e=json.loads(ws.recv())
  if e['type']=='error':raise RuntimeError(e['error']['code'])
  if e['type']=='conversation.item.input_audio_transcription.failed':raise RuntimeError('Transcription failed')
  if e['type']=='conversation.item.input_audio_transcription.completed':transcript=e['transcript'];print('Polish transcript:',transcript,flush=True)
  if e['type']=='response.done':
   assert e['response']['status']=='completed',e['response'].get('status_details')
   response=' '.join(p.get('text','') for x in e['response']['output'] for p in x.get('content',[]) if p['type']=='output_text');print('Lumi response:',response,flush=True)
  if transcript and response:break
 assert transcript and response,'Missing transcription or response'
 output=tts(response);assert output and len(output)%2==0
 report.update({'transcript':transcript,'response':response,'elevenlabs_pcm_bytes':len(output),'passed':True})
 (r/'live-audio-check.json').write_text(json.dumps(report,ensure_ascii=False,indent=2))
 print('PASS: synthetic Polish audio -> OpenAI transcription/text -> ElevenLabs PCM. No device involved.')
finally:ws.close()
