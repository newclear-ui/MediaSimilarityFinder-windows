#pragma once
#include <vector>
#include "video_policy.h"
namespace msf {
struct SamplePlan { double intervalSec = 5; std::vector<double> timestamps; };
SamplePlan make_sample_plan(double);
// 0.9.4.156: explicit interval override (seconds, uniform sweep). intervalOverrideSec>0 replaces the duration tier;
// <=0 keeps the legacy tier ladder. Short-form custom intervals ride here; long-form local windows do not exist yet.
SamplePlan make_sample_plan(double duration, double intervalOverrideSec);
// 0.9.4.171 (V cohort sampling, directive §4-§5): cohort-aware probe plan.
// - Default path (structured==false) is byte-identical to make_sample_plan()
//   with the short-form override, so existing fingerprints do not change.
// - Structured path (structured==true) starts from that same base and adds:
//   opening detail (0-30 s at longStructIntervalSec), main-start candidates
//   (30/60/90/120/180 s), and reverse-from-T endpoint probes
//   (T-15 s, T-30 s, T-1/2/4/8/12/20 min) plus last-60 s refinement at
//   longPreciseIntervalSec. All positions are merged, sorted ascending and
//   deduplicated, then decoded in ONE sequential sweep (E-3B RefuseAll
//   stands: no per-timestamp seeks, no reverse playback).
// - G1/G2 (<=60 s) with default intervals collapse back to the base set,
//   so short-form fingerprints are unchanged even when structured is on.
SamplePlan make_cohort_plan(double duration, const VideoPolicy& policy, bool structured);
// 0.9.4.171: structured sampling is opt-in until long-form accuracy gates
// pass (directive §8). Enabled by MSF_VIDEO_STRUCTURE=full (validation) or
// by non-default long-form intervals in the GUI policy (user explicitly
// asked for windowed probing). Default Auto policy stays on the legacy
// path with byte-identical output.
bool videoStructuredSamplingEnabled(const VideoPolicy& policy);
}  // namespace msf
