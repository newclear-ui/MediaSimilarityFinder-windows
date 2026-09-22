#include "sampling.h"
#include <cmath>
#include <iostream>
int main(){
 if(std::abs(msf::sampling_interval(10)-1)>1e-9)return 1;
 if(std::abs(msf::sampling_interval(60)-2)>1e-9)return 2;
 if(std::abs(msf::sampling_interval(300)-5)>1e-9)return 3;
 if(std::abs(msf::sampling_interval(301)-10)>1e-9)return 4;
 if(std::abs(msf::sampling_interval(1801)-15)>1e-9)return 5;
 if(msf::sampling_times(9).size()!=10)return 6;
 if(msf::sampling_times(60).size()!=31)return 7;
 std::cout<<"video_sampling=ok\n";return 0;
}