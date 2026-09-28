#include "resource_policy.h"
#include <cstddef>
#include <iostream>
int main(){
  auto a=msf::make_policy(msf::ResourceMode::Maximum),b=msf::make_policy(msf::ResourceMode::Balanced),c=msf::make_policy(msf::ResourceMode::Gaming),d=msf::make_policy(msf::ResourceMode::Custom,73,81);
  auto h=msf::make_policy(msf::ResourceMode::High);
  if(a.cpuPercent!=90||a.gpuPercent!=90||b.cpuPercent!=55||b.gpuPercent!=60||c.cpuPercent!=25||c.gpuPercent!=25||d.cpuPercent!=73||d.gpuPercent!=81)return 1;
  if(h.cpuPercent!=75||h.gpuPercent!=75)return 1;
  const int inputs[]={-100,-1,0,1,9,10,11,50,89,90,91,99,100};
  const int expected[]={10,10,10,10,10,10,11,50,89,90,90,90,90};
  for(std::size_t i=0;i<sizeof(inputs)/sizeof(inputs[0]);++i){
    if(msf::normalize_user_cpu_percent(inputs[i])!=expected[i])return 4;
  }
  auto low=msf::make_policy(msf::ResourceMode::Custom,0,81);
  auto high=msf::make_policy(msf::ResourceMode::Custom,100,81);
  auto negative=msf::make_policy(msf::ResourceMode::Custom,-5,81);
  if(low.cpuPercent!=10||low.gpuPercent!=81)return 5;
  if(high.cpuPercent!=90||high.gpuPercent!=81)return 5;
  if(negative.cpuPercent!=10||negative.gpuPercent!=81)return 5;
  if(msf::recommended_worker_count(h,16)!=12||msf::recommended_gpu_batch_size(h,64)!=48)return 2;
 if(msf::recommended_worker_count(c,16)!=4||msf::recommended_gpu_batch_size(c,64)!=16)return 2;
 c.gpuEnabled=false; if(c.gpuEnabled)return 3;
 std::cout<<"resource_policy=ok\n";return 0;
}
