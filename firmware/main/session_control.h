#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
/* Match an entire short farewell, never a substring of a longer statement.
   Polish UTF-8 letters are folded without depending on the C locale. */
static inline bool lumi_is_goodbye(const char *text){
 char normalized[160];size_t n=0;
 const unsigned char *p=(const unsigned char *)text;
 while(*p){
  unsigned char c=*p++;char out=0;
  if(c>='A'&&c<='Z')out=(char)(c+32);
  else if(c>='a'&&c<='z')out=(char)c;
  else if(c<128){
   if(c>='0'&&c<='9')return false;
   if(c==' '||c=='\t'||c=='\r'||c=='\n'||c==','||c=='.'||c=='!'||c=='?'||c==';'||c==':')out=' ';
   else return false;
  }else{
   if(!*p)return false;
   unsigned char d=*p++;
   if(c==0xc3&&(d==0x93||d==0xb3))out='o';
   else if(c==0xc4){
    if(d==0x84||d==0x85)out='a';
    else if(d==0x86||d==0x87)out='c';
    else if(d==0x98||d==0x99)out='e';
   }else if(c==0xc5){
    if(d==0x81||d==0x82)out='l';
    else if(d==0x83||d==0x84)out='n';
    else if(d==0x9a||d==0x9b)out='s';
    else if(d==0xb9||d==0xba||d==0xbb||d==0xbc)out='z';
   }
   if(!out)return false;
  }
  if(out==' '&&(!n||normalized[n-1]==' '))continue;
  if(n+1>=sizeof(normalized))return false;
  normalized[n++]=out;
 }
 if(n&&normalized[n-1]==' ')n--;
 normalized[n]=0;
 char *s=normalized;
 const char *prefixes[]={"dobra ","dobrze ","okej ","ok "};
 for(size_t i=0;i<sizeof(prefixes)/sizeof(prefixes[0]);i++){
  size_t len=strlen(prefixes[i]);if(!strncmp(s,prefixes[i],len)){s+=len;break;}
 }
 if(!strncmp(s,"lumi ",5))s+=5;
 n=strlen(s);if(n>=5&&!strcmp(s+n-5," lumi"))s[n-5]=0;
 const char *phrases[]={"pa","pa pa","papa","do widzenia","do zobaczenia","do uslyszenia",
  "na razie","nara","dobranoc","zegnaj","zegnam","koniec rozmowy","zakoncz rozmowe",
  "konczymy rozmowe","konczymy","to wszystko","dzieki to wszystko","dziekuje to wszystko"};
 for(size_t i=0;i<sizeof(phrases)/sizeof(phrases[0]);i++)if(!strcmp(s,phrases[i]))return true;
 return false;
}
