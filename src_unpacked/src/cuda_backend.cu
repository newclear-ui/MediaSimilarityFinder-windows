#include <cuda_runtime.h>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <vector>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// The CPU reference evaluates the 32x32 DCT directly. CUDA uses the same
// normalized DCT, but precomputes cosine/normalization terms once so that the
// hot kernel performs arithmetic rather than thousands of trig evaluations.
__constant__ double c_cos[8][32];
__constant__ double c_norm[8];

__global__ void msf_init_dummy() {}

__global__ void msf_ssim_kernel(const std::uint8_t* a,const std::uint8_t* b,
                                std::uint64_t count,double* out){
    const std::uint64_t pair=blockIdx.x;
    const int window=blockIdx.y;
    if(pair>=count||window>=36) return;
    const int tid=threadIdx.x;
    const int wx=(window%6)*8, wy=(window/6)*8;
    __shared__ double sums[5][64];
    const std::size_t base=static_cast<std::size_t>(pair)*2304u;
    const int y=wy+tid/8, x=wx+tid%8;
    const double av=static_cast<double>(a[base+y*48+x]);
    const double bv=static_cast<double>(b[base+y*48+x]);
    sums[0][tid]=av; sums[1][tid]=bv; sums[2][tid]=av*av;
    sums[3][tid]=bv*bv; sums[4][tid]=av*bv;
    __syncthreads();
    for(int stride=32;stride>0;stride>>=1){
        if(tid<stride) for(int k=0;k<5;++k) sums[k][tid]+=sums[k][tid+stride];
        __syncthreads();
    }
    if(tid==0){
        constexpr double C1=6.5025,C2=58.5225;
        const double mx=sums[0][0]/64.0,my=sums[1][0]/64.0;
        const double vx=sums[2][0]/64.0-mx*mx,vy=sums[3][0]/64.0-my*my;
        const double cv=sums[4][0]/64.0-mx*my;
        const double num=(2*mx*my+C1)*(2*cv+C2);
        const double den=(mx*mx+my*my+C1)*(vx+vy+C2);
        out[pair*36u+static_cast<std::size_t>(window)]=den>0?num/den:1.0;
    }
}

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
    std::uint8_t* ssimA=nullptr;
    std::uint8_t* ssimB=nullptr;
    double* ssimOut=nullptr;
    std::size_t ssimCapacity=0;
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
    if(s->ssimA) cudaFree(s->ssimA); if(s->ssimB) cudaFree(s->ssimB); if(s->ssimOut) cudaFree(s->ssimOut);
    if(s->stream) cudaStreamDestroy(s->stream); delete s;
}

extern "C" bool msf_cuda_backend_ssim_batch(void* p,const std::uint8_t* a,const std::uint8_t* b,std::uint64_t count,double* out){
    auto* s=static_cast<CudaBackendState*>(p);
    if(!s||!a||!b||!out||!count||count>static_cast<std::uint64_t>(SIZE_MAX/2304)) return false;
    const std::size_t bytes=static_cast<std::size_t>(count)*2304u;
    const std::size_t outBytes=static_cast<std::size_t>(count)*36u*sizeof(double);
    if(bytes>s->ssimCapacity){
        if(cudaStreamSynchronize(s->stream)!=cudaSuccess) return false;
        if(s->ssimA) cudaFree(s->ssimA); if(s->ssimB) cudaFree(s->ssimB); if(s->ssimOut) cudaFree(s->ssimOut);
        s->ssimA=nullptr;s->ssimB=nullptr;s->ssimOut=nullptr;s->ssimCapacity=0;
        if(cudaMalloc(reinterpret_cast<void**>(&s->ssimA),bytes)!=cudaSuccess) return false;
        if(cudaMalloc(reinterpret_cast<void**>(&s->ssimB),bytes)!=cudaSuccess) return false;
        if(cudaMalloc(reinterpret_cast<void**>(&s->ssimOut),outBytes)!=cudaSuccess) return false;
        s->ssimCapacity=bytes;
    }
    if(cudaMemcpyAsync(s->ssimA,a,bytes,cudaMemcpyHostToDevice,s->stream)!=cudaSuccess) return false;
    if(cudaMemcpyAsync(s->ssimB,b,bytes,cudaMemcpyHostToDevice,s->stream)!=cudaSuccess) return false;
    msf_ssim_kernel<<<dim3(static_cast<unsigned>(count),36,1),64,0,s->stream>>>(s->ssimA,s->ssimB,count,s->ssimOut);
    if(cudaGetLastError()!=cudaSuccess) return false;
    std::vector<double> windows(static_cast<std::size_t>(count)*36u);
    if(cudaMemcpyAsync(windows.data(),s->ssimOut,outBytes,cudaMemcpyDeviceToHost,s->stream)!=cudaSuccess) return false;
    if(cudaStreamSynchronize(s->stream)!=cudaSuccess) return false;
    for(std::size_t i=0;i<static_cast<std::size_t>(count);++i){ double sum=0; for(int w=0;w<36;++w) sum+=windows[i*36u+w]; out[i]=std::clamp(sum/36.0,0.0,1.0); }
    return true;
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
