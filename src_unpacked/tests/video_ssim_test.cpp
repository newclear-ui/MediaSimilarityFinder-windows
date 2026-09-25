// L3 SSIM cell verification: frame_ssim() behavior plus the Hamming-gate
// fallback contract of video_similarity() (no thumbs == legacy scoring).
#include "video_fingerprint.h"
#include "gpu_backend.h"
#include <cmath>
#include <iostream>
#include <vector>
int main(){
 constexpr int kT=msf::VideoFingerprint::kThumbSize;
 // Identity: identical frames score ~1.
 std::vector<std::uint8_t> a((std::size_t)kT*kT), b((std::size_t)kT*kT);
 for(int y=0;y<kT;++y)for(int x=0;x<kT;++x){a[(std::size_t)y*kT+x]=(std::uint8_t)((x*7+y*13)&0xFF);b[(std::size_t)y*kT+x]=a[(std::size_t)y*kT+x];}
 if(msf::frame_ssim(a.data(),b.data(),kT,kT)<0.999) return 1;
 // Flat black vs flat white: near 0 (luminance term collapses).
 std::vector<std::uint8_t> blk((std::size_t)kT*kT,0), wht((std::size_t)kT*kT,255);
 if(msf::frame_ssim(blk.data(),wht.data(),kT,kT)>0.05) return 2;
 // Same low-frequency structure, different detail: mid range, not ~1.
 std::vector<std::uint8_t> c=b;
 for(int y=0;y<kT;++y)for(int x=0;x<kT;++x) if(((x/6)+(y/6))%2) c[(std::size_t)y*kT+x]^=0x90;
 const double s=msf::frame_ssim(a.data(),c.data(),kT,kT);
 if(!(s>0.05&&s<0.999)) return 3;
 // Bad geometry rejected.
 if(msf::frame_ssim(a.data(),b.data(),0,kT)!=0) return 4;
 if(msf::frame_ssim(nullptr,b.data(),kT,kT)!=0) return 5;
 // Fallback contract: hand-built fingerprints without thumbs score exactly
 // the legacy Hamming path (useSsim off), identical pair -> 100.
 msf::VideoFingerprint fa,fb;
 fa.timestamps={0,1}; fb.timestamps={0,1};
 fa.hashes={0x0101010101010101ULL,0x0202020202020202ULL};
 fb.hashes=fa.hashes;
 fa.mirrorHashes={0,0}; fb.mirrorHashes={0,0};
 msf::VideoSimilarityOptions o; o.thresholdPercent=50.0;
 if(msf::video_similarity(fa,fb,o)<99.9) return 6;
 // Same pair WITH identical thumbs stays high (blend of high H + SSIM~1).
 fa.thumb48=a; fa.thumb48.insert(fa.thumb48.end(),a.begin(),a.end());
 fb.thumb48=fa.thumb48;
 if(msf::video_similarity(fa,fb,o)<99.0) return 7;
 // Same pair where one side's thumbs are unrelated noise: blend must pull the
 // score down vs the no-thumb legacy score (false-positive cutting works).
 msf::VideoFingerprint fc=fa, fd=fb;
 for(std::size_t k=0;k<fd.thumb48.size();++k) fd.thumb48[k]=(std::uint8_t)((k*2654435761ULL>>16)&0xFF);
 msf::VideoFingerprint fe=fa, ff=fb; fe.thumb48.clear(); ff.thumb48.clear();
 const double noThumb=msf::video_similarity(fe,ff,o);
  const double noisy=msf::video_similarity(fc,fd,o);
  if(!(noisy<noThumb)) return 8;
  // GPU batch path: use enough passing cells to cross the batch threshold and
  // compare the CUDA result with the CPU reference.
  msf::GpuBackend gpu; const bool available=gpu.available();
  msf::VideoFingerprint fg,fh; fg.timestamps.resize(8); fh.timestamps.resize(8);
  for(int i=0;i<8;++i){ fg.hashes.push_back(0x1111111111111111ULL+static_cast<std::uint64_t>(i)); fh.hashes.push_back(fg.hashes.back()); fg.mirrorHashes.push_back(0); fh.mirrorHashes.push_back(0); fg.thumb48.insert(fg.thumb48.end(),a.begin(),a.end()); fh.thumb48.insert(fh.thumb48.end(),a.begin(),a.end()); }
  const double cpuScore=msf::video_similarity(fg,fh,o);
  msf::VideoSimilarityStats gst; auto go=o; go.gpu=available?&gpu:nullptr; go.stats=&gst;
  const double gpuScore=msf::video_similarity(fg,fh,go);
  if(std::abs(cpuScore-gpuScore)>0.2){ std::cerr<<"ssim_gpu_mismatch cpu="<<cpuScore<<" gpu="<<gpuScore<<" diff="<<std::abs(cpuScore-gpuScore)<<"\n"; return 9; }
  if(available&&(!gst.gpuUsed||gst.gpuPairs==0)) return 10;
  std::cout<<"video_ssim=ok id=1 flat<0.05 detail="<<s<<" legacy="<<noThumb<<" noisy="<<noisy<<" gpu="<<(available?"yes":"no")<<" gpuMs="<<gst.gpuMs<<"\n";return 0;
}
