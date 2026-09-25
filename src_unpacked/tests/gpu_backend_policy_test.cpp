#include "gpu_backend.h"
#include <iostream>
#include <string>
int main(){
    msf::GpuBackend g; const auto info=g.detect();
    const auto b=g.recommendedBatchSize(256);
    // Node A: GPU is the generic term; the resolved backend name reports what
    // actually executes (CUDA device or CPU fallback) — never an
    // unimplemented backend.
    if(g.kind()!=msf::GpuBackendKind::Auto){std::cerr<<"default kind must be Auto\n";return 3;}
    const std::string name=g.backendName();
    if(name!="CUDA"&&name!="CPU"){std::cerr<<"unexpected backend name\n";return 4;}
    if(info.available!=(name=="CUDA")){std::cerr<<"backend name must match detection\n";return 5;}
    msf::GpuBackend cpu;
    cpu.setKind(msf::GpuBackendKind::Cpu);
    if(cpu.available()){std::cerr<<"pinned CPU backend must not report available\n";return 6;}
    if(cpu.backendName()!="CPU"){std::cerr<<"pinned CPU backend must be named CPU\n";return 7;}
    if(std::string(msf::gpuBackendKindName(msf::GpuBackendKind::Cuda))!="CUDA")return 8;
    if(!info.available){ if(b!=0){std::cerr<<"no-device batch must be zero\n";return 1;} std::cout<<"gpu_policy=skip_no_device\n"; return 0; }
    if(info.globalMemoryBytes==0||info.freeMemoryBytes==0||b==0||b>256){std::cerr<<"invalid GPU policy values\n";return 2;}
    std::cout<<"gpu_policy=ok batch="<<b<<" free="<<info.freeMemoryBytes<<"\n"; return 0;
}
