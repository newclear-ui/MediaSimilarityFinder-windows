#include "gpu_backend.h"
#include "fingerprint.h"
#include <cstdint>
#include <iostream>
#include <vector>
int main(){
    msf::GpuBackend gpu; auto info=gpu.detect();
    if(!info.available){ std::cout<<"cuda_backend=skip_no_device\n"; return 0; }
    constexpr std::size_t count=8; std::vector<std::uint8_t> pixels(count*1024);
    for(std::size_t i=0;i<count;++i) for(std::size_t p=0;p<1024;++p) pixels[i*1024+p]=static_cast<std::uint8_t>((p*17+i*31+(p/32)*7)%256);
    std::vector<std::uint64_t> gpuHashes(count);
    if(!gpu.hashBatch(pixels.data(),count,gpuHashes.data())){ std::cerr<<"cuda hashBatch failed\n"; return 1; }
    for(std::size_t i=0;i<count;++i){
        std::vector<std::uint8_t> one(pixels.begin()+i*1024,pixels.begin()+(i+1)*1024);
        auto cpu=msf::perceptual_hash(one,32,32);
        if(cpu!=gpuHashes[i]){ std::cerr<<"hash mismatch at "<<i<<" cpu="<<cpu<<" gpu="<<gpuHashes[i]<<"\n"; return 2; }
    }
    std::cout<<"cuda_backend=ok name="<<info.name<<" cc="<<info.major<<"."<<info.minor<<"\n";
    return 0;
}
