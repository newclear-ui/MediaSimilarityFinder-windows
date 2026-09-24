#include <cuda_runtime.h>
#include <cstdint>
#include <cmath>
#include <algorithm>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// The CPU reference evaluates the 32x32 DCT directly. CUDA uses the same
// normalized DCT, but precomputes cosine/normalization terms once so that the
// hot kernel performs arithmetic rather than thousands of trig evaluations.
__constant__ double c_cos[8][32];
__constant__ double c_norm[8];

__global__ void msf_init_dummy() {}

__global__ void msf_phash_kernel(const std::uint8_t* in,std::uint64_t count,std::uint64_t* out){
    const std::uint64_t i=blockIdx.x;
    if(i>=count) return;
    const int tid=threadIdx.x;
    const std::uint8_t* p=in+i*1024;
    __shared__ std::uint8_t pixels[1024];
    __shared__ double tmp[8*32];
    __shared__ double coeff[64];

    for(int k=tid;k<1024;k+=blockDim.x) pixels[k]=p[k];
    __syncthreads();

    // 256 threads calculate the horizontal 1-D DCT terms. This turns the
    // original 64*1024 direct products into 256*32 + 64*32 products while
    // keeping the exact double-precision reference math.
    if(tid<256){
        const int u=tid/32, y=tid%32;
        double sum=0.0;
        for(int x=0;x<32;++x) sum += static_cast<double>(pixels[y*32+x])*c_cos[u][x];
        tmp[u*32+y]=sum*c_norm[u];
    }
    __syncthreads();

    if(tid<64){
        const int u=tid/8, v=tid%8;
        double sum=0.0;
        for(int y=0;y<32;++y) sum += tmp[u*32+y]*c_cos[v][y];
        const double c=sum*c_norm[v];
        coeff[tid]=(c>-1e-7&&c<1e-7)?0.0:c;
    }
    __syncthreads();

    if(tid==0){
        double vals[63]; int n=0;
        for(int k=0;k<64;++k) if(k!=0) vals[n++]=coeff[k];
        for(int a=0;a<n;++a) for(int b=a+1;b<n;++b) if(vals[b]<vals[a]){
            const double t=vals[a]; vals[a]=vals[b]; vals[b]=t;
        }
        const double med=vals[31];
        std::uint64_t h=0;
        for(int k=1;k<64;++k) if(coeff[k]>=med) h|=1ULL<<(k-1);
        out[i]=h;
    }
}

static void host_tables(double cosTable[8][32], double norm[8]){
    for(int u=0;u<8;++u){
        norm[u]=(u==0)?std::sqrt(1.0/32.0):std::sqrt(2.0/32.0);
        for(int x=0;x<32;++x)
            cosTable[u][x]=std::cos((2*x+1)*u*M_PI/64.0);
    }
}

struct CudaBackendState {
    std::uint8_t* di=nullptr;
    std::uint64_t* do_=nullptr;
    std::size_t inputCapacity=0;
    std::size_t outputCapacity=0;
    cudaStream_t stream=nullptr;
    bool tablesReady=false;
};

static bool ensure_tables(CudaBackendState* s){
    if(s->tablesReady) return true;
    double hCos[8][32]; double hNorm[8]; host_tables(hCos,hNorm);
    if(cudaMemcpyToSymbol(c_cos,hCos,sizeof(hCos))!=cudaSuccess) return false;
    if(cudaMemcpyToSymbol(c_norm,hNorm,sizeof(hNorm))!=cudaSuccess) return false;
    s->tablesReady=true; return true;
}
static bool ensure_capacity(CudaBackendState* s,std::size_t inputBytes,std::size_t outputBytes){
    if(inputBytes<=s->inputCapacity && outputBytes<=s->outputCapacity) return true;
    if(cudaStreamSynchronize(s->stream)!=cudaSuccess) return false;
    if(s->di) { cudaFree(s->di); s->di=nullptr; }
    if(s->do_) { cudaFree(s->do_); s->do_=nullptr; }
    s->inputCapacity=inputBytes; s->outputCapacity=outputBytes;
    if(cudaMalloc(reinterpret_cast<void**>(&s->di),inputBytes)!=cudaSuccess){ s->inputCapacity=0; return false; }
    if(cudaMalloc(reinterpret_cast<void**>(&s->do_),outputBytes)!=cudaSuccess){ cudaFree(s->di); s->di=nullptr; s->inputCapacity=0; s->outputCapacity=0; return false; }
    return true;
}

extern "C" void* msf_cuda_backend_create(){
    auto* s=new CudaBackendState{};
    if(cudaStreamCreateWithFlags(&s->stream,cudaStreamNonBlocking)!=cudaSuccess){ delete s; return nullptr; }
    if(!ensure_tables(s)){ cudaStreamDestroy(s->stream); delete s; return nullptr; }
    return s;
}
extern "C" void msf_cuda_backend_destroy(void* p){
    auto* s=static_cast<CudaBackendState*>(p); if(!s) return;
    if(s->stream) cudaStreamSynchronize(s->stream);
    if(s->di) cudaFree(s->di); if(s->do_) cudaFree(s->do_);
    if(s->stream) cudaStreamDestroy(s->stream); delete s;
}
extern "C" bool msf_cuda_backend_hash_batch(void* p,const std::uint8_t* in,std::uint64_t count,std::uint64_t* out){
    auto* s=static_cast<CudaBackendState*>(p);
    if(!s||!in||!out||!count||count>2147483647ULL||count>static_cast<std::uint64_t>(SIZE_MAX/1024)) return false;
    if(!ensure_capacity(s,static_cast<std::size_t>(count)*1024,static_cast<std::size_t>(count)*sizeof(std::uint64_t))) return false;
    const std::size_t inputBytes=static_cast<std::size_t>(count)*1024;
    const std::size_t outputBytes=static_cast<std::size_t>(count)*sizeof(std::uint64_t);
    if(cudaMemcpyAsync(s->di,in,inputBytes,cudaMemcpyHostToDevice,s->stream)!=cudaSuccess) return false;
    msf_phash_kernel<<<static_cast<unsigned>(count),256,0,s->stream>>>(s->di,count,s->do_);
    if(cudaGetLastError()!=cudaSuccess) return false;
    if(cudaMemcpyAsync(out,s->do_,outputBytes,cudaMemcpyDeviceToHost,s->stream)!=cudaSuccess) return false;
    return cudaStreamSynchronize(s->stream)==cudaSuccess;
}
