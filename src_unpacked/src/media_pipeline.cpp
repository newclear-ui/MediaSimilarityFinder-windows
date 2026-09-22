#include "media_pipeline.h"
#include "gpu_backend.h"
#include <algorithm>
#include "crop_fingerprint.h"
namespace msf {
bool MediaPipeline::image(const std::string& path,std::uint64_t& fingerprint, std::uint64_t* mirrorFingerprint) const { ImageDecoder d; GrayImage img; if(!d.decode(path,32,32,img)) return false; fingerprint=perceptual_hash(img.pixels,img.width,img.height); if(mirrorFingerprint) *mirrorFingerprint=perceptual_hash_mirrored(img.pixels,img.width,img.height); return true; }
std::vector<ImageFingerprintResult> MediaPipeline::imageBatch(const std::vector<std::string>& paths,bool preferGpu,std::size_t gpuBatchSize) const {
    std::vector<ImageFingerprintResult> out(paths.size());
    std::vector<std::uint8_t> packed; std::vector<std::size_t> map;
    for(std::size_t i=0;i<paths.size();++i){
        out[i].path=paths[i];
        ImageDecoder d; GrayImage img;
        if(!d.decode(paths[i],32,32,img)||img.pixels.size()!=1024) continue;
        map.push_back(i); packed.insert(packed.end(),img.pixels.begin(),img.pixels.end());
    }
    if(map.empty()) return out;
    gpuBatchSize=std::max<std::size_t>(1,gpuBatchSize);
    if(preferGpu){
        const auto safe=gpu_.recommendedBatchSize(gpuBatchSize);
        if(safe>0) gpuBatchSize=safe;
    }
    for(std::size_t base=0;base<map.size();base+=gpuBatchSize){
        const std::size_t n=std::min(gpuBatchSize,map.size()-base);
        std::vector<std::uint64_t> hashes(n);
        const std::uint8_t* block=packed.data()+base*1024;
        bool used=false;
        if(preferGpu) used=gpu_.hashBatch(block,n,hashes.data());
        if(!used){
            for(std::size_t k=0;k<n;++k) hashes[k]=perceptual_hash(std::vector<std::uint8_t>(block+k*1024,block+(k+1)*1024),32,32);
        }
        for(std::size_t k=0;k<n;++k){ auto oi=map[base+k]; out[oi].fingerprint=hashes[k];
            ImageDecoder cd; GrayImage original; if(cd.decodePreserveAspect(paths[oi],128,original)) out[oi].crops=cropFingerprints(original);
            std::vector<std::uint8_t> px(block+k*1024,block+(k+1)*1024);
            out[oi].mirrorFingerprint=perceptual_hash_mirrored(px,32,32);
            out[oi].ok=true; out[oi].usedGpu=used; out[oi].gpuFallback=preferGpu&&!used; }
    }
    return out;
}
}
