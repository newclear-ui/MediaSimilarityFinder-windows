#include <cuda_runtime.h>
#include <cstdint>
#include <chrono>
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
    // D4a: internal timing. One event per boundary, recorded on the same
    // stream so elapsed times reflect real execution order. Created once and
    // reused; per-call creation would add overhead to the measured value.
    // timingReady == false is a supported state: hashing still works and the
    // caller simply sees measured=false (never a zero standing in for time).
    cudaEvent_t evH2dStart=nullptr, evH2dEnd=nullptr;
    cudaEvent_t evKernelStart=nullptr, evKernelEnd=nullptr;
    cudaEvent_t evD2hStart=nullptr, evD2hEnd=nullptr;
    bool timingReady=false;
    struct HashTiming {
        bool measured=false;
        double h2dDeviceMs=0, kernelDeviceMs=0, d2hDeviceMs=0;
        double syncHostMs=0, hostTotalMs=0;
    } lastTiming{};
};

static double hostNowMs(){
    using namespace std::chrono;
    return duration<double,std::milli>(steady_clock::now().time_since_epoch()).count();
}

static bool ensure_timing_events(CudaBackendState* s){
    if(s->timingReady) return true;
    cudaEvent_t* slots[6]={&s->evH2dStart,&s->evH2dEnd,&s->evKernelStart,
                           &s->evKernelEnd,&s->evD2hStart,&s->evD2hEnd};
    for(auto* slot:slots){
        if(*slot) continue;
        if(cudaEventCreateWithFlags(slot,cudaEventDefault)!=cudaSuccess){ *slot=nullptr; return false; }
    }
    s->timingReady=true; return true;
}
// D4a: a plain C POD so the .cu never leaks CUDA types upward.
struct CudaBackendHashTiming {
    int measured;
    double h2dDeviceMs, kernelDeviceMs, d2hDeviceMs;
    double syncHostMs, hostTotalMs;
};

// Events are recorded inline at each boundary, never batched up front, so the
// deltas reflect the real stream order. A record failure is latched into the
// same flag and only disables timing; the hash path is untouched afterwards.
static bool rec(cudaEvent_t ev,cudaStream_t stream,bool active){
    if(!active) return true;
    return cudaEventRecord(ev,stream)==cudaSuccess;
}

// Device elapsed times come from the event pair deltas. syncHostMs is the
// host wall time between "everything enqueued" and "stream sync returned",
// i.e. how long the host actually waited. It is NOT hostTotal - device sum.
static void collect_timing(CudaBackendState* s,double hostStart,double hostEnqueued,double hostEnd){
    float h2d=0.0f, kern=0.0f, d2h=0.0f;
    if(cudaEventElapsedTime(&h2d,s->evH2dStart,s->evH2dEnd)!=cudaSuccess) return;
    if(cudaEventElapsedTime(&kern,s->evKernelStart,s->evKernelEnd)!=cudaSuccess) return;
    if(cudaEventElapsedTime(&d2h,s->evD2hStart,s->evD2hEnd)!=cudaSuccess) return;
    s->lastTiming.measured=true;
    s->lastTiming.h2dDeviceMs=h2d;
    s->lastTiming.kernelDeviceMs=kern;
    s->lastTiming.d2hDeviceMs=d2h;
    s->lastTiming.syncHostMs=hostEnd-hostEnqueued;
    s->lastTiming.hostTotalMs=hostEnd-hostStart;
}

static void destroy_timing_events(CudaBackendState* s){
    cudaEvent_t* slots[6]={&s->evH2dStart,&s->evH2dEnd,&s->evKernelStart,
                           &s->evKernelEnd,&s->evD2hStart,&s->evD2hEnd};
    for(auto* slot:slots){ if(*slot){ cudaEventDestroy(*slot); *slot=nullptr; } }
    s->timingReady=false;
}

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
    ensure_timing_events(s);   // D4a: optional. Failure only disables timing.
    return s;
}
extern "C" void msf_cuda_backend_destroy(void* p){
    auto* s=static_cast<CudaBackendState*>(p); if(!s) return;
    if(s->stream) cudaStreamSynchronize(s->stream);
    if(s->di) cudaFree(s->di); if(s->do_) cudaFree(s->do_);
    if(s->ssimA) cudaFree(s->ssimA); if(s->ssimB) cudaFree(s->ssimB); if(s->ssimOut) cudaFree(s->ssimOut);
    destroy_timing_events(s);
    if(s->stream) cudaStreamDestroy(s->stream); delete s;
}

extern "C" bool msf_cuda_backend_ssim_batch(void* p,const std::uint8_t* a,const std::uint8_t* b,std::uint64_t count,double* out){
    auto* s=static_cast<CudaBackendState*>(p);
    if(!s||!a||!b||!out||!count||count>2147483647ULL||count>static_cast<std::uint64_t>(SIZE_MAX/2304)) return false;
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
    // D4a: timing is best-effort and never changes the hash result or this
    // function's success value. `tm` only latches off if a record fails.
    bool tm=s->timingReady;
    s->lastTiming=CudaBackendState::HashTiming{};
    const double hostStart=hostNowMs();
    tm=rec(s->evH2dStart,s->stream,tm);
    if(cudaMemcpyAsync(s->di,in,inputBytes,cudaMemcpyHostToDevice,s->stream)!=cudaSuccess) return false;
    tm=rec(s->evH2dEnd,s->stream,tm);
    tm=rec(s->evKernelStart,s->stream,tm);
    msf_phash_kernel<<<static_cast<unsigned>(count),256,0,s->stream>>>(s->di,count,s->do_);
    if(cudaGetLastError()!=cudaSuccess) return false;
    tm=rec(s->evKernelEnd,s->stream,tm);
    tm=rec(s->evD2hStart,s->stream,tm);
    if(cudaMemcpyAsync(out,s->do_,outputBytes,cudaMemcpyDeviceToHost,s->stream)!=cudaSuccess) return false;
    tm=rec(s->evD2hEnd,s->stream,tm);
    const double hostEnqueued=hostNowMs();
    const bool ok=cudaStreamSynchronize(s->stream)==cudaSuccess;
    const double hostEnd=hostNowMs();
    if(tm&&ok) collect_timing(s,hostStart,hostEnqueued,hostEnd);
    return ok;
}
extern "C" void msf_cuda_backend_hash_timing(void* p,CudaBackendHashTiming* out){
    if(!out) return;
    *out=CudaBackendHashTiming{};
    auto* s=static_cast<CudaBackendState*>(p);
    if(!s||!s->lastTiming.measured) return;
    out->measured=1;
    out->h2dDeviceMs=s->lastTiming.h2dDeviceMs;
    out->kernelDeviceMs=s->lastTiming.kernelDeviceMs;
    out->d2hDeviceMs=s->lastTiming.d2hDeviceMs;
    out->syncHostMs=s->lastTiming.syncHostMs;
    out->hostTotalMs=s->lastTiming.hostTotalMs;
}
