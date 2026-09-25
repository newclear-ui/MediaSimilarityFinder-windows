#include "media_analyzer.h"
#include <iostream>
int main(){auto a=msf::analyze_image_bytes({1,2,3});auto v1=msf::analyze_video_duration("a",9);auto v2=msf::analyze_video_duration("b",60);auto v3=msf::analyze_video_duration("c",301);if(!a.fingerprint||v1.intervalSec!=1||v2.intervalSec!=2||v3.intervalSec!=8||v1.samples!=10||v2.samples!=31)return 1;std::cout<<"media_analyzer=ok\n";return 0;}
