#include "scan_pipeline.h"
#include <iostream>
int main(){
 msf::ScanPipeline p;
 p.add({"a.jpg",msf::MediaKind::Image,10,1,0xFFFF});
 p.add({"b.jpg",msf::MediaKind::Image,10,1,0xFFFF});
 p.add({"c.mp4",msf::MediaKind::Video,20,2,0xFF00});
 auto s=p.analyze(8);
 if(s.files!=3||s.indexed!=3||s.candidates!=1||s.groups!=1)return 1;
 // Variant-dedup order: B's normal fp is 12 away from A (fails) but shares the
 // part-0 bucket; B's mirror is 5 away (passes) in the same bucket. Marking the
 // group before the distance check used to drop the pair entirely.
 // (Fingerprints are nonzero: 0 means "absent" and skips indexing.)
 const std::uint64_t vx = 0x0101010101010101ULL;
 std::uint64_t vy = vx, vz = vx;
 for (int b = 32; b < 44; ++b) vy ^= (1ULL << b); // 12 flips, part0 untouched
 for (int b = 8; b < 13; ++b) vz ^= (1ULL << b);  // 5 flips, part0 untouched
 msf::ScanPipeline vd;
 vd.add({"va.jpg",msf::MediaKind::Image,10,1,vx});
 msf::MediaFile vb{"vb.jpg",msf::MediaKind::Image,10,1,vy,vz};
 vd.add(vb);
 if(vd.analyze(8).groups!=1) return 30;
 // Temporal anchors: XOR fingerprints share no bucket, but a frame-anchor
 // pair collides, so the pair is enumerated (candidates>0) without a false
 // verdict (groups==0: best() rejects, temporal has no real files here).
 // Control without anchors stays fully invisible.
 {
  const std::uint64_t H = 0x0101010101010101ULL;
  const std::uint64_t H2 = H ^ 0x1FULL;
  std::uint64_t far2 = ~0ULL;
  const unsigned shifts[9] = {0,8,15,22,29,36,43,50,57};
  for (int k = 0; k < 9; ++k) far2 ^= (0x7ULL << shifts[k]);
  msf::ScanPipeline an;
  msf::MediaFile aa{"aa.mp4",msf::MediaKind::Video,10,1,~0ULL};
  aa.anchors.push_back(H);
  msf::MediaFile ab{"ab.mp4",msf::MediaKind::Video,10,1,far2};
  ab.anchors.push_back(H2);
  an.add(aa); an.add(ab);
  auto sa = an.analyze(8);
  if (sa.candidates < 1 || sa.groups != 0) return 31;
  msf::ScanPipeline an0;
  an0.add({"aa.mp4",msf::MediaKind::Video,10,1,~0ULL});
  an0.add({"ab.mp4",msf::MediaKind::Video,10,1,far2});
  auto s0 = an0.analyze(8);
  if (s0.candidates != 0 || s0.groups != 0) return 32;
 }
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