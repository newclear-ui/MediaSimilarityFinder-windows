#include "resource_policy.h"
#include <cstddef>
namespace msf {
ResourcePolicy make_policy(ResourceMode m,int c,int g){
    ResourcePolicy p; p.mode=m;
    if(m==ResourceMode::Maximum){p.cpuPercent=90;p.gpuPercent=90;}
    else if(m==ResourceMode::High){p.cpuPercent=75;p.gpuPercent=75;}
    else if(m==ResourceMode::Balanced){p.cpuPercent=55;p.gpuPercent=60;}
    else if(m==ResourceMode::Gaming){p.cpuPercent=25;p.gpuPercent=25;}
    else {p.cpuPercent=std::clamp(c,5,100);p.gpuPercent=std::clamp(g,5,100);}
    return p;
}
int recommended_worker_count(const ResourcePolicy& p,int hardwareThreads){
    hardwareThreads=std::max(1,hardwareThreads);
    return std::max(1,std::min(hardwareThreads,(hardwareThreads*p.cpuPercent+99)/100));
}
std::size_t recommended_gpu_batch_size(const ResourcePolicy& p,std::size_t base){
    base=std::max<std::size_t>(1,base);
    return std::max<std::size_t>(1,(base*static_cast<std::size_t>(p.gpuPercent)+99)/100);
}
}
