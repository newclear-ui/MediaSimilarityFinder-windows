#include "video_fingerprint.h"
#include <cmath>
#include <iostream>
int main(){
 msf::VideoFingerprint longv, shortv; longv.duration=20; shortv.duration=8;
 for(int i=0;i<20;++i){longv.timestamps.push_back(i); longv.hashes.push_back(0x1000ULL + static_cast<unsigned long long>(i));}
 for(int i=0;i<8;++i){shortv.timestamps.push_back(i); shortv.hashes.push_back(0x1005ULL + static_cast<unsigned long long>(i));}
 // The shorter clip is an exact contiguous segment of the longer sequence.
 for(int i=0;i<8;++i) shortv.hashes[i]=longv.hashes[i+5];
 const double s=msf::video_similarity(longv,shortv,{50.0,8.0});
 if(s<99.9){std::cerr<<"partial_similarity="<<s<<"\n";return 1;}
 std::cout<<"video_partial_alignment=ok\nsimilarity="<<s<<"\n";return 0;
}
