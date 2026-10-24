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

static void report(const std::vector<int16_t>& pcm,const char *label){
 float peak=0;int hits=0;
 for(size_t p=0;p<pcm.size();p+=320){
  hits+=lumi_wake_process(pcm.data()+p,std::min(size_t(320),pcm.size()-p));
  peak=std::max(peak,lumi_wake_score());assert(lumi_wake_healthy());
 }
 printf("%s hits=%d peak=%.6f\n",label,hits,peak);fflush(stdout);
}
int main(int argc,char **argv){
 assert(argc>1);auto pcm=load_wav(argv[1]);
 for(int cycle=0;cycle<3;cycle++){
  assert(lumi_wake_init());printf("CYCLE %d\n",cycle);
  report(pcm,"fresh");
  assert(lumi_wake_reset());report(pcm,"reset");
  assert(lumi_wake_reset());
  std::vector<int16_t> quiet(16000*120);uint32_t rng=42;
  for(auto &x:quiet){rng=rng*1664525+1013904223;x=int(rng>>24)%65-32;}
  report(quiet,"120s background");report(pcm,"after background");
  lumi_wake_deinit();
 }
}
