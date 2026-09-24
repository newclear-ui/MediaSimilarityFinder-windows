#include "media_pipeline.h"
#include "gpu_backend.h"
#include <algorithm>
#include <future>
#include <thread>
#include "crop_fingerprint.h"
namespace msf {
// WIC/FFmpeg decodes are independent per file but individually serial work.
// Fan them out over a bounded worker pool; outputs stay index-ordered so
// results (and the GPU hash batch built from them) are deterministic.
static unsigned decodeWorkerCount(){
 unsigned hw=std::thread::hardware_concurrency();
 if(hw<4) hw=4; if(hw>32) hw=32; return hw;
}
template <typename F>
static void parallelFor(std::size_t n, F&& fn){
 if(!n) return;
 const unsigned jobs=std::min<unsigned>(decodeWorkerCount(), (unsigned)n);
 const std::size_t chunk=(n+jobs-1)/jobs;
 std::vector<std::future<void>> futs; futs.reserve(jobs);
 for(unsigned j=0;j<jobs;++j){
  const std::size_t b=j*chunk, e=std::min(n,b+chunk);
  if(b>=e) break;
  futs.emplace_back(std::async(std::launch::async,[b,e,&fn]{
   try { for(std::size_t i=b;i<e;++i) fn(i); } catch(...) {}
  }));
 }
 for(auto& f:futs) f.get();
}
bool MediaPipeline::image(const std::string& path,std::uint64_t& fingerprint, std::uint64_t* mirrorFingerprint) const { ImageDecoder d; GrayImage img; if(!d.decode(path,32,32,img)) return false; const auto h=perceptual_hash_pair(img.pixels,img.width,img.height); fingerprint=h.normal; if(mirrorFingerprint) *mirrorFingerprint=h.mirrored; return true; }
std::vector<ImageFingerprintResult> MediaPipeline::imageBatch(const std::vector<std::string>& paths,bool preferGpu,std::size_t gpuBatchSize,std::atomic<bool>* activity) const {
    struct ActivityGuard { std::atomic<bool>* p; ~ActivityGuard(){ if(p) p->store(false,std::memory_order_relaxed); } } guard{activity};
    if(activity) activity->store(false,std::memory_order_relaxed);
    std::vector<ImageFingerprintResult> out(paths.size());
    struct Decoded { bool ok=false; GrayImage img; };
    std::vector<Decoded> dec(paths.size());
    parallelFor(paths.size(), [&](std::size_t i){
        ImageDecoder d; GrayImage img;
        if(d.decode(paths[i],32,32,img)&&img.pixels.size()==1024){ dec[i].ok=true; dec[i].img=std::move(img); }
    });
    std::vector<std::uint8_t> packed; std::vector<std::size_t> map;
    for(std::size_t i=0;i<paths.size();++i){
        out[i].path=paths[i];
        if(!dec[i].ok) continue;
        map.push_back(i); packed.insert(packed.end(),dec[i].img.pixels.begin(),dec[i].img.pixels.end());
    }
    if(map.empty()) return out;
    gpuBatchSize=std::max<std::size_t>(1,gpuBatchSize);
    const bool gpuReady = preferGpu && gpu_.available();
    if(preferGpu){
        const auto safe=gpu_.recommendedBatchSize(gpuBatchSize);
        if(safe>0) gpuBatchSize=safe;
    }
    for(std::size_t base=0;base<map.size();base+=gpuBatchSize){
        const std::size_t n=std::min(gpuBatchSize,map.size()-base);
        std::vector<std::uint64_t> hashes(n), mirrors(n);
        const std::uint8_t* block=packed.data()+base*1024;
        bool used=false;
        if(gpuReady){
            if(activity) activity->store(true,std::memory_order_relaxed);
            used=gpu_.hashBatch(block,n,hashes.data());
            if(!used && activity) activity->store(false,std::memory_order_relaxed);
        }
        parallelFor(n,[&](std::size_t k){
            const auto h=perceptual_hash_pair_32(block+k*1024);
            if(!used) hashes[k]=h.normal;
            mirrors[k]=h.mirrored;
        });
        for(std::size_t k=0;k<n;++k){ auto oi=map[base+k]; out[oi].fingerprint=hashes[k];
            out[oi].mirrorFingerprint=mirrors[k];
            out[oi].ok=true; out[oi].usedGpu=used; out[oi].gpuFallback=preferGpu&&!used; }
    }
    if(activity) activity->store(false,std::memory_order_relaxed);
    // Crop pass decodes a second, larger frame per image; parallelize it the
    // same way. Each task writes only its own output slot.
    parallelFor(map.size(), [&](std::size_t m){
        auto oi=map[m]; ImageDecoder cd; GrayImage original;
        if(cd.decodePreserveAspect(paths[oi],128,original)) out[oi].crops=cropFingerprints(original);
    });
    return out;
}
}
