#include "resource_policy.h"
#include <iostream>
int main(){
  auto a=msf::make_policy(msf::ResourceMode::Maximum),b=msf::make_policy(msf::ResourceMode::Balanced),c=msf::make_policy(msf::ResourceMode::Gaming),d=msf::make_policy(msf::ResourceMode::Custom,73,81);
  auto h=msf::make_policy(msf::ResourceMode::High);
  if(a.cpuPercent!=90||a.gpuPercent!=90||b.cpuPercent!=55||b.gpuPercent!=60||c.cpuPercent!=25||c.gpuPercent!=25||d.cpuPercent!=73||d.gpuPercent!=81)return 1;
  if(h.cpuPercent!=75||h.gpuPercent!=75)return 1;
  if(msf::recommended_worker_count(h,16)!=12||msf::recommended_gpu_batch_size(h,64)!=48)return 2;
 if(msf::recommended_worker_count(c,16)!=4||msf::recommended_gpu_batch_size(c,64)!=16)return 2;
 c.gpuEnabled=false; if(c.gpuEnabled)return 3;
 std::cout<<"resource_policy=ok\n";return 0;
}
