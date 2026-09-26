#include "scheduler.h"
#include <cmath>
namespace msf {
namespace {
constexpr double kShareEpsilon = 1e-9;
} // namespace
void ThroughputWindow::add(double tSec, double units) {
  prune(tSec);
  samples_.emplace_back(tSec, units);
  prune(tSec);
}
double ThroughputWindow::rate(double nowSec) const {
  prune(nowSec);
  if (samples_.size() < 2) return -1.0;
  const double span = samples_.back().first - samples_.front().first;
  if (span <= 0.0) return -1.0;
  double sum = 0;
  for (const auto& s : samples_) sum += s.second;
  return sum / span;
}
void ThroughputWindow::clear() { samples_.clear(); }
void ThroughputWindow::prune(double nowSec) const {
  while (!samples_.empty() && nowSec - samples_.front().first > windowSec_)
    samples_.pop_front();
}
SchedulerDecision CpuGpuScheduler::evaluate(const SchedulerHardware& hw) {
  SchedulerDecision d;
  if (!hw.gpuEnabled) {
    d.cpuShare = 100.0; d.gpuShare = 0.0;
    d.backend = "CPU"; d.reason = "gpu_off"; d.gpuUsed = false;
    return d;
  }
  if (!hw.gpuAvailable) {
    d.cpuShare = 100.0; d.gpuShare = 0.0;
    d.backend = "CPU"; d.reason = "gpu_unavailable"; d.gpuUsed = false;
    return d;
  }
  double cpu = 0, gpu = 0;
  std::string reason;
  bool throttled = false;
  effectivePair(hw, cpu, gpu, reason, throttled);
  if (gpu <= 0.0) {
    // No capacity: distinguish missing hardware (B1) from live contention
    // (B3). cpu keeps its scaled value; shares stay exact.
    d.cpuShare = 100.0; d.gpuShare = 0.0;
    d.backend = "CPU"; d.reason = reason; d.gpuUsed = false;
    return d;
  }
  d.gpuShare = 100.0 * gpu / (cpu + gpu);
  d.cpuShare = 100.0 - d.gpuShare;
  d.backend = hw.backendName.empty() ? "CPU" : hw.backendName;
  d.reason = reason; d.gpuUsed = true;
  return d;
}
// B3 headroom rule. Base capacities come from the B2 branch (observed
// rates when both known, else baselines); live load scales them:
//   cpuAvail = (100 - sysCpu)/100 floored at 0.05 — the floor keeps us from
//     fully zeroing our own share on self-loaded systems (coarse B3 rule;
//     self-attribution is B4+ work).
//   gpuAvail = (100 - sysGpu)/100 with no floor — system GPU% is
//     external-dominated (our batches are sub-ms), so full contention may
//     legitimately converge to CPU. memPressure is recorded, not scaled
//     (B6 policy use).
void CpuGpuScheduler::effectivePair(const SchedulerHardware& hw, double& cpu, double& gpu,
                                    std::string& reason, bool& throttled) {
  double baseCpu = hw.cpuThreads > 0 ? (double)hw.cpuThreads : 1.0;
  double baseGpu = hw.gpuComputeUnits > 0 ? hw.gpuComputeUnits : 0.0;
  bool observed = false;
  if (hw.cpuRateKnown && hw.gpuRateKnown && hw.cpuRate > 0 && hw.gpuRate > 0) {
    baseCpu = hw.cpuRate;
    baseGpu = hw.gpuRate;
    observed = true;
  }
  if (baseGpu <= 0.0) {
    cpu = baseCpu; gpu = 0.0; reason = "no_gpu_capacity"; throttled = false;
    return;
  }
  double cpuAvail = 1.0, gpuAvail = 1.0;
  if (hw.cpuLoadKnown) {
    cpuAvail = (100.0 - hw.cpuLoad) / 100.0;
    if (cpuAvail < 0.05) cpuAvail = 0.05;
    if (cpuAvail > 1.0) cpuAvail = 1.0;
  }
  if (hw.gpuLoadKnown) {
    gpuAvail = (100.0 - hw.gpuLoad) / 100.0;
    if (gpuAvail < 0.0) gpuAvail = 0.0;
    if (gpuAvail > 1.0) gpuAvail = 1.0;
  }
  cpu = baseCpu * cpuAvail;
  gpu = baseGpu * gpuAvail;
  throttled = (gpu <= 0.0);
  reason = observed ? "observed_throughput" : "proportional_baseline";
  if (throttled) reason = "external_load_throttle";
}
bool CpuGpuScheduler::sameDecision(const SchedulerDecision& a, const SchedulerDecision& b) {
  return a.gpuUsed == b.gpuUsed && a.backend == b.backend &&
         std::fabs(a.gpuShare - b.gpuShare) < kShareEpsilon;
}
SchedulerDecision CpuGpuScheduler::decide(const SchedulerHardware& hw) {
  last_ = evaluate(hw);
  lastHw_ = hw;
  decided_ = true;
  ++evaluations_;
  lastThrottled_ = (last_.reason == "external_load_throttle");
  return last_;
}
bool CpuGpuScheduler::maybeReevaluate(const SchedulerHardware& hw, long long nowTickMs) {
  if (!decided_) { decide(hw); lastEvalTickMs_ = nowTickMs; return true; }
  if (reevalIntervalMs_ > 0 && lastEvalTickMs_ >= 0 && nowTickMs - lastEvalTickMs_ < reevalIntervalMs_)
    return false;
  lastEvalTickMs_ = nowTickMs;
  ++evaluations_;
  const SchedulerDecision next = evaluate(hw);
  lastHw_ = hw;
  const bool nowThrottled = (next.reason == "external_load_throttle");
  if (nowThrottled && !lastThrottled_) ++throttles_;
  lastThrottled_ = nowThrottled;
  if (!sameDecision(last_, next)) { last_ = next; ++adjustments_; }
  return true;
}
void CpuGpuScheduler::currentCapacities(double& cpu, double& gpu) const {
  std::string reason;
  bool throttled = false;
  effectivePair(lastHw_, cpu, gpu, reason, throttled);
}
void CpuGpuScheduler::reset() {
  last_ = SchedulerDecision{};
  lastHw_ = SchedulerHardware{};
  decided_ = false;
  lastThrottled_ = false;
  throttles_ = 0;
  lastEvalTickMs_ = -1;
  evaluations_ = adjustments_ = 0;
}
}
