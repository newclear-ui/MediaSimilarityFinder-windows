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
  const double cpu = hw.cpuThreads > 0 ? (double)hw.cpuThreads : 1.0;
  const double gpu = hw.gpuComputeUnits > 0 ? hw.gpuComputeUnits : 0.0;
  if (gpu <= 0.0) {
    d.cpuShare = 100.0; d.gpuShare = 0.0;
    d.backend = "CPU"; d.reason = "no_gpu_capacity"; d.gpuUsed = false;
    return d;
  }
  // B2: both backends recently observed (identical image/sec units) ->
  // shares follow the observed ratio. Anything else keeps the baseline
  // ratio (unknown is never treated as zero throughput).
  if (hw.cpuRateKnown && hw.gpuRateKnown && hw.cpuRate > 0 && hw.gpuRate > 0) {
    d.gpuShare = 100.0 * hw.gpuRate / (hw.cpuRate + hw.gpuRate);
    d.cpuShare = 100.0 - d.gpuShare;
    d.backend = hw.backendName.empty() ? "CPU" : hw.backendName;
    d.reason = "observed_throughput"; d.gpuUsed = true;
    return d;
  }
  // Proportional baseline: shares follow relative capacity, never a fixed
  // split. A weak GPU converges toward CPU-heavy by construction (B2 refines
  // the capacity inputs; the rule stays).
  d.gpuShare = 100.0 * gpu / (cpu + gpu);
  d.cpuShare = 100.0 - d.gpuShare;
  d.backend = hw.backendName.empty() ? "CPU" : hw.backendName;
  d.reason = "proportional_baseline"; d.gpuUsed = true;
  return d;
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
  if (!sameDecision(last_, next)) { last_ = next; ++adjustments_; }
  return true;
}
void CpuGpuScheduler::currentCapacities(double& cpu, double& gpu) const {
  if (lastHw_.cpuRateKnown && lastHw_.gpuRateKnown &&
      lastHw_.cpuRate > 0 && lastHw_.gpuRate > 0) {
    cpu = lastHw_.cpuRate;
    gpu = lastHw_.gpuRate;
    return;
  }
  cpu = lastHw_.cpuThreads > 0 ? (double)lastHw_.cpuThreads : 1.0;
  gpu = lastHw_.gpuComputeUnits > 0 ? lastHw_.gpuComputeUnits : 0.0;
}
void CpuGpuScheduler::reset() {
  last_ = SchedulerDecision{};
  lastHw_ = SchedulerHardware{};
  decided_ = false;
  lastEvalTickMs_ = -1;
  evaluations_ = adjustments_ = 0;
}
}
