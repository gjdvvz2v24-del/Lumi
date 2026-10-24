#!/usr/bin/env python3
"""Runtime prompt parsing/fallback and guarded USB write regression tests."""
from pathlib import Path
import importlib.util
import json
import os
import subprocess
import tempfile
import unittest
from unittest.mock import patch
ROOT = Path(__file__).resolve().parents[1]
IDF = Path(os.environ.get('IDF_PATH', str(ROOT.parent / 'esp-idf')))
spec = importlib.util.spec_from_file_location('upload', ROOT / 'update_prompt_usb.py')
upload = importlib.util.module_from_spec(spec);spec.loader.exec_module(upload)


class USBTests(unittest.TestCase):
    def test_invalid_file_before_any_device_access(self):
        with tempfile.TemporaryDirectory() as t:
            p = Path(t) / 'prompt.txt'
            for data in (b'', b' \n\t', b'x\x00y', b'\xff', b'a' * 15001):
                p.write_bytes(data)
                with self.assertRaises((ValueError, UnicodeError)):
                    upload.update('unused', p, runner=lambda *a, **k: self.fail('Device accessed'))
            p.write_text('\ufeffZażółć gęślą jaźń.\n', encoding='utf-8')
            self.assertEqual(upload.read_prompt(p), 'Zażółć gęślą jaźń.'.encode())

    def test_old_layout_never_written_and_new_layout_prompt_only(self):
        with tempfile.TemporaryDirectory() as t:
            root = Path(t);(root/'dist').mkdir();(root/'idf/components/spiffs').mkdir(parents=True)
            (root/'idf/components/spiffs/spiffsgen.py').touch()
            table = b'T' * 0xc00
            (root/'dist/partition-table.bin').write_bytes(table)
            (root/'dist/prompt-storage.json').write_text(json.dumps({'offset':upload.OFFSET,'size':upload.SIZE,'generator_args':[]}))
            (root/'prompt.txt').write_text('Polski prompt')
            for valid in (False, True):
                writes = []
                def runner(cmd, **kw):
                    if cmd[1].endswith('spiffsgen.py'):Path(cmd[4]).write_bytes(b'\xff' * upload.SIZE)
                    elif 'read_flash' in cmd:Path(cmd[-1]).write_bytes(table if valid else b'O' * 0xc00)
                    elif 'write_flash' in cmd:writes.append(cmd)
                    else:self.fail(str(cmd))
                with patch.object(upload,'ROOT',root), patch.dict(os.environ, {'IDF_PATH':str(root/'idf')}):
                    if valid:upload.update('test-port',root/'prompt.txt',runner)
                    else:
                        with self.assertRaises(ValueError):upload.update('test-port',root/'prompt.txt',runner)
                self.assertEqual(len(writes),int(valid))
                if valid:self.assertEqual(writes[0][-2],hex(upload.OFFSET))


def runtime_test():
    with tempfile.TemporaryDirectory() as t:
        t = Path(t)
        (t/'esp_spiffs.h').write_text('''#include <stdbool.h>
#define ESP_OK 0
typedef struct {const char *base_path,*partition_label; int max_files; bool format_if_mount_failed;} esp_vfs_spiffs_conf_t;
static bool fake_mounted;
static inline int esp_vfs_spiffs_unregister(const char *label){(void)label;fake_mounted=false;return 0;}
static inline int esp_vfs_spiffs_register(const esp_vfs_spiffs_conf_t *c){if(c->format_if_mount_failed||fake_mounted)return 1;fake_mounted=true;return 0;}
''')
        (t/'esp_log.h').write_text('#define ESP_LOGI(tag,...) ((void)(tag))\n#define ESP_LOGW(tag,...) ((void)(tag))\n')
        (t/'test.c').write_text(r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "prompt_store.h"
static const char *fallback="{\"type\":\"session.update\",\"session\":{\"instructions\":\"fallback\",\"output_modalities\":[\"text\"]}}";
static void check(const char *expected){
 char *s=bober_prompt_session(fallback);assert(s);
 cJSON *j=cJSON_Parse(s),*session=cJSON_GetObjectItem(j,"session");
 assert(!strcmp(cJSON_GetObjectItem(session,"instructions")->valuestring,expected));
 assert(!strcmp(cJSON_GetArrayItem(cJSON_GetObjectItem(session,"output_modalities"),0)->valuestring,"text"));
 cJSON_Delete(j);free(s);
}
static void writefile(const char *data,size_t n){FILE *f=fopen(BOBER_PROMPT_PATH,"wb");assert(f);assert(fwrite(data,1,n,f)==n);fclose(f);}
int main(void){
 remove(BOBER_PROMPT_PATH);check("fallback");
 const char *polish="Zażółć gęślą jaźń.\nCytat: \"Mordo\" \\ test";
 writefile(polish,strlen(polish));check(polish);
 writefile("Nowy prompt",11);check("Nowy prompt"); /* reload, no compiled cache */
 writefile("",0);check("fallback");
 writefile(" \n\t",3);check("fallback");
 writefile("a\0b",3);check("fallback");
 writefile("\xc0\x80",2);check("fallback");
 writefile("\xed\xa0\x80",3);check("fallback");
 writefile("\xf4\x90\x80\x80",4);check("fallback");
 writefile("\xe2\x82",2);check("fallback");
 char *large=malloc(15002);memset(large,'a',15001);large[15000]=0;
 writefile(large,15000);check(large);large[15000]='a';writefile(large,15001);check("fallback");free(large);
 puts("PASS: production runtime file reload, JSON escaping, UTF-8, limits and fallback; no autoformat");
}
''')
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address,undefined',f'-DBOBER_PROMPT_PATH="{t}/prompt.txt"','-I'+str(t),'-I'+str(ROOT/'main'),'-I'+str(IDF/'components/json/cJSON'),str(t/'test.c'),str(ROOT/'main/prompt_store.c'),str(IDF/'components/json/cJSON/cJSON.c'),'-o',str(t/'test')],check=True)
        subprocess.run([str(t/'test')],check=True)


if __name__ == '__main__':
    runtime_test()
    unittest.main()
