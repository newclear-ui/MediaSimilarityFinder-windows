#include "video_fingerprint.h"
#include "fingerprint.h"
#include <iostream>
#include <vector>
static std::uint64_t h(int v){ std::vector<std::uint8_t> p(32*32,(std::uint8_t)v); for(int y=6;y<26;++y) for(int x=5;x<18;++x) p[y*32+x]=(std::uint8_t)(v+90); return msf::perceptual_hash(p,32,32); }
int main(){
 msf::VideoFingerprint a,b; msf::VideoCropFingerprint ca,cb;
 const std::uint64_t fullA=0xAAAAAAAAAAAAAAAAULL, fullB=0x5555555555555555ULL;
 const std::uint64_t crop4=0x1234567890ABCDEFULL;
 for(int i=0;i<8;++i){
   a.timestamps.push_back(i);b.timestamps.push_back(i);a.hashes.push_back(fullA);b.hashes.push_back(fullB);
   ca.timestamps.push_back(i);cb.timestamps.push_back(i);
   // Only 4:3 is a true crop match. Other ratios are deliberately unrelated.
   ca.a4x3.push_back(crop4); cb.a4x3.push_back(crop4);
   ca.a1x1.push_back(0x0101010101010101ULL); cb.a1x1.push_back(0x1010101010101010ULL);
   ca.a9x16.push_back(0x0F0F0F0F0F0F0F0FULL); cb.a9x16.push_back(0xF0F0F0F0F0F0F0F0ULL);
   ca.mirrorA4x3.push_back(0x2222222222222222ULL); cb.mirrorA4x3.push_back(0x3333333333333333ULL);
   ca.mirrorA1x1.push_back(0x4444444444444444ULL); cb.mirrorA1x1.push_back(0x8888888888888888ULL);
   ca.mirrorA9x16.push_back(0x6666666666666666ULL); cb.mirrorA9x16.push_back(0x9999999999999999ULL);
 }
 double s=msf::video_crop_similarity(a,ca,b,cb,{90,8,2}); if(s<99.0){std::cerr<<s;return 1;}
 // A 4:3 crop and a 1:1 crop with the same hash must not match each other;
 // because the full hashes are intentionally dissimilar, a score near 100
 // would indicate an illegal cross-ratio crop-to-crop comparison.
 msf::VideoCropFingerprint cross=cb;
 for(auto &v:cross.a4x3) v=0x1010101010101010ULL;
 for(auto &v:cross.a1x1) v=crop4;
 double t=msf::video_crop_similarity(a,ca,b,cross,{99.0,8,2});
 if(t>99.0){std::cerr<<"cross_ratio_false_positive="<<t;return 2;}
 std::cout<<"video_crop_similarity=ok\nscore="<<s<<"\n"; return 0;
}