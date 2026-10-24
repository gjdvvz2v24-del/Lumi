#include "wake_detector.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstring>
#include <cmath>

static uint32_t le32(const uint8_t *p) { return p[0] | (uint32_t(p[1])<<8) | (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24); }
static std::vector<int16_t> load_wav(const char *path) {
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    assert(bytes.size() >= 44 && !memcmp(bytes.data(), "RIFF", 4) && !memcmp(bytes.data()+8, "WAVE", 4));
    bool format_ok = false;
    for (size_t p=12; p+8<=bytes.size();) {
        uint32_t size=le32(&bytes[p+4]);
        assert(p+8+size<=bytes.size());
        if (!memcmp(&bytes[p], "fmt ", 4)) {
            assert(size>=16);
            auto b=&bytes[p+8];
            format_ok=b[0]==1&&b[1]==0&&b[2]==1&&b[3]==0&&le32(b+4)==16000&&b[14]==16;
        }
        if (!memcmp(&bytes[p], "data", 4)) {
            assert(format_ok && !(size%2));
            std::vector<int16_t> pcm(8000 + size/2 + 24000, 0);
            memcpy(pcm.data()+8000, &bytes[p+8], size);
            return pcm;
        }
        p+=8+size+(size&1);
    }
    assert(false); return {};
}
static bool run(const std::vector<int16_t> &pcm, size_t chunk) {
    bool triggered=false;
    for (size_t p=0;p<pcm.size();p+=chunk) {
        triggered=lumi_wake_process(pcm.data()+p,std::min(chunk,pcm.size()-p))||triggered;
        assert(lumi_wake_healthy());
    }
    return triggered;
}
int main(int argc,char **argv) {
    assert(argc>1);
    for(int cycle=0;cycle<2;++cycle) {
        assert(lumi_wake_init());
        printf("cycle=%d arena_used=%zu\n",cycle,lumi_wake_arena_used());
        std::vector<int16_t> silence(16000*5);
        assert(!run(silence,320));
        for(int i=1;i<argc;++i) {
            auto wav=load_wav(argv[i]);
            assert(lumi_wake_reset());
            bool triggered=run(wav,320);
            printf("%s triggered=%d score=%.6f\n",argv[i],triggered,lumi_wake_score());
            assert(triggered);
            assert(lumi_wake_reset());
            assert(run(wav,73));
            for(int variant=0;variant<3;variant++) {
                auto noisy=wav;
                for(size_t n=0;n<noisy.size();n++) {
                    double x=wav[n]*(variant==0?0.5:1.0);
                    if(variant==1)x+=512; // real microphones can have DC offset
                    if(variant==2)x+=512*std::sin(2*3.141592653589793*50*n/16000);
                    noisy[n]=(int16_t)std::max(-32768.0,std::min(32767.0,x));
                }
                assert(lumi_wake_reset());
                bool heard=run(noisy,320);
                printf("variant=%d triggered=%d\n",variant,heard);fflush(stdout);
                assert(heard);
            }
        }
        assert(lumi_wake_reset());
        assert(!run(silence,73));
        lumi_wake_deinit();
        assert(!lumi_wake_healthy());
    }
    puts("PASS: TFLite Micro allocation, positive WAVs, silence, reset, chunking, reinitialization");
}
