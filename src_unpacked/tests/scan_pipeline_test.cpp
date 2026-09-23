#include "scan_pipeline.h"
#include <iostream>
int main(){
 msf::ScanPipeline p;
 p.add({"a.jpg",msf::MediaKind::Image,10,1,0xFFFF});
 p.add({"b.jpg",msf::MediaKind::Image,10,1,0xFFFF});
 p.add({"c.mp4",msf::MediaKind::Video,20,2,0xFF00});
 auto s=p.analyze(8);
 if(s.files!=3||s.indexed!=3||s.candidates!=1||s.groups!=1)return 1;
 // Cooperative cancellation: identical fingerprints yield ~20k pairs, far past
 // the poll interval, so an always-true stop check must abort promptly instead
 // of grinding through every pair (the phase where stop/pause used to do nothing).
 msf::ScanPipeline big;
 for(int i=0;i<200;++i) big.add({"img"+std::to_string(i)+".jpg",msf::MediaKind::Image,10,1,0xFFFF});
 auto full=big.analyze(8);
 if(full.candidates<1024) return 20;
 auto s3=big.analyze(8,{},{});
 if(s3.candidates!=full.candidates) return 21;
 // An always-firing stop check aborts the pair loops and returns partial
 // stats instead of grinding through every pair.
 auto cut=big.analyze(8,{},[](){return true;});
 if(!(cut.candidates<full.candidates)) return 22;
 msf::ScanPipeline mixed; mixed.add({"i1",msf::MediaKind::Image,1,1,0x1234}); mixed.add({"i2",msf::MediaKind::Image,1,1,0x1235}); mixed.add({"v1",msf::MediaKind::Video,1,1,0x1234}); mixed.add({"v2",msf::MediaKind::Video,1,1,0x1235}); auto ms=mixed.analyze(1); if(ms.possiblePairs!=2) return 8; std::cout<<"mixed_kind=ok\n"; std::cout<<"scan_pipeline=ok\n";return 0;
}