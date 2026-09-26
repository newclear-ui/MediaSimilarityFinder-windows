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
  // 5. C3 tracker: normal runtime never fires.
  {
    msf::DeviationTracker t;
    check(!t.feed(120.0, 480.0, 120.0, 480.0, "id1"), "t-normal-1");
    check(!t.feed(125.0, 470.0, 120.0, 480.0, "id1"), "t-normal-2");
    check(t.consecutive() == 0, "t-normal-count");
  }
  // 6. C3 tracker: a single outlier never fires.
  {
    msf::DeviationTracker t;
    check(!t.feed(120.0, 480.0, 120.0, 480.0, "id1"), "t-out-base");
    check(!t.feed(10.0, 480.0, 120.0, 480.0, "id1"), "t-out-single");
    check(t.consecutive() == 1, "t-out-count");
    check(!t.feed(120.0, 480.0, 120.0, 480.0, "id1"), "t-out-recover");
    check(t.consecutive() == 0, "t-out-reset");
  }
  // 7. C3 tracker: repeated deviation fires exactly once per run.
  {
    msf::DeviationTracker t;
    check(!t.feed(60.0, 480.0, 120.0, 480.0, "id1"), "t-rep-1");
    check(!t.feed(60.0, 480.0, 120.0, 480.0, "id1"), "t-rep-2");
    check(t.feed(60.0, 480.0, 120.0, 480.0, "id1"), "t-rep-fire");
    check(t.consecutive() == 0, "t-rep-reset");
    check(!t.feed(60.0, 480.0, 120.0, 480.0, "id1"), "t-rep-fresh");
  }
  // 8. C3 tracker: profile switch resets the run; invalid inputs never fire.
  {
    msf::DeviationTracker t;
    check(!t.feed(60.0, 480.0, 120.0, 480.0, "id1"), "t-sw-1");
    check(!t.feed(60.0, 480.0, 120.0, 480.0, "id2"), "t-sw-reset");
    check(t.consecutive() == 1, "t-sw-count");
    check(!t.feed(-1.0, 480.0, 120.0, 480.0, "id2"), "t-invalid");
    check(t.consecutive() == 0, "t-invalid-reset");
    // CPU-only-like scan (no GPU observation) never feeds the trigger.
    check(!t.feed(90.0, -1.0, 120.0, 480.0, "id2"), "t-cpuonly");
    check(t.consecutive() == 0, "t-cpuonly-count");
  }
  // 9. C3 candidate consistency + update record round-trip.
  {
    check(msf::DeviationTracker::candidateConsistent(115.0, 470.0, 120.0, 480.0, 0.40),
          "cand-consistent");
    check(!msf::DeviationTracker::candidateConsistent(300.0, 480.0, 120.0, 480.0, 0.40),
          "cand-inconsistent");
    check(!msf::DeviationTracker::candidateConsistent(0.0, 480.0, 120.0, 480.0, 0.40),
          "cand-invalid");
    // Consistent update: metrics replaced, confidence stepped, record kept.
    const std::string path = tmpIni("calib-update.ini");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    msf::ProfileStore s;
    msf::PerformanceProfile p;
    msf::ProfileIdentity id = testIdentity();
    p.identity = id;
    p.confidence = 0.7;
    p.cpuThroughput = {120.0, MeasureState::Measured};
    p.gpuThroughput = {480.0, MeasureState::Measured};
    s.setProfile(p);
    check(s.save(path), "upd-save");
    msf::ProfileStore r;
    check(r.load(path), "upd-load");
    msf::PerformanceProfile upd = r.profile();
    const double oldConf = upd.confidence;
    upd.cpuThroughput = {110.0, MeasureState::Measured};
    upd.gpuThroughput = {450.0, MeasureState::Measured};
    upd.confidence = std::min(0.95, oldConf + 0.1);
    upd.lastUpdate = {"recalibration_consistent", upd.id, oldConf, upd.confidence, true};
    msf::ProfileStore w;
    w.setProfile(upd);
    check(w.save(path), "upd-save2");
    msf::ProfileStore r2;
    check(r2.load(path), "upd-reload");
    const auto& q = r2.profile();
    check(q.cpuThroughput.value == 110.0, "upd-values");
    check(q.lastUpdate.present, "upd-present");
    check(q.lastUpdate.reason == "recalibration_consistent", "upd-reason");
    check(q.lastUpdate.oldProfileId == q.id, "upd-ids");
    check(q.lastUpdate.oldConfidence == 0.7 && q.lastUpdate.newConfidence > 0.7, "upd-conf");
    // Inconsistent keep: metrics kept, confidence stepped down, record kept.
    msf::PerformanceProfile kp = q;
    const double kc = kp.confidence;
    kp.confidence = std::max(0.1, kc - 0.1);
    kp.lastUpdate = {"recalibration_inconsistent", kp.id, kc, kp.confidence, true};
    msf::ProfileStore w2;
    w2.setProfile(kp);
    check(w2.save(path), "keep-save");
    msf::ProfileStore r3;
    check(r3.load(path), "keep-reload");
    check(r3.profile().cpuThroughput.value == 110.0, "keep-values");
    check(r3.profile().confidence < kc, "keep-conf");
    check(r3.profile().lastUpdate.reason == "recalibration_inconsistent", "keep-reason");
  }
  if (failures) {
    std::cerr << "calibration failures=" << failures << "\n";
    return 1;
  }
  std::cout << "calibration=ok\n";
  return 0;
}
