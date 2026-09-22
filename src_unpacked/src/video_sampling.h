#pragma once
#include <vector>
namespace msf { struct SamplePlan{double intervalSec=5;std::vector<double> timestamps;}; SamplePlan make_sample_plan(double); }
