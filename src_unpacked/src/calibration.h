#pragma once
// Node C2: Initial Calibration (short bounded probes, no user files).
//
// Measures what current code can measure honestly and nothing else:
//   CPU fingerprint throughput ??perceptual_hash_pair_32 over synthetic
//     32x32 frames (same unit the engine hashes).
//   GPU fingerprint + batch throughput ??GpuBackend::hashBatch over packed
//     synthetic frames (warmup discarded). Mirror hashes stay CPU-side in
//     the engine, so only the hashBatch rate is claimed here.
//   resize/conversion ??NOT measurable: scaling happens inside the image
//     decoder (WIC/FFmpeg target 32x32), with no separable software step.
//   CPU decode baseline ??NOT measurable pre-walk: the scan streams
//     (walk/analysis overlap), so no file set exists before analysis, and
//     sampling user media mid-scan would perturb the measurement. C3+
//     candidate: opportunistic decode stats from real scans.
//   transfer bandwidth ??NOT measurable: no kernel-side transfer split
//     exists (established in B5); the B5 assumption keeps flowing.
//   queue latency ??not_measured (D1 instruments). HW decode ??//     not_available (Node F).
// Budgets bound every phase; leftovers stay partial, failures never throw
// out (the engine must never fail a search over calibration).
#include <string>
#include <vector>
#include "benchmark.h"
#include "gpu_backend.h"
#include "profile.h"
namespace msf {
struct CalibrationConfig {
  int cpuImages = 300;        // synthetic CPU hash units
  int gpuBatchSize = 64;      // images per GPU batch call
  int gpuBatches = 6;         // timed batches (plus 1 discarded warmup)
  long long budgetMs = 8000;  // total guard; phases abort past it (partial)
  bool gpuAllowed = true;     // false under user GPU-off (policy, not hardware)
  ProfileIdentity identity;   // hardware+versions, filled by the engine
};
struct CalibrationResult {
  PerformanceProfile profile; // metrics with states; never fake numbers
  CalibrationTelemetry telemetry; // filled for direct bench_ copy
  bool completed = false;     // every applicable metric measured
  bool attempted = false;     // at least one probe ran
  std::string failedStage;    // "" when none failed
};
class Calibrator {
public:
  CalibrationResult run(const CalibrationConfig& cfg, GpuBackend* gpu);
};
// C4: the exact predicate the engine uses to accept a fresh candidate for
// a profile update. Extracted (not inlined in the scan lambda) so the
// "failed calibration keeps the existing profile" rule is unit-testable:
// anything false here means no write happens, existing file untouched.
bool calibrationUsableForUpdate(const CalibrationResult& cand);
// Node C3: Opportunistic Recalibration trigger.
//
// A single outlier must never replace a profile: only K consecutive scans
// whose live observation deviates from the feeding baseline fire the
// trigger. Thresholds are fixed policy (build-history), not tunables:
// deviation above 25% on either rate counts; 3 in a row fires.
// Candidate consistency uses 40% tolerance against the triggering live
// observation; confidence moves in 0.1 steps (cap 0.95, floor 0.1).
struct DeviationPolicy {
  double threshold = 0.25;
  int repetitions = 3;
  double consistencyTol = 0.40;
  double confidenceStep = 0.1;
  double confidenceMax = 0.95;
  double confidenceMin = 0.1;
};
class DeviationTracker {
public:
  void configure(const DeviationPolicy& p) { policy_ = p; }
  void reset();
  void resetForProfile(const std::string& profileId);
  // One scan's live observation vs the profile baseline that fed it.
  // Returns true exactly on the firing scan (counter resets after fire
  // so the next trigger needs a fresh repetition run).
  bool feed(double liveCpu, double liveGpu, double profCpu, double profGpu,
            const std::string& profileId);
  int consecutive() const { return consecutive_; }
  // Fresh candidate agrees with the triggering live observation?
  static bool candidateConsistent(double candCpu, double candGpu,
                                  double liveCpu, double liveGpu, double tol);
private:
  DeviationPolicy policy_;
  int consecutive_ = 0;
  std::string lastProfileId_;
  bool armed_ = false;
};
}
