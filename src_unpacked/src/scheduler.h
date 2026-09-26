#pragma once
// Node B1 (0.9.4 line): Minimal Adaptive Allocation. Node B2 adds recent
// throughput feedback (see ThroughputWindow below).
//
// B1 decides CPU/GPU work shares from baseline capacities only. B2 feeds
// observed image-path throughput in: when both backends have recent rates,
// shares follow the observed ratio; otherwise the baseline ratio holds.
// No smoothing (B4), no transfer/workload cost (B5), no external-load model
// (B3). Pipeline topology is untouched.
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
namespace msf {
// Simple recent-window rate observer: push (timestampSec, completedUnits),
// rate() returns units/sec over the retained window, or -1 when unknown
// (fewer than 2 samples or everything expired). No smoothing by design;
// B4 owns smoothing.
class ThroughputWindow {
public:
  void setWindowSec(double s) { windowSec_ = s > 0 ? s : 1.0; }
  void add(double tSec, double units);
  double rate(double nowSec) const;
  void clear();
  std::size_t samples() const { return samples_.size(); }
private:
  void prune(double nowSec) const;
  double windowSec_ = 30.0;
  mutable std::deque<std::pair<double, double>> samples_;
};
struct SchedulerHardware {
  int cpuThreads = 0;              // baseline CPU capacity proxy
  bool gpuEnabled = true;          // user GPU ON/OFF
  bool gpuAvailable = false;       // a concrete backend resolved
  double gpuComputeUnits = 0;      // baseline GPU capacity proxy (e.g. SMs)
  std::string backendName = "CPU"; // resolved backend ("CUDA", "CPU", ...)
  // B2: recent image-path throughput (images/sec). Unknown unless flagged;
  // unknown values are never numeric zero (Node A measurement rule).
  bool cpuRateKnown = false, gpuRateKnown = false;
  double cpuRate = 0, gpuRate = 0;
};
struct SchedulerDecision {
  double cpuShare = 100.0, gpuShare = 0.0; // percent, sum to 100
  std::string backend = "CPU";             // what actually executes
  std::string reason;                      // gpu_off | gpu_unavailable |
                                           // no_gpu_capacity | proportional_baseline
  bool gpuUsed = false;
};
class CpuGpuScheduler {
public:
  // Re-evaluation cadence for long scans. Not user-exposed (brief B1).
  // B1 inputs are static so re-runs are stable; the counter still proves
  // the cadence fires, and B2+ live inputs make it adjust.
  void setReevalIntervalMs(long long ms) { reevalIntervalMs_ = ms < 0 ? 0 : ms; }
  SchedulerDecision decide(const SchedulerHardware& hw);
  // Returns true when a (re)evaluation ran. Counts adjustments only when
  // the decision actually changed (share delta or backend flip).
  bool maybeReevaluate(const SchedulerHardware& hw, long long nowTickMs);
  const SchedulerDecision& lastDecision() const { return last_; }
  bool hasDecision() const { return decided_; }
  std::uint64_t evaluations() const { return evaluations_; }
  std::uint64_t adjustments() const { return adjustments_; }
  // Effective capacities for telemetry: observed rates when both backends
  // reported recently (same image/sec units), else the baselines.
  void currentCapacities(double& cpu, double& gpu) const;
  void reset();
private:
  static SchedulerDecision evaluate(const SchedulerHardware& hw);
  static bool sameDecision(const SchedulerDecision& a, const SchedulerDecision& b);
  SchedulerDecision last_;
  bool decided_ = false;
  SchedulerHardware lastHw_;
  long long reevalIntervalMs_ = 2000;
  long long lastEvalTickMs_ = -1;
  std::uint64_t evaluations_ = 0, adjustments_ = 0;
};
}
