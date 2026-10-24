from pathlib import Path
import json,websocket
r=Path(__file__).resolve().parents[1]
s=(r/'main/openai_profile.h').read_text()
line=next(x for x in s.splitlines() if x.startswith('static const char bober_session_update'))
event=json.loads(json.loads(line.split(' = ',1)[1][:-1]))
key=(Path.home()/'.config/bober/openai.key').read_text().strip()
ws=websocket.create_connection('wss://api.openai.com/v1/realtime?model=gpt-realtime-2.1',header=['Authorization: Bearer '+key],timeout=25)
try:
 ws.send(json.dumps(event))
 for _ in range(200):
  e=json.loads(ws.recv());print('event:',e.get('type'))
  if e.get('type')=='error':
   raise RuntimeError(e.get('error',{}).get('code','API error'))
  if e.get('type')=='session.updated':
   (r/'live-session-check.json').write_text(json.dumps({'session_updated':True,'model':e['session'].get('model'),'output_modalities':e['session'].get('output_modalities'),'audio_input':e['session'].get('audio',{}).get('input')},indent=2))
   ws.send(json.dumps({'type':'response.create','response':{'output_modalities':['text'],'instructions':'Odpowiedz jednym zdaniem po polsku: test połączenia Bobera.'}}))
  if e.get('type')=='response.done':
   response=e['response'];print('response_status:',response.get('status'));print('status_details:',response.get('status_details'))
   if response.get('status')!='completed':raise RuntimeError('Response did not complete')
   (r/'live-response-check.json').write_text(json.dumps({'completed':True,'output':response.get('output')},ensure_ascii=False,indent=2))
   break
 else:raise RuntimeError('No completed response within event limit')
finally:ws.close()
