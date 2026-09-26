// Node C2 Initial Calibration tests.
//
// Bounded probes, partial results, failure isolation, and the hardware /
// policy distinction (no device = not_available, user-off = not_measured).
// GPU-present cases degrade to skip-report when no CUDA device exists
// (same pattern as gpu_backend_policy_test). No absolute performance
// values asserted: machine speeds vary.
#include "calibration.h"
#include "profile.h"
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace {
int failures = 0;
void check(bool ok, const char* name) {
  if (ok) return;
  std::cerr << "fail: " << name << "\n";
  ++failures;
}
msf::ProfileIdentity testIdentity() {
  msf::ProfileIdentity id;
  id.cpuThreads = 8;
  id.gpuName = "Test GPU";
  id.gpuBackend = "CUDA";
  id.appVersion = "t";
  id.engineVersion = "t";
  return id;
}
std::string tmpIni(const char* name) {
  auto d = std::filesystem::temp_directory_path() / "msf_calib_test";
  std::error_code ec;
  std::filesystem::create_directories(d, ec);
  return (d / name).string();
}
} // namespace
int main() {
  using msf::Calibrator;
  using msf::CalibrationConfig;
  using msf::MeasureState;
  // 1. CPU-only: no device -> gpu not_available, cpu measured, complete.
  {
    Calibrator c;
    CalibrationConfig cfg;
    cfg.identity = testIdentity();
    cfg.identity.gpuName.clear();
    cfg.identity.gpuBackend = "CPU";
    const auto r = c.run(cfg, nullptr);
    check(r.attempted, "cpu-attempted");
    check(r.profile.cpuThroughput.state == MeasureState::Measured, "cpu-measured");
    check(r.profile.cpuThroughput.value > 0, "cpu-positive");
    check(r.profile.gpuThroughput.state == MeasureState::NotAvailable, "cpu-gpu-na");
    check(r.profile.gpuBatchThroughput.state == MeasureState::NotAvailable, "cpu-batch-na");
    check(r.completed, "cpu-completed");
    check(r.profile.confidence == 0.8, "cpu-confidence");
    check(r.telemetry.started && r.telemetry.completed, "cpu-telemetry-flags");
    check(r.telemetry.cpuState == MeasureState::Measured, "cpu-telemetry-state");
    check(r.telemetry.gpuState == MeasureState::NotAvailable, "cpu-telemetry-gpu");
    check(r.telemetry.durationMs >= 0, "cpu-duration");
    check(!r.telemetry.profileId.empty(), "cpu-profile-id");
  }
  // 2. Created profile reloads and classifies Exact.
  {
    Calibrator c;
    CalibrationConfig cfg;
    cfg.identity = testIdentity();
    const auto r = c.run(cfg, nullptr);
    const std::string path = tmpIni("calib-created.ini");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    msf::ProfileStore w;
    w.setProfile(r.profile);
    check(w.save(path), "reload-save");
    msf::ProfileStore r2;
    check(r2.load(path), "reload-load");
    // Identity GPU name differs from stored ("Test GPU" vs CPU-only run
    // above is irrelevant here: same identity object reused).
    check(r2.classify(testIdentity(), -1, (long long)std::time(nullptr)) ==
              msf::ProfileMatch::Exact,
          "reload-exact");
  }
  // 3. Partial: zero budget skips phases but never throws.
  {
    Calibrator c;
    CalibrationConfig cfg;
    cfg.identity = testIdentity();
    cfg.budgetMs = 0;
    cfg.cpuImages = 1000000; // would exceed any budget if attempted
    const auto r = c.run(cfg, nullptr);
    check(!r.completed, "partial-incomplete");
    check(r.profile.confidence == 0.5 || r.profile.confidence == 0.0, "partial-confidence");
    check(r.telemetry.state == MeasureState::Partial || r.telemetry.state == MeasureState::Failed,
          "partial-telemetry");
  }
  // 4. GPU-present cases: full run when allowed, policy skip when off.
  {
    msf::GpuBackend gpu;
    if (!gpu.available()) {
      std::cout << "calibration=ok gpu=skip_no_device\n";
    } else {
      Calibrator c;
      CalibrationConfig cfg;
      cfg.identity = testIdentity();
      const auto on = c.run(cfg, &gpu);
      check(on.profile.gpuThroughput.state == MeasureState::Measured, "gpu-measured");
      check(on.profile.gpuThroughput.value > 0, "gpu-positive");
      check(on.profile.gpuBatchThroughput.state == MeasureState::Measured, "gpu-batch");
      check(on.completed, "gpu-completed");
      cfg.gpuAllowed = false;
      const auto off = c.run(cfg, &gpu);
      check(off.profile.gpuThroughput.state == MeasureState::NotMeasured, "off-notmeasured");
      check(!off.completed, "off-partial");
      check(off.profile.cpuThroughput.state == MeasureState::Measured, "off-cpu-ok");
    }
  }
  if (failures) {
    std::cerr << "calibration failures=" << failures << "\n";
    return 1;
  }
  std::cout << "calibration=ok\n";
  return 0;
}
