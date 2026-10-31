#pragma once
#include <vector>
namespace msf { struct SamplePlan{double intervalSec=5;std::vector<double> timestamps;}; SamplePlan make_sample_plan(double); // 0.9.4.156: explicit interval override (seconds, uniform sweep). intervalOverrideSec>0 replaces the duration tier; // <=0 keeps the legacy tier ladder. Short-form custom intervals ride here; long-form local windows do not exist yet.
SamplePlan make_sample_plan(double duration,double intervalOverrideSec); }
