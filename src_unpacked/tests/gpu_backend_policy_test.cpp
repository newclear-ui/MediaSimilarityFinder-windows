#include "gpu_backend.h"
#include <iostream>
int main(){
    msf::GpuBackend g; const auto info=g.detect();
    const auto b=g.recommendedBatchSize(256);
    if(!info.available){ if(b!=0){std::cerr<<"no-device batch must be zero\n";return 1;} std::cout<<"gpu_policy=skip_no_device\n"; return 0; }
    if(info.globalMemoryBytes==0||info.freeMemoryBytes==0||b==0||b>256){std::cerr<<"invalid GPU policy values\n";return 2;}
    std::cout<<"gpu_policy=ok batch="<<b<<" free="<<info.freeMemoryBytes<<"\n"; return 0;
}
