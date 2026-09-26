// Node B1 Minimal Adaptive Allocation tests.
//
// Covers the brief's first-validation table with synthetic hardware views
// (no device needed): GPU OFF -> CPU 100%; GPU ON + valid GPU -> shared;
// slow GPU -> CPU-lean; unavailable -> CPU fallback; plus re-evaluation
// cadence (stable inputs never adjust) and telemetry recording.
#include "scheduler.h"
#include "benchmark.h"
#include <cmath>
#include <iostream>
namespace {
int failures = 0;
void check(bool ok, const char* name) {
  if (!ok) { std::cerr << "fail: " << name << "\n"; ++failures; }
}
bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }
} // namespace
int main() {
  using msf::CpuGpuScheduler;
  using msf::SchedulerHardware;
  // 1. GPU OFF -> CPU 100%.
  {
    CpuGpuScheduler s;
    SchedulerHardware hw;
    hw.cpuThreads = 16; hw.gpuEnabled = false;
    hw.gpuAvailable = true; hw.gpuComputeUnits = 80; hw.backendName = "CUDA";
    const auto d = s.decide(hw);
    check(near(d.cpuShare, 100.0) && near(d.gpuShare, 0.0), "off-shares");
    check(!d.gpuUsed && d.backend == "CPU" && d.reason == "gpu_off", "off-meta");
  }
  // 2. GPU unavailable -> CPU fallback (keeps user ON intent intact).
  {
    CpuGpuScheduler s;
    SchedulerHardware hw;
    hw.cpuThreads = 8; hw.gpuEnabled = true;
    hw.gpuAvailable = false; hw.gpuComputeUnits = 0; hw.backendName = "CPU";
    const auto d = s.decide(hw);
    check(near(d.cpuShare, 100.0) && !d.gpuUsed, "unavail-shares");
    check(d.reason == "gpu_unavailable" && d.backend == "CPU", "unavail-meta");
  }
  // 3. GPU ON + valid GPU -> proportional shares summing to 100.
  {
    CpuGpuScheduler s;
    SchedulerHardware hw;
    hw.cpuThreads = 16; hw.gpuEnabled = true;
    hw.gpuAvailable = true; hw.gpuComputeUnits = 80; hw.backendName = "CUDA";
    const auto d = s.decide(hw);
    check(near(d.cpuShare + d.gpuShare, 100.0), "prop-sum");
    check(near(d.gpuShare, 100.0 * 80 / 96), "prop-values");
    check(d.gpuUsed && d.backend == "CUDA" && d.reason == "proportional_baseline", "prop-meta");
  }
  // 4. Slow GPU -> CPU-lean by construction (never a fixed split).
  {
    CpuGpuScheduler s;
    SchedulerHardware hw;
    hw.cpuThreads = 16; hw.gpuEnabled = true;
    hw.gpuAvailable = true; hw.gpuComputeUnits = 1; hw.backendName = "CUDA";
    const auto d = s.decide(hw);
    check(d.cpuShare > 90.0 && d.gpuShare < 10.0, "slow-lean");
    check(d.gpuUsed, "slow-still-uses");
  }
  // 5. Available but zero capacity -> CPU, explicit reason.
  {
    CpuGpuScheduler s;
    SchedulerHardware hw;
    hw.cpuThreads = 4; hw.gpuEnabled = true;
    hw.gpuAvailable = true; hw.gpuComputeUnits = 0; hw.backendName = "CUDA";
    const auto d = s.decide(hw);
    check(near(d.cpuShare, 100.0) && !d.gpuUsed && d.reason == "no_gpu_capacity", "zero-cap");
  }
  // 6. Re-evaluation cadence: stable inputs re-evaluate without adjusting;
  // changed inputs adjust exactly once.
  {
    CpuGpuScheduler s;
    s.setReevalIntervalMs(100);
    SchedulerHardware hw;
    hw.cpuThreads = 8; hw.gpuEnabled = true;
    hw.gpuAvailable = true; hw.gpuComputeUnits = 40; hw.backendName = "CUDA";
    s.decide(hw);
    // First maybeReevaluate arms the cadence clock (decide carries no tick).
    check(s.maybeReevaluate(hw, 0), "cadence-arm");
    check(!s.maybeReevaluate(hw, 50), "cadence-hold");
    check(s.maybeReevaluate(hw, 200), "cadence-fire");
    check(s.evaluations() == 3 && s.adjustments() == 0, "stable-no-adjust");
    hw.gpuEnabled = false;
    check(s.maybeReevaluate(hw, 500), "change-reeval");
    check(s.adjustments() == 1 && !s.lastDecision().gpuUsed, "change-adjust");
    s.reset();
    check(!s.hasDecision() && s.evaluations() == 0, "reset");
  }
  // 7. Telemetry recording: measured state with the decided shares.
  {
    CpuGpuScheduler s;
    SchedulerHardware hw;
    hw.cpuThreads = 8; hw.gpuEnabled = true;
    hw.gpuAvailable = true; hw.gpuComputeUnits = 24; hw.backendName = "CUDA";
    const auto d = s.decide(hw);
    msf::BenchmarkRecorder rec;
    msf::BenchmarkConfig cfg;
    cfg.root = "C:/media"; cfg.build = "t"; cfg.engine = "t"; cfg.db = "t";
    rec.start(cfg);
    auto& st = rec.scheduler();
    st.markMeasured();
    st.initialCpuCapacity = (double)hw.cpuThreads;
    st.initialGpuCapacity = hw.gpuComputeUnits;
    st.currentCpuCapacity = (double)hw.cpuThreads;
    st.currentGpuCapacity = hw.gpuComputeUnits;
    st.cpuWorkShare = d.cpuShare;
    st.gpuWorkShare = d.gpuShare;
    st.selectedBackend = d.backend;
    rec.finalize(true, 0, 0, 0, 0, 0, 0, 0.0, 0, 0);
    const std::string js = rec.toJson();
    const bool ok = js.find("\"scheduler\":{\"state\":\"measured\"") != std::string::npos &&
                    js.find("\"selectedBackend\":\"CUDA\"") != std::string::npos &&
                    js.find("\"gpuWorkShare\":75.000") != std::string::npos;
    check(ok, "telemetry-json");
  }
  if (failures) { std::cerr << "scheduler failures=" << failures << "\n"; return 1; }
  std::cout << "scheduler=ok\n";
  return 0;
}
