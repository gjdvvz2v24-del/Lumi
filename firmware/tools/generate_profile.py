#!/usr/bin/env python3
"""Generate firmware session settings from the editable, credential-free profile."""
from pathlib import Path
import json
root=Path(__file__).resolve().parents[1]
a=json.loads((root/'agent-profile.json').read_text())
instructions=(root/a['instructions_file']).read_text(encoding='utf-8-sig').strip()
if not instructions or len(instructions.encode('utf-8'))>15000 or any(ord(c)<32 and c not in '\n\r\t' for c in instructions):
    raise ValueError('prompt.txt: expected nonempty UTF-8 text up to 15000 bytes, without control characters')
session={'type' :'session.update','session':{'type':'realtime','output_modalities':['text'],'instructions':instructions,'max_output_tokens':a.get('max_output_tokens',1024),'tools':a['tools'],'tool_choice':'auto','audio':{'input':{'format':{'type':'audio/pcm','rate':24000},'transcription':a['transcription'],'turn_detection':{'type':'server_vad','threshold':0.5,'prefix_padding_ms':300,'silence_duration_ms':650,'create_response':True,'interrupt_response':False}}}}}
sol=a.get('sol',{})
if sol.get('reasoning_effort','low') not in ('low','medium','high','xhigh','max') or not 1024<=sol.get('max_output_tokens',1024)<=8192:
    raise ValueError('Invalid experimental Sol reasoning settings')
(root/'main/openai_profile.h').write_text('#pragma once\n#define LUMI_SOL_REASONING_EFFORT '+json.dumps(sol.get('reasoning_effort','low'))+'\n#define LUMI_SOL_MAX_OUTPUT_TOKENS '+str(sol.get('max_output_tokens',1024))+'\n#define BOBER_OPENAI_MODEL '+json.dumps(a['model'])+'\n#define BOBER_TTS_MODEL '+json.dumps(a['tts']['model_id'])+'\n#define BOBER_SINGING_MODEL '+json.dumps(a['tts'].get('singing_model_id','eleven_v4'))+'\nstatic const char bober_session_update[] = '+json.dumps(json.dumps(session,ensure_ascii=False),ensure_ascii=False)+';\nstatic const char bober_greeting[] = '+json.dumps(a['first_message'],ensure_ascii=False)+';\n')
print('Updated main/openai_profile.h; rebuild firmware to apply.')

voice=a['tts']['voice_id']
if not voice or not all(c.isascii() and (c.isalnum() or c in '_-') for c in voice):
    raise ValueError('Invalid TTS voice ID')
(root/'main/voice_profile.h').write_text('#pragma once\n#define BOBER_DEFAULT_VOICE '+json.dumps(voice)+'\n')
