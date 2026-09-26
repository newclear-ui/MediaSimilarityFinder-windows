#pragma once
// Node B1: Minimal Adaptive Allocation. B2: recent-throughput feedback.
// B4: Stability Control — SMA smoothing, kill-band hysteresis, minimum
// hold on published decisions. All scheduler-internal; the engine keeps
// calling decide()/maybeReevaluate() and gating on lastDecision().
//
// B4 rules:
//  - Observed rates and loads pass through SMA-4 (feed-if-known-else-clear;
//    baselines are static and unsmoothed). B3 tests pin hold to 0 where they
//    exercise non-stability features.
//  - GPU kill hysteresis band: kill at availability <= 0.02 (load >= 98),
//    relieve above 0.05 (load < 95), keep previous state between.
//  - Minimum hold (default 10 s, settable, 0 disables): a changed decision
//    publishes only after the hold expires. The first change after decide()
//    is exempt (B1-compatible). adjustments counts publishes, so the counter
//    stops jittering with the system.
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include "resource_policy.h"
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
  // B3: live system load (percent). System readings include our own usage;
  // the rules below treat them as headroom signals with documented floors,
  // not as precise external-load attribution (that model is B3-simple;
  // self-attribution arrives with B4+ runtime accounting).
  bool cpuLoadKnown = false, gpuLoadKnown = false, memKnown = false;
  double cpuLoad = 0, gpuLoad = 0, memPressure = 0;
  // B6: the scheduler connects to Resource Mode policy. Modes tune
  // stability/responsiveness (floors, hold, kill band), never raw speed:
  // shares stay proportional. Manual's CPU limit is enforced upstream by
  // worker counts, so scheduler-side Manual == Balanced (documented).
  ResourceMode mode = ResourceMode::Balanced;
  // D1-reserved: queue depths have no producer on the scan path yet.
  // B3 carries the fields so D1 fills them without interface churn;
  // evaluate() ignores them until then.
  bool queueKnown = false;
  double cpuQueueDepth = 0, gpuQueueDepth = 0;
  // B5: transfer cost. Bytes moved per GPU unit (known from the packing
  // layout); bandwidth is a coarse default in MB/s that Node C calibrates
  // by measurement. Zero bytes disables the term (baseline path untouched).
  double transferBytesPerUnit = 0;
  double transferBandwidthMBps = 12000.0;
  // C1: profile baseline (images/sec pair from a usable PerformanceProfile).
  // Priority: live measured rates (B2) -> profile baseline (C1) ->
  // hardware proxies (B1). Pair-or-nothing, same ratio-honesty rule.
  bool profileBaselineKnown = false;
  double profileBaselineCpu = 0, profileBaselineGpu = 0;
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
  // B6: per-mode stability policy. Provisional values, documented in
  // build-history; long-run observation tunes them. Light is the legacy
  // Gaming alias (same enum value) and shares Gaming params.
  struct ModeParams { double cpuFloor; long long holdMs; double killAt; double relieveAbove; };
  static ModeParams paramsForMode(ResourceMode mode);
  // Effective hold: explicit setHoldMs() wins (tests, diagnostics);
  // otherwise the mode default. Default member -1 means "auto".
  void setHoldMs(long long ms) { holdMs_ = ms; }
  SchedulerDecision decide(const SchedulerHardware& hw);
  // Returns true when a (re)evaluation ran. Counts adjustments only when
  // the decision actually changed (share delta or backend flip).
  bool maybeReevaluate(const SchedulerHardware& hw, long long nowTickMs);
  const SchedulerDecision& lastDecision() const { return last_; }
  bool hasDecision() const { return decided_; }
  std::uint64_t evaluations() const { return evaluations_; }
  std::uint64_t adjustments() const { return adjustments_; }
  // B3: transitions into load-driven GPU kill (edge-counted).
  std::uint64_t throttles() const { return throttles_; }
  // Effective capacities for telemetry: observed rates when both backends
  // reported recently (same image/sec units), else the baselines.
  void currentCapacities(double& cpu, double& gpu) const;
  void reset();
private:
  static SchedulerDecision evaluate(const SchedulerHardware& hw, bool keepThrottled);
  static bool sameDecision(const SchedulerDecision& a, const SchedulerDecision& b);
  // B3: headroom scaling shared by evaluate() and currentCapacities().
  // outThrottled is set when live load kills a GPU share the base rule kept.
  static void effectivePair(const SchedulerHardware& hw, bool keepThrottled,
                            double& cpu, double& gpu,
                            std::string& reason, bool& throttled);
  // B4: fixed-window average over consecutive known samples. Unknown input
  // clears the lane so stale values never pose as fresh.
  struct Sma {
    void setN(std::size_t n) { n_ = n < 1 ? 1 : n; }
    void feed(double v);
    void clear();
    bool value(double& out) const;
  private:
    std::size_t n_ = 4;
    std::deque<double> q_;
  };
  SchedulerHardware smoothHw(const SchedulerHardware& hw);
  SchedulerDecision last_;
  bool decided_ = false;
  SchedulerHardware lastHw_;
  bool lastThrottled_ = false;
  std::uint64_t throttles_ = 0;
  long long holdMs_ = -1; // -1 = mode default (B6); explicit value wins
  static long long effectiveHoldMs(long long explicitHold, ResourceMode mode);
  long long lastPublishTickMs_ = -1;
  Sma smaCpuRate_, smaGpuRate_, smaCpuLoad_, smaGpuLoad_;
  long long reevalIntervalMs_ = 2000;
  long long lastEvalTickMs_ = -1;
  std::uint64_t evaluations_ = 0, adjustments_ = 0;
};
}
