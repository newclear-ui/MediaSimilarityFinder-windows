#include "video_sampling.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
namespace msf {
SamplePlan make_sample_plan(double d) {
  SamplePlan p;
  if (d <= 0) return p;
  if (d <= 10)
    p.intervalSec = 1;
  else if (d <= 60)
    p.intervalSec = 2;
  else if (d <= 240)
    p.intervalSec = 4;
  else if (d <= 960)
    p.intervalSec = 8;
  else if (d <= 3840)
    p.intervalSec = 16;
  else
    p.intervalSec = 32;
  for (double t = 0; t < d; t += p.intervalSec) p.timestamps.push_back(t);
  if (p.timestamps.empty() || p.timestamps.back() < d - .001) p.timestamps.push_back(d);
  return p;
}
// 0.9.4.156: intervalOverrideSec>0 replaces the duration tier (uniform sweep).
SamplePlan make_sample_plan(double d, double intervalOverrideSec) {
  SamplePlan p;
  if (d <= 0) return p;
  if (intervalOverrideSec > 0)
    p.intervalSec = intervalOverrideSec;
  else if (d <= 10)
    p.intervalSec = 1;
  else if (d <= 60)
    p.intervalSec = 2;
  else if (d <= 240)
    p.intervalSec = 4;
  else if (d <= 960)
    p.intervalSec = 8;
  else if (d <= 3840)
    p.intervalSec = 16;
  else
    p.intervalSec = 32;
  for (double t = 0; t < d; t += p.intervalSec) p.timestamps.push_back(t);
  if (p.timestamps.empty() || p.timestamps.back() < d - .001) p.timestamps.push_back(d);
  return p;
}
bool videoStructuredSamplingEnabled(const VideoPolicy& policy) {
  const char* v = std::getenv("MSF_VIDEO_STRUCTURE");
  if (v && std::string(v) == "full") return true;
  // GUI opt-in: the user explicitly set long-form window intervals, so the
  // structured path honors them. Defaults stay on the legacy path.
  if (policy.longStructIntervalSec != kVideoLongStructIntervalDefault) return true;
  if (policy.longPreciseIntervalSec != kVideoLongPreciseIntervalDefault) return true;
  return false;
}
SamplePlan make_cohort_plan(double d, const VideoPolicy& policy, bool structured) {
  const double shortIv = (d <= 60.0) ? policy.shortIntervalSec : 0.0;
  SamplePlan base = make_sample_plan(d, shortIv);
  if (!structured || d <= 0) return base;
  // Structured windows (directive §4.3-§4.5, §5): merge extra probe
  // positions into the base sweep, then sort ascending for the single
  // sequential decode. No random seeks; reverse is position selection only.
  std::vector<double> extra;
  const double ls = (policy.longStructIntervalSec > 0) ? policy.longStructIntervalSec
                                                       : kVideoLongStructIntervalDefault;
  const double lp = (policy.longPreciseIntervalSec > 0) ? policy.longPreciseIntervalSec
                                                        : kVideoLongPreciseIntervalDefault;
  // Opening detail: 0-30 s at longStruct steps.
  if (d > 0) {
    const double openEnd = std::min(30.0, d);
    for (double t = 0.0; t <= openEnd + 1e-9; t += ls) extra.push_back(t);
  }
  // Main-start boundary candidates (fixed examples, not kept targets).
  for (double t : {30.0, 60.0, 90.0, 120.0, 180.0}) {
    if (t > 0 && t < d) extra.push_back(t);
  }
  // Reverse-from-T endpoint probes (§5): computed from T, consumed ascending.
  for (double off : {15.0, 30.0, 60.0, 120.0, 240.0, 480.0, 720.0, 1200.0}) {
    const double t = d - off;
    if (t >= 0) extra.push_back(t);
  }
  // Last-60 s refinement at longPrecise steps (credits/boundary detail).
  if (d > 0) {
    const double winStart = std::max(0.0, d - 60.0);
    for (double t = winStart; t < d; t += lp) extra.push_back(t);
  }
  std::vector<double> all = base.timestamps;
  all.insert(all.end(), extra.begin(), extra.end());
  std::sort(all.begin(), all.end());
  // Dedup within 1 ms (decoder timestamp resolution) and clamp to [0, d].
  std::vector<double> out;
  out.reserve(all.size());
  for (double t : all) {
    if (!(t >= 0)) continue;
    if (t > d) t = d;
    if (!out.empty() && std::abs(t - out.back()) < 1e-3) continue;
    out.push_back(t);
  }
  if (out.empty() || out.back() < d - 1e-3) {
    if (!out.empty() && std::abs(out.back() - d) < 1e-3)
      out.back() = d;
    else
      out.push_back(d);
  }
  // Safety cap: structured must never explode the sweep (base for a 3 h
  // movie is ~340 probes; windows add < 40 with defaults).
  if (out.size() > 10000) out.resize(10000);
  SamplePlan p;
  p.intervalSec = base.intervalSec;
  p.timestamps = std::move(out);
  return p;
}
}  // namespace msf
