#include "sampling.h"
namespace msf {
double sampling_interval(double d){if(d<=10)return 1;if(d<=60)return 2;if(d<=300)return 5;if(d<=1800)return 10;if(d<=3600)return 15;return 30;}
std::vector<double> sampling_times(double d){std::vector<double>v;if(d<=0)return v;double s=sampling_interval(d);for(double t=0;t<d;t+=s)v.push_back(t);if(v.empty()||v.back()<d-.001)v.push_back(d);return v;}
}