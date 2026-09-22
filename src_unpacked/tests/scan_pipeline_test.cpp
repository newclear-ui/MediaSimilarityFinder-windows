#include "scan_pipeline.h"
#include <iostream>
int main(){
 msf::ScanPipeline p;
 p.add({"a.jpg",msf::MediaKind::Image,10,1,0xFFFF});
 p.add({"b.jpg",msf::MediaKind::Image,10,1,0xFFFF});
 p.add({"c.mp4",msf::MediaKind::Video,20,2,0xFF00});
 auto s=p.analyze(8);
 if(s.files!=3||s.indexed!=3||s.candidates!=1||s.groups!=1)return 1;
 msf::ScanPipeline mixed; mixed.add({"i1",msf::MediaKind::Image,1,1,0x1234}); mixed.add({"i2",msf::MediaKind::Image,1,1,0x1235}); mixed.add({"v1",msf::MediaKind::Video,1,1,0x1234}); mixed.add({"v2",msf::MediaKind::Video,1,1,0x1235}); auto ms=mixed.analyze(1); if(ms.possiblePairs!=2) return 8; std::cout<<"mixed_kind=ok\n"; std::cout<<"scan_pipeline=ok\n";return 0;
}