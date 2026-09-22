#include "gpu_backend.h"
#include <algorithm>
#include <utility>
#ifdef MSF_HAS_CUDA
#include <cuda_runtime_api.h>
extern "C" void* msf_cuda_backend_create();
extern "C" void msf_cuda_backend_destroy(void*);
extern "C" bool msf_cuda_backend_hash_batch(void*,const std::uint8_t*,std::uint64_t,std::uint64_t*);
#endif

namespace msf {
struct GpuBackend::Impl {
#ifdef MSF_HAS_CUDA
    void* cuda=nullptr;
#endif
};

GpuBackend::GpuBackend() : impl_(new Impl{}) {
#ifdef MSF_HAS_CUDA
    impl_->cuda=msf_cuda_backend_create();
#endif
}
GpuBackend::~GpuBackend() {
#ifdef MSF_HAS_CUDA
    if(impl_) msf_cuda_backend_destroy(impl_->cuda);
#endif
    delete impl_;
}
GpuBackend::GpuBackend(GpuBackend&& o) noexcept : impl_(std::exchange(o.impl_,nullptr)) {}
GpuBackend& GpuBackend::operator=(GpuBackend&& o) noexcept {
    if(this==&o) return *this;
#ifdef MSF_HAS_CUDA
    if(impl_) msf_cuda_backend_destroy(impl_->cuda);
#endif
    delete impl_; impl_=std::exchange(o.impl_,nullptr); return *this;
}

GpuInfo GpuBackend::detect() const {
#ifdef MSF_HAS_CUDA
    int n=0;
    if(cudaGetDeviceCount(&n)!=cudaSuccess || n<=0) return {};
    cudaDeviceProp prop{};
    if(cudaGetDeviceProperties(&prop,0)!=cudaSuccess) return {};
    std::size_t freeBytes=0,totalBytes=0;
    if(cudaMemGetInfo(&freeBytes,&totalBytes)!=cudaSuccess) totalBytes=static_cast<std::size_t>(prop.totalGlobalMem);
    return {true,prop.name,prop.major,prop.minor,static_cast<std::size_t>(prop.totalGlobalMem),freeBytes};
#else
    return {};
#endif
}
bool GpuBackend::available() const {
#ifdef MSF_HAS_CUDA
    return impl_ && impl_->cuda && detect().available;
#else
    return false;
#endif
}
std::size_t GpuBackend::recommendedBatchSize(std::size_t requested) const {
    requested=std::max<std::size_t>(1,requested);
    const auto info=detect();
    if(!info.available || info.freeMemoryBytes==0) return 0;
    constexpr std::size_t bytesPerImage=1032;
    const std::size_t budget=info.freeMemoryBytes/2;
    const std::size_t byMemory=std::max<std::size_t>(1,budget/bytesPerImage);
    return std::min(requested,byMemory);
}
bool GpuBackend::hashBatch(const std::uint8_t* g,std::uint64_t n,std::uint64_t* out) const {
    if(!g||!out||n==0||!available()) return false;
#ifdef MSF_HAS_CUDA
    return msf_cuda_backend_hash_batch(impl_->cuda,g,n,out);
#else
    return false;
#endif
}
}
