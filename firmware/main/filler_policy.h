#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
/* Long utterances only; at most one in three eligible turns and once a minute. */
static inline bool lumi_filler_eligible(const char *text,int64_t now,int64_t last,uint32_t chance){
 size_t words=0;bool in_word=false;
 for(const unsigned char *p=(const unsigned char*)text;*p;p++){
  bool space=*p==' '||*p=='\n'||*p=='\t'||*p=='\r';
  if(!space&&!in_word)words++;
  in_word=!space;
 }
 return words>=20&&(!last||(now>=last&&now-last>=60000))&&chance%3==0;
}
