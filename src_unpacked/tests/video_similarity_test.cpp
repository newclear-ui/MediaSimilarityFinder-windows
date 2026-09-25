#include "video_fingerprint.h"
#include "video_sampling.h"
#include <cmath>
#include <iostream>
static msf::VideoFingerprint synth(double dur){
  msf::VideoFingerprint v; v.duration=dur;
  const auto plan=msf::make_sample_plan(dur);
  for(double t:plan.timestamps){ v.timestamps.push_back(t); v.hashes.push_back(0x12340000ULL ^ (std::uint64_t)(t*10.0)); v.mirrorHashes.push_back(0); }
  return v;
}
int main(){
  msf::VideoFingerprint longv, shortv; longv.duration=20; shortv.duration=8;
  for(int i=0;i<20;++i){longv.timestamps.push_back(i); longv.hashes.push_back(0x1000ULL + static_cast<unsigned long long>(i));}
  for(int i=0;i<8;++i){shortv.timestamps.push_back(i); shortv.hashes.push_back(0x1005ULL + static_cast<unsigned long long>(i));}
  for(int i=0;i<8;++i) shortv.hashes[i]=longv.hashes[i+5];
  const double s=msf::video_similarity(longv,shortv,{50.0,8.0});
  if(s<99.9){std::cerr<<"partial_similarity="<<s<<"\n";return 1;}
  msf::VideoFingerprint a=synth(59.98), b=synth(60.02);
  const double bs=msf::video_similarity(a,b,{50.0,8.0,2.0,0.0});
  if(bs<99.0){std::cerr<<"boundary_similarity="<<bs<<"\n";return 2;}
  msf::VideoFingerprint c, d; c.duration=60; d.duration=60;
  for(int i=0;i<12;++i){c.timestamps.push_back(i*5.0); c.hashes.push_back(0x2000ULL+i); c.mirrorHashes.push_back(0);}
  for(int i=0;i<15;++i){d.timestamps.push_back(i*4.0); d.hashes.push_back(0x2000ULL+(std::uint64_t)(i*4/5)); d.mirrorHashes.push_back(0);}
  const double ls=msf::video_similarity(c,d,{50.0,8.0,2.0,0.0});
  if(!(ls>=0.0&&ls<=100.0)){std::cerr<<"legacy_range="<<ls<<"\n";return 3;}
  std::cout<<"video_partial_alignment=ok\nsimilarity="<<s<<"\n";return 0;
}
