#include "fingerprint.h"
#include "scan_pipeline.h"
#include "video_fingerprint.h"
#include <cassert>
#include <iostream>

static std::vector<std::uint8_t> makeImage(bool mirror=false){
    std::vector<std::uint8_t> p(32*32,10);
    for(int y=5;y<27;++y) for(int x=4;x<14;++x) p[y*32+(mirror?31-x:x)]=240;
    for(int y=9;y<20;++y) for(int x=18;x<25;++x) p[y*32+(mirror?31-x:x)]=180;
    return p;
}
int main(){
    auto a=makeImage(false), b=makeImage(true);
    auto ha=msf::perceptual_hash(a,32,32), hb=msf::perceptual_hash(b,32,32);
    auto hma=msf::perceptual_hash_mirrored(a,32,32), hmb=msf::perceptual_hash_mirrored(b,32,32);
    if(hma!=hb || hmb!=ha) return 1;
    msf::ScanPipeline p;
    p.add({"a.jpg",msf::MediaKind::Image,1,1,ha,hma});
    p.add({"b.jpg",msf::MediaKind::Image,1,1,hb,hmb});
    auto r=p.analyze(0);
    if(r.matches.size()!=1 || r.matches[0].percent<99.9) return 2;
    msf::VideoFingerprint va,vb; va.duration=4; vb.duration=4;
    for(int i=0;i<4;++i){va.timestamps.push_back(i);vb.timestamps.push_back(i);va.hashes.push_back(ha);vb.hashes.push_back(hb);va.mirrorHashes.push_back(hma);vb.mirrorHashes.push_back(hmb);}
    auto vs=msf::video_similarity(va,vb,{99.0,8.0,2.0}); std::cout<<"video="<<vs<<"\n"; if(vs<99.0) return 3;
    std::cout<<"mirror_aware=ok\n"; return 0;
}
