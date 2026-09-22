#include "resource_policy.h"
#include "gpu_backend.h"
#include <iostream>
int main(){
 auto a=msf::make_policy(msf::ResourceMode::Maximum);
 auto b=msf::make_policy(msf::ResourceMode::Light);
 auto c=msf::make_policy(msf::ResourceMode::Custom,73,81);
 if(a.cpuPercent!=90||a.gpuPercent!=90)return 1;
 if(b.cpuPercent>=a.cpuPercent||b.gpuPercent>=a.gpuPercent)return 2;
 if(c.cpuPercent!=73||c.gpuPercent!=81)return 3;
 std::cout<<"resource_policy=ok\n";
 std::cout<<"gpu_backend="<<(msf::GpuBackend().detect().available?"available":"fallback")<<"\n";
 return 0;
}
