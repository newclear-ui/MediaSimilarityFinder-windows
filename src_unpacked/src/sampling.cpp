#include "sampling.h"
namespace msf {
double sampling_interval(double d){if(d<=10)return 1;if(d<=60)return 2;if(d<=240)return 4;if(d<=960)return 8;if(d<=3840)return 16;return 32;}
std::vector<double> sampling_times(double d){std::vector<double>v;if(d<=0)return v;double s=sampling_interval(d);for(double t=0;t<d;t+=s)v.push_back(t);if(v.empty()||v.back()<d-.001)v.push_back(d);return v;}
}