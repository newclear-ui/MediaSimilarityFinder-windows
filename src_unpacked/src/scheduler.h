#pragma once
// Node B1 (0.9.4 line): Minimal Adaptive Allocation.
//
// Decides CPU/GPU work shares from baseline capacities only. No moving
// average, no hysteresis, no transfer/workload cost, no external-load model
// (those arrive in B2-B5). Pipeline topology is untouched: the engine calls
// decide() at scan start and maybeReevaluate() at existing phase points;
// the decision only gates which backend executes, exactly where the old
// gpuEnabled flag gated before.
//
// Capacity units are deliberately relative: only the RATIO matters for
// shares. B1 proxies are hardware_concurrency (CPU) and SM count (CUDA).
// Measured throughput replaces them in B2; calibration in Node C.
#include <cstdint>
#include <string>
namespace msf {
struct SchedulerHardware {
  int cpuThreads = 0;              // baseline CPU capacity proxy
  bool gpuEnabled = true;          // user GPU ON/OFF
  bool gpuAvailable = false;       // a concrete backend resolved
  double gpuComputeUnits = 0;      // baseline GPU capacity proxy (e.g. SMs)
  std::string backendName = "CPU"; // resolved backend ("CUDA", "CPU", ...)
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
  void reset();
private:
  static SchedulerDecision evaluate(const SchedulerHardware& hw);
  static bool sameDecision(const SchedulerDecision& a, const SchedulerDecision& b);
  SchedulerDecision last_;
  bool decided_ = false;
  long long reevalIntervalMs_ = 2000;
  long long lastEvalTickMs_ = -1;
  std::uint64_t evaluations_ = 0, adjustments_ = 0;
};
}
