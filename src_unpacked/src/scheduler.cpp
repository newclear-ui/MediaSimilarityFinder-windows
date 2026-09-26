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
SchedulerDecision CpuGpuScheduler::evaluate(const SchedulerHardware& hw, bool keepThrottled) {
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
  effectivePair(hw, keepThrottled, cpu, gpu, reason, throttled);
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
// B6: per-mode stability policy. Modes tune responsiveness, never raw
// speed: shares stay proportional under every mode. Values are provisional
// (build-history); long-run observation tunes them.
//   Maximum: nimble (5 s hold), self-prioritizing floor, standard band.
//   High: standard calm with a slightly higher self floor.
//   Balanced/Custom: the B4-validated defaults (existing tests pin these).
//   Gaming (= legacy Light alias): yields early (kill at load>=95),
//     returns late (relieve below 90), holds long (30 s), cuts deepest.
CpuGpuScheduler::ModeParams CpuGpuScheduler::paramsForMode(ResourceMode mode) {
  switch (mode) {
    case ResourceMode::Maximum:
      return {0.10, 5000, 0.02, 0.05};
    case ResourceMode::High:
      return {0.07, 10000, 0.02, 0.05};
    case ResourceMode::Gaming:
      return {0.02, 30000, 0.05, 0.10};
    case ResourceMode::Balanced:
    case ResourceMode::Custom:
    default:
      return {0.05, 10000, 0.02, 0.05};
  }
}
long long CpuGpuScheduler::effectiveHoldMs(long long explicitHold, ResourceMode mode) {
  if (explicitHold >= 0) return explicitHold;
  return paramsForMode(mode).holdMs;
}
// B3 headroom rule (B6: floors and band edges now come from the mode).
// Base capacities come from the B2 branch (observed rates when both known,
// else baselines); live load scales them:
//   cpuAvail = (100 - sysCpu)/100 floored at the mode floor — the floor
//     keeps us from fully zeroing our own share on self-loaded systems
//     (coarse rule; self-attribution is future runtime-accounting work).
//   gpuAvail = (100 - sysGpu)/100 with no floor — system GPU% is
//     external-dominated (our batches are sub-ms), so full contention may
//     legitimately converge to CPU. memPressure is recorded, not scaled.
void CpuGpuScheduler::effectivePair(const SchedulerHardware& hw, bool keepThrottled,
                                    double& cpu, double& gpu,
                                    std::string& reason, bool& throttled) {  double baseCpu = hw.cpuThreads > 0 ? (double)hw.cpuThreads : 1.0;
  double baseGpu = hw.gpuComputeUnits > 0 ? hw.gpuComputeUnits : 0.0;
  bool observed = false;
  if (hw.cpuRateKnown && hw.gpuRateKnown && hw.cpuRate > 0 && hw.gpuRate > 0) {
    baseCpu = hw.cpuRate;
    baseGpu = hw.gpuRate;
    observed = true;
    // B5: total-cost rule. Observed GPU rate is compute-only throughput;
    // each unit also pays transferBytesPerUnit at the assumed bandwidth.
    // Effective rate = 1 / (computeTime + transferTime). Workload-specific
    // cost variation is already embedded in the observed rates; an explicit
    // workload model belongs to Node E. Baseline (unobserved) path skips
    // this term: without rate units there is nothing to add seconds to.
    if (hw.transferBytesPerUnit > 0 && hw.transferBandwidthMBps > 0) {
      const double tSec = hw.transferBytesPerUnit / (hw.transferBandwidthMBps * 1e6);
      if (tSec > 0) baseGpu = 1.0 / (1.0 / baseGpu + tSec);
    }
  }
  if (baseGpu <= 0.0) {
    cpu = baseCpu; gpu = 0.0; reason = "no_gpu_capacity"; throttled = false;
    return;
  }
  double cpuAvail = 1.0, gpuAvail = 1.0;
  const ModeParams mp = paramsForMode(hw.mode);
  if (hw.cpuLoadKnown) {
    cpuAvail = (100.0 - hw.cpuLoad) / 100.0;
    if (cpuAvail < mp.cpuFloor) cpuAvail = mp.cpuFloor;
    if (cpuAvail > 1.0) cpuAvail = 1.0;
  }
  if (hw.gpuLoadKnown) {
    gpuAvail = (100.0 - hw.gpuLoad) / 100.0;
    if (gpuAvail < 0.0) gpuAvail = 0.0;
    if (gpuAvail > 1.0) gpuAvail = 1.0;
  }
  cpu = baseCpu * cpuAvail;
  gpu = baseGpu * gpuAvail;
  // B4 kill-band hysteresis, B6 mode edges: kill at/below killAt, relieve
  // above relieveAbove, keep the previous (published) state between.
  bool kill = false, relieve = false;
  if (hw.gpuLoadKnown) {
    if (gpuAvail <= mp.killAt) kill = true;
    else if (gpuAvail > mp.relieveAbove) relieve = true;
  }
  if (kill || (!relieve && keepThrottled)) {
    gpu = 0.0;
    throttled = true;
    reason = "external_load_throttle";
    return;
  }
  throttled = false;
  reason = observed ? "observed_throughput" : "proportional_baseline";
}
bool CpuGpuScheduler::sameDecision(const SchedulerDecision& a, const SchedulerDecision& b) {
  return a.gpuUsed == b.gpuUsed && a.backend == b.backend &&
         std::fabs(a.gpuShare - b.gpuShare) < kShareEpsilon;
}
void CpuGpuScheduler::Sma::feed(double v) {
  q_.push_back(v);
  while (q_.size() > n_) q_.pop_front();
}
void CpuGpuScheduler::Sma::clear() { q_.clear(); }
bool CpuGpuScheduler::Sma::value(double& out) const {
  if (q_.empty()) return false;
  double sum = 0;
  for (double v : q_) sum += v;
  out = sum / (double)q_.size();
  return true;
}
SchedulerHardware CpuGpuScheduler::smoothHw(const SchedulerHardware& hw) {
  SchedulerHardware s = hw;
  auto lane = [](Sma& m, bool known, double v, bool& oKnown, double& oVal) {
    if (known) m.feed(v);
    else m.clear();
    double a = 0;
    if (m.value(a)) { oKnown = true; oVal = a; }
    else { oKnown = false; oVal = 0; }
  };
  lane(smaCpuRate_, hw.cpuRateKnown, hw.cpuRate, s.cpuRateKnown, s.cpuRate);
  lane(smaGpuRate_, hw.gpuRateKnown, hw.gpuRate, s.gpuRateKnown, s.gpuRate);
  lane(smaCpuLoad_, hw.cpuLoadKnown, hw.cpuLoad, s.cpuLoadKnown, s.cpuLoad);
  lane(smaGpuLoad_, hw.gpuLoadKnown, hw.gpuLoad, s.gpuLoadKnown, s.gpuLoad);
  return s;
}
SchedulerDecision CpuGpuScheduler::decide(const SchedulerHardware& hw) {
  const SchedulerHardware s = smoothHw(hw);
  last_ = evaluate(s, false);
  lastHw_ = s;
  decided_ = true;
  ++evaluations_;
  // Fresh start: the next change is hold-exempt (B1-compatible), and the
  // published throttle state tracks the fresh decision.
  lastPublishTickMs_ = -1;
  lastThrottled_ = (last_.reason == "external_load_throttle");
  return last_;
}
bool CpuGpuScheduler::maybeReevaluate(const SchedulerHardware& hw, long long nowTickMs) {
  if (!decided_) { decide(hw); lastEvalTickMs_ = nowTickMs; return true; }
  if (reevalIntervalMs_ > 0 && lastEvalTickMs_ >= 0 && nowTickMs - lastEvalTickMs_ < reevalIntervalMs_)
    return false;
  lastEvalTickMs_ = nowTickMs;
  ++evaluations_;
  const SchedulerHardware s = smoothHw(hw);
  const SchedulerDecision next = evaluate(s, lastThrottled_);
  lastHw_ = s;
  if (sameDecision(last_, next)) return true;
  // B4 minimum hold, B6 mode default: the published (acted) decision only
  // moves after the hold expires. Telemetry inputs (lastHw_) stay fresh.
  const long long hold = effectiveHoldMs(holdMs_, hw.mode);
  if (hold > 0 && lastPublishTickMs_ >= 0 && nowTickMs - lastPublishTickMs_ < hold)
    return true;
  const bool nowThrottled = (next.reason == "external_load_throttle");
  if (nowThrottled && !lastThrottled_) ++throttles_;
  lastThrottled_ = nowThrottled;
  last_ = next;
  lastPublishTickMs_ = nowTickMs;
  ++adjustments_;
  return true;
}
void CpuGpuScheduler::currentCapacities(double& cpu, double& gpu) const {
  std::string reason;
  bool throttled = false;
  effectivePair(lastHw_, lastThrottled_, cpu, gpu, reason, throttled);
}
void CpuGpuScheduler::reset() {
  last_ = SchedulerDecision{};
  lastHw_ = SchedulerHardware{};
  decided_ = false;
  lastThrottled_ = false;
  throttles_ = 0;
  lastPublishTickMs_ = -1;
  smaCpuRate_.clear();
  smaGpuRate_.clear();
  smaCpuLoad_.clear();
  smaGpuLoad_.clear();
  lastEvalTickMs_ = -1;
  evaluations_ = adjustments_ = 0;
}
}
