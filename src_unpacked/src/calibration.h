#pragma once
// Node C2: Initial Calibration (short bounded probes, no user files).
//
// Measures what current code can measure honestly and nothing else:
//   CPU fingerprint throughput — perceptual_hash_pair_32 over synthetic
//     32x32 frames (same unit the engine hashes).
//   GPU fingerprint + batch throughput — GpuBackend::hashBatch over packed
//     synthetic frames (warmup discarded). Mirror hashes stay CPU-side in
//     the engine, so only the hashBatch rate is claimed here.
//   resize/conversion — NOT measurable: scaling happens inside the image
//     decoder (WIC/FFmpeg target 32x32), with no separable software step.
//   CPU decode baseline — NOT measurable pre-walk: the scan streams
//     (walk/analysis overlap), so no file set exists before analysis, and
//     sampling user media mid-scan would perturb the measurement. C3+
//     candidate: opportunistic decode stats from real scans.
//   transfer bandwidth — NOT measurable: no kernel-side transfer split
//     exists (established in B5); the B5 assumption keeps flowing.
//   queue latency — not_measured (D1 instruments). HW decode —
//     not_available (Node F).
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
}
