#!/usr/bin/env python3
"""Inspect actual generated Music payloads without paid calls."""
from pathlib import Path
import json,os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
idf=Path(os.environ.get('IDF_PATH','/Users/grpi/Work/lumi/esp-idf'))
s=r'''
#include <stdio.h>
#include <stdlib.h>
#include "music_protocol.h"
int main(void){
 for(unsigned i=0;i<256;i++){
  cJSON *p=lumi_music_payload(i*2654435761u);char *s=cJSON_PrintUnformatted(p);puts(s);free(s);cJSON_Delete(p);
 }
 for(unsigned i=0;i<32;i++){
  cJSON *p=lumi_music_payload(42);char *s=cJSON_PrintUnformatted(p);puts(s);free(s);cJSON_Delete(p);
 }
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(s)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I'+str(root/'main'),'-I'+str(idf/'components/json/cJSON'),str(p/'test.c'),str(idf/'components/json/cJSON/cJSON.c'),'-o',str(p/'test')],check=True)
 rows=[json.loads(x) for x in subprocess.check_output([str(p/'test')],text=True).splitlines()]
subjects=[]
for row in rows:
 assert row['music_length_ms']==60000 and row['model_id']=='music_v2'
 text=row['prompt'];assert 'Polish lyrics' in text and len(text)<1200
 subject=text.split('Subject: ',1)[1].split('. Setting:',1)[0]
 assert subject not in subjects[-6:]
 subjects.append(subject)
assert len(set(subjects[:256]))>=48
all_subjects=' '.join(subjects[:256])
for word in ['school','football','pizza','friend','robot','train','garden']:
 assert word in all_subjects,word
print('PASS: diverse real payloads, broad subject coverage, no subject repeats within six songs even with a stuck random seed')
