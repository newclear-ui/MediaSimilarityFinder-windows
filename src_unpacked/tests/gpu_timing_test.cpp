#include "benchmark.h"
#include "gpu_backend.h"
#include "fingerprint.h"
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

// D4a regression. Two halves:
//
//  1. Recorder contract (runs everywhere, no CUDA required). Proves the new
//     backend-internal keys exist, that feeding a complete measurement turns
//     every state into `measured`, and that a recorder which never receives
//     one keeps every state at `not_measured` instead of 0. That last part is
//     the D4a rule: a backend that cannot time anything must not look like a
//     backend that measured zero.
//
//  2. Backend behavior (capability-aware). On a real CUDA device the backend
//     must report measured timing for a real hash call and still produce the
//     same hashes as the CPU reference. Without CUDA the assertions are that
//     the call fails and timing stays unmeasured.
//
// Deliberately NOT asserted: that host total equals the sum of the device
// deltas. It does not, and pretending otherwise would bake a wrong invariant
// into the test suite. Only non-negativity and one loose upper bound are
// checked, per the D4a brief.
namespace {
int checks = 0;
bool ok(bool cond, const char* what) {
  ++checks;
  if (!cond) { std::cerr << "FAIL: " << what << "\n"; return false; }
  return true;
}
}  // namespace

static int recorderUnmeasured() {
  msf::TelemetryRecorder rec;
  msf::TelemetryConfig cfg;
  cfg.root = "C:/media";
  cfg.build = "0.9.4.20"; cfg.engine = "1.5.0"; cfg.db = "1.0.3";
  cfg.scanImages = true; cfg.cudaAvailable = true;
  rec.start(cfg);
  rec.addGpuBatchMs(4.0);
  const std::string js = rec.toJson();
  if (!ok(js.find("\"gpuH2dDeviceMs\"") != std::string::npos, "gpuH2dDeviceMs key exists")) return 1;
  if (!ok(js.find("\"gpuKernelDeviceMs\"") != std::string::npos, "gpuKernelDeviceMs key exists")) return 2;
  if (!ok(js.find("\"gpuD2hDeviceMs\"") != std::string::npos, "gpuD2hDeviceMs key exists")) return 3;
  if (!ok(js.find("\"gpuSyncHostMs\"") != std::string::npos, "gpuSyncHostMs key exists")) return 4;
  if (!ok(js.find("\"gpuHostTotalMs\"") != std::string::npos, "gpuHostTotalMs key exists")) return 5;
  if (!ok(js.find("\"gpuTimedBatches\"") != std::string::npos, "gpuTimedBatches key exists")) return 6;
  if (!ok(js.find("\"gpuH2dState\":\"not_measured\"") != std::string::npos, "h2d not_measured when untimed")) return 7;
  if (!ok(js.find("\"gpuKernelState\":\"not_measured\"") != std::string::npos, "kernel not_measured when untimed")) return 8;
  if (!ok(js.find("\"gpuD2hState\":\"not_measured\"") != std::string::npos, "d2h not_measured when untimed")) return 9;
  if (!ok(js.find("\"gpuSyncState\":\"not_measured\"") != std::string::npos, "sync not_measured when untimed")) return 10;
  if (!ok(js.find("\"gpuHostTotalState\":\"not_measured\"") != std::string::npos, "hostTotal not_measured when untimed")) return 11;
  if (!ok(js.find("\"gpuTimedBatches\":0") != std::string::npos, "zero timed batches when untimed")) return 12;
  // D8a bumped the schema for the dataset section; the D4a keys are additive,
  // so this tracks the current schema rather than pinning D4a's own number.
  if (!ok(js.find("\"schemaVersion\":" +
                  std::to_string(msf::TelemetryRecorder::kBenchmarkSchemaVersion)) !=
              std::string::npos,
          "schemaVersion matches the recorder constant")) return 13;
  // A batch counted as GPU work but never timed must not flip a state.
  rec.addImageGpuDeviceTiming(0.0, 0.0, 0.0, 0.0, 0.0, /*usedGpu=*/false);
  const std::string js2 = rec.toJson();
  if (!ok(js2.find("\"gpuH2dState\":\"not_measured\"") != std::string::npos,
          "usedGpu=false keeps timing unmeasured")) return 14;
  return 0;
}

static int recorderMeasured() {
  msf::TelemetryRecorder rec;
  msf::TelemetryConfig cfg;
  cfg.root = "C:/media";
  cfg.build = "0.9.4.20"; cfg.engine = "1.5.0"; cfg.db = "1.0.3";
  cfg.scanImages = true; cfg.cudaAvailable = true;
  rec.start(cfg);
  rec.addImageGpuDeviceTiming(0.30, 1.20, 0.10, 0.80, 2.60, true);
  rec.addImageGpuDeviceTiming(0.20, 1.00, 0.15, 0.70, 2.10, true);
  const std::string js = rec.toJson();
  if (!ok(js.find("\"gpuH2dState\":\"measured\"") != std::string::npos, "h2d measured")) return 20;
  if (!ok(js.find("\"gpuKernelState\":\"measured\"") != std::string::npos, "kernel measured")) return 21;
  if (!ok(js.find("\"gpuD2hState\":\"measured\"") != std::string::npos, "d2h measured")) return 22;
  if (!ok(js.find("\"gpuSyncState\":\"measured\"") != std::string::npos, "sync measured")) return 23;
  if (!ok(js.find("\"gpuHostTotalState\":\"measured\"") != std::string::npos, "hostTotal measured")) return 24;
  if (!ok(js.find("\"gpuTimedBatches\":2") != std::string::npos, "two timed batches")) return 25;
  // Cumulative sums, printed with the recorder's fixed precision.
  if (!ok(js.find("\"gpuH2dDeviceMs\":0.500") != std::string::npos, "h2d accumulates")) return 26;
  if (!ok(js.find("\"gpuKernelDeviceMs\":2.200") != std::string::npos, "kernel accumulates")) return 27;
  if (!ok(js.find("\"gpuD2hDeviceMs\":0.250") != std::string::npos, "d2h accumulates")) return 28;
  if (!ok(js.find("\"gpuSyncHostMs\":1.500") != std::string::npos, "sync accumulates")) return 29;
  if (!ok(js.find("\"gpuHostTotalMs\":4.700") != std::string::npos, "hostTotal accumulates")) return 30;
  return 0;
}

static int ham64(std::uint64_t a, std::uint64_t b) {
  std::uint64_t x = a ^ b; int n = 0; while (x) { x &= x - 1; ++n; } return n;
}

// Measures one batch of `count` images and reports the internal split.
// Device time and host total are deliberately reported side by side: the gap
// between them is exactly the host-side portion the D4b decision turns on.
static bool probe(msf::GpuBackend& gpu, std::size_t count, msf::GpuBackend::HashTiming& t) {
  std::vector<std::uint8_t> px(count * 1024);
  for (std::size_t i = 0; i < count; ++i)
    for (std::size_t p = 0; p < 1024; ++p)
      px[i * 1024 + p] = static_cast<std::uint8_t>((p * 31 + i * 7) % 256);
  std::vector<std::uint64_t> out(count, 0);
  if (!gpu.hashBatch(px.data(), count, out.data(), &t)) return false;
  return true;
}

static int backendBehavior() {
  msf::GpuBackend gpu;
  const auto info = gpu.detect();
  const std::size_t count = 16;
  std::vector<std::uint8_t> px(count * 1024);
  for (std::size_t i = 0; i < count; ++i)
    for (std::size_t p = 0; p < 1024; ++p)
      px[i * 1024 + p] = static_cast<std::uint8_t>((p * 31 + i * 7) % 256);
  std::vector<std::uint64_t> out(count, 0);

  msf::GpuBackend::HashTiming timing;
  const bool hashed = gpu.hashBatch(px.data(), count, out.data(), &timing);

#ifdef MSF_HAS_CUDA
  if (!info.available) {
    // Capability-aware: a GPU build without a device must behave like CPU.
    if (!ok(!hashed, "no device -> hashBatch fails")) return 40;
    if (!ok(!timing.measured, "no device -> timing unmeasured")) return 41;
    std::cout << "gpu_timing=skip_no_device checks=" << checks << "\n";
    return 0;
  }
  if (!ok(hashed, "device present -> hashBatch succeeds")) return 42;
  if (!ok(timing.measured, "device present -> timing measured")) return 43;
  if (!ok(timing.h2dDeviceMs >= 0.0, "h2d >= 0")) return 44;
  if (!ok(timing.kernelDeviceMs >= 0.0, "kernel >= 0")) return 45;
  if (!ok(timing.d2hDeviceMs >= 0.0, "d2h >= 0")) return 46;
  if (!ok(timing.syncHostMs >= 0.0, "sync host >= 0")) return 47;
  if (!ok(timing.hostTotalMs >= 0.0, "host total >= 0")) return 48;
  // Loose bound only: device deltas cannot exceed the host wall time by more
  // than a wide margin. No exact-equality claim.
  const double deviceSum = timing.h2dDeviceMs + timing.kernelDeviceMs + timing.d2hDeviceMs;
  if (!ok(deviceSum <= timing.hostTotalMs * 4.0 + 5.0, "device sum within loose host bound")) return 49;
  // Regression guard: instrumentation must not alter the hash result.
  for (std::size_t i = 0; i < count; ++i) {
    std::vector<std::uint8_t> one(px.begin() + i * 1024, px.begin() + (i + 1) * 1024);
    const auto cpu = msf::perceptual_hash(one, 32, 32);
    if (!ok(ham64(cpu, out[i]) <= 8, "hash still matches CPU reference")) return 50;
  }
  // A second, larger batch keeps the D4b decision input in the test log: the
  // host-vs-device gap is batch-size dependent, so one size cannot decide it.
  msf::GpuBackend::HashTiming big;
  bool bigOk = false;
  for (int attempt = 0; attempt < 3 && !bigOk; ++attempt) bigOk = probe(gpu, 256, big);
  if (!ok(bigOk && big.measured, "larger batch also measured")) return 51;
  std::cout << "gpu_timing=ok device name=" << info.name
            << " small16 h2d=" << timing.h2dDeviceMs
            << " kernel=" << timing.kernelDeviceMs
            << " d2h=" << timing.d2hDeviceMs
            << " syncHost=" << timing.syncHostMs
            << " hostTotal=" << timing.hostTotalMs
            << " | batch256 h2d=" << big.h2dDeviceMs
            << " kernel=" << big.kernelDeviceMs
            << " d2h=" << big.d2hDeviceMs
            << " syncHost=" << big.syncHostMs
            << " hostTotal=" << big.hostTotalMs
            << " checks=" << checks << "\n";
  return 0;
#else
  (void)timing; (void)px; (void)out; (void)info; (void)count;
  if (!ok(!hashed, "CPU build -> hashBatch fails")) return 40;
  if (!ok(!timing.measured, "CPU build -> timing unmeasured")) return 41;
  std::cout << "gpu_timing=ok cpu_only checks=" << checks << "\n";
  return 0;
#endif
}

int main() {
  if (int rc = recorderUnmeasured()) return rc;
  if (int rc = recorderMeasured()) return rc;
  if (int rc = backendBehavior()) return rc;
  std::cout << "gpu_timing=all_ok checks=" << checks << "\n";
  return 0;
}
