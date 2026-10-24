#include "animation.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned seed=17;
static unsigned rnd(void) { seed=seed*1664525u+1013904223u;return seed; }
int main(int argc,char **argv) {
    assert(argc==2);
    FILE *f=fopen(argv[1],"rb");assert(f);
    fseek(f,0,SEEK_END);size_t size=(size_t)ftell(f);rewind(f);
    unsigned char *original=malloc(size),*mutated=malloc(size),*output=malloc(32768);
    assert(original&&mutated&&output);assert(fread(original,1,size,f)==size);fclose(f);
    for (unsigned i=0;i<2500;i++) {
        memcpy(mutated,original,size);
        for (unsigned j=0;j<8;j++) mutated[rnd()%size]^=(unsigned char)rnd();
        bober_animation_t animation;
        size_t length=i%5?size:rnd()%size;
        if (bober_animation_open(&animation,mutated,length))
            (void)bober_animation_decode(&animation,rnd()%animation.count,output,32768);
    }
    free(output);free(mutated);free(original);puts("PASS: 2500 corrupted assets under ASan/UBSan");
    return 0;
}
