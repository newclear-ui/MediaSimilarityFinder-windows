#include "calibration.h"
#include "fingerprint.h"
#include <chrono>
#include <cstdint>
#include <ctime>
#include <vector>
namespace msf {
namespace {
// Deterministic xorshift fill: varied data (never degenerate-zero buffers),
// identical across runs and machines. Throughput comparisons stay fair.
void fillProbe(std::vector<std::uint8_t>& buf, std::uint64_t seed) {
  std::uint64_t x = seed ? seed : 0x9e3779b97f4a7c15ULL;
  for (auto& b : buf) {
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    b = (std::uint8_t)(x & 0xFF);
  }
}
double nowMs() {
  return std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace
CalibrationResult Calibrator::run(const CalibrationConfig& cfg, GpuBackend* gpu) {
  CalibrationResult r;
  r.profile.identity = cfg.identity;
  r.profile.confidence = 0;
  const double t0 = nowMs();
  // Zero/negative budget means "measure nothing" (cheap caller-side skip
  // without special-casing every phase below).
  const bool budgeted = cfg.budgetMs > 0;
  auto overBudget = [&]() { return !budgeted || nowMs() - t0 > (double)cfg.budgetMs; };
  r.telemetry.started = true;
  r.telemetry.state = MeasureState::Partial;
  // Queue / HW-decode / resize / decode / transfer verdicts are structural:
  // no producer exists yet (D1 / F / decoder-internal / no kernel split).
  r.profile.queueLatencyMs.state = MeasureState::NotMeasured;
  r.telemetry.queueState = MeasureState::NotMeasured;
  r.profile.resizeThroughput.state = MeasureState::NotMeasured;
  r.telemetry.resizeState = MeasureState::NotMeasured;
  r.profile.decodeThroughput.state = MeasureState::NotMeasured;
  r.telemetry.decodeState = MeasureState::NotMeasured;
  r.profile.transferBandwidthMBps.state = MeasureState::NotMeasured;
  r.telemetry.transferState = MeasureState::NotMeasured;
  const bool gpuPresent = (gpu != nullptr) && gpu->available();
  try {
    // Phase 1: CPU fingerprint throughput (single-thread clean unit;
    // parallel scaling is worker topology, not calibration scope).
    if (cfg.cpuImages > 0 && !overBudget()) {
      r.attempted = true;
      std::vector<std::uint8_t> frame(1024);
      fillProbe(frame, 0x1234);
      volatile std::uint64_t sink = 0;
      const double p0 = nowMs();
      for (int i = 0; i < cfg.cpuImages; ++i) {
        frame[0] ^= (std::uint8_t)i; // defeat memoization; same work shape
        const auto h = perceptual_hash_pair_32(frame.data());
        sink += h.normal + h.mirrored;
      }
      const double dt = (nowMs() - p0) / 1000.0;
      (void)sink;
      if (dt > 0) {
        const double rate = (double)cfg.cpuImages / dt;
        r.profile.cpuThroughput = {rate, MeasureState::Measured};
        r.telemetry.cpuThroughput = rate;
        r.telemetry.cpuState = MeasureState::Measured;
      } else {
        r.profile.cpuThroughput.state = MeasureState::Failed;
        r.telemetry.cpuState = MeasureState::Failed;
        if (r.failedStage.empty()) r.failedStage = "cpu";
      }
    }
    // Phase 2: GPU fingerprint + batch throughput (one discarded warmup;
    // launch/init cost must not pose as steady throughput).
    if (cfg.gpuAllowed && gpuPresent && cfg.gpuBatchSize > 0 && cfg.gpuBatches > 0 && !overBudget()) {
      r.attempted = true;
      const std::size_t n = (std::size_t)cfg.gpuBatchSize;
      std::vector<std::uint8_t> block(n * 1024);
      fillProbe(block, 0x5678);
      std::vector<std::uint64_t> out(n);
      if (!gpu->hashBatch(block.data(), (std::uint64_t)n, out.data())) {
        r.profile.gpuThroughput.state = MeasureState::Failed;
        r.profile.gpuBatchThroughput.state = MeasureState::Failed;
        r.telemetry.gpuState = MeasureState::Failed;
        if (r.failedStage.empty()) r.failedStage = "gpu";
      } else {
        const double g0 = nowMs();
        int done = 0;
        for (int b = 0; b < cfg.gpuBatches && !overBudget(); ++b) {
          block[0] ^= (std::uint8_t)b;
          if (!gpu->hashBatch(block.data(), (std::uint64_t)n, out.data())) break;
          ++done;
        }
        const double dt = (nowMs() - g0) / 1000.0;
        if (done > 0 && dt > 0) {
          const double imgs = (double)(n * (std::size_t)done);
          const double rate = imgs / dt;
          const double batchRate = (double)done / dt;
          r.profile.gpuThroughput = {rate, MeasureState::Measured};
          r.profile.gpuBatchThroughput = {batchRate, MeasureState::Measured};
          r.telemetry.gpuThroughput = rate;
          r.telemetry.gpuState = MeasureState::Measured;
        } else {
          r.profile.gpuThroughput.state = MeasureState::Failed;
          r.profile.gpuBatchThroughput.state = MeasureState::Failed;
          r.telemetry.gpuState = MeasureState::Failed;
          if (r.failedStage.empty()) r.failedStage = "gpu";
        }
      }
    } else if (!cfg.gpuAllowed && gpuPresent) {
      // Policy-disabled device: capability exists, measurement refused.
      r.profile.gpuThroughput.state = MeasureState::NotMeasured;
      r.profile.gpuBatchThroughput.state = MeasureState::NotMeasured;
      r.telemetry.gpuState = MeasureState::NotMeasured;
    } else {
      // No device at all (CPU-only): unavailable, never zero.
      r.profile.gpuThroughput.state = MeasureState::NotAvailable;
      r.profile.gpuBatchThroughput.state = MeasureState::NotAvailable;
      r.telemetry.gpuState = MeasureState::NotAvailable;
    }
  } catch (...) {
    // Calibration never propagates: the engine proceeds on baselines.
    if (r.failedStage.empty()) r.failedStage = "exception";
  }
  r.telemetry.durationMs = nowMs() - t0;
  // Completion = every APPLICABLE metric measured. CPU-only machines are
  // complete with CPU alone (nothing else is measurable there); a skipped
  // GPU under user-off or any failure makes it partial.
  const bool cpuOk = r.profile.cpuThroughput.state == MeasureState::Measured;
  const bool gpuDone = r.profile.gpuThroughput.state == MeasureState::Measured;
  const bool gpuExcused = !gpuPresent; // CPU-only: nothing more to measure
  r.completed = r.attempted && cpuOk && (gpuDone || gpuExcused);
  r.telemetry.completed = r.completed;
  // Confidence (C2 numbers, reasons in build-history): full runs earn
  // conservative 0.8 (single run, no history); partial earns 0.5.
  // Live runtime overrides either way, so over-trust is impossible.
  r.profile.confidence = r.completed ? 0.8 : (r.attempted ? 0.5 : 0.0);
  r.telemetry.confidence = r.profile.confidence;
  r.telemetry.state = r.completed ? MeasureState::Measured : MeasureState::Partial;
  if (!r.attempted) r.telemetry.state = MeasureState::Failed;
  r.profile.id = ProfileStore::deriveId(r.profile.identity);
  r.telemetry.profileId = r.profile.id;
  r.telemetry.profileVersion = std::to_string(PerformanceProfile::kProfileVersion);
  return r;
}
}
