#include "sampling.h"
#include "video_sampling.h"
#include <cmath>
#include <iostream>
static double coverage(const std::vector<double>& fine, const std::vector<double>& coarse){
  std::size_t core=0, hit=0;
  for(std::size_t j=0;j+1<coarse.size();++j){ ++core;
    for(std::size_t i=0;i<fine.size();++i) if(std::abs(fine[i]-coarse[j])<1e-3){ ++hit; break; } }
  return core ? (double)hit/(double)core : 0;
}
int main(){
  if(std::abs(msf::sampling_interval(10)-1)>1e-9)return 1;
  if(std::abs(msf::sampling_interval(60)-2)>1e-9)return 2;
  if(std::abs(msf::sampling_interval(300)-8)>1e-9)return 3;
  if(std::abs(msf::sampling_interval(301)-8)>1e-9)return 4;
  if(std::abs(msf::sampling_interval(1801)-16)>1e-9)return 5;
  if(msf::sampling_times(9).size()!=10)return 6;
  if(msf::sampling_times(60).size()!=31)return 7;
  auto p5998=msf::make_sample_plan(59.98), p6002=msf::make_sample_plan(60.02);
  if(std::abs(p5998.intervalSec-2)>1e-9||std::abs(p6002.intervalSec-4)>1e-9) return 8;
  if(coverage(p5998.timestamps,p6002.timestamps)<0.8) return 9;
  auto p999=msf::make_sample_plan(9.99), p1002=msf::make_sample_plan(10.02);
  if(std::abs(p999.intervalSec-1)>1e-9||std::abs(p1002.intervalSec-2)>1e-9) return 10;
  if(coverage(p999.timestamps,p1002.timestamps)<0.8) return 11;
  auto p2999=msf::make_sample_plan(299.9), p3001=msf::make_sample_plan(300.1);
  if(std::abs(p2999.intervalSec-p3001.intervalSec)>1e-9) return 12;
  std::cout<<"video_sampling=ok\n";return 0;
}