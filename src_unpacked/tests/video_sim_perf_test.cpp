// 0.9.4.159 P2: video_similarity() workspace/perf probe.
//
// Deterministic workload (fixed LCG hashes + gradient thumbs, one all-zero
// thumb to exercise the xHas gate) at realistic kept sizes (12x14).
// - Pins determinism: two CPU runs must be bit-identical.
// - If a GPU is present, pins CPU-vs-GPU agreement (existing 0.2 convention).
// - Reports wall time for a fixed iteration count so before/after builds can
//   be compared on the same machine (run this binary on each build).
#include "video_fingerprint.h"
#include "gpu_backend.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <vector>

namespace {
std::uint64_t lcg(std::uint64_t& s) {
  s = s * 6364136223846793005ULL + 1442695040888963407ULL;
  return s ^ (s >> 29);
}
}  // namespace

int main() {
  constexpr int kT = msf::VideoFingerprint::kThumbSize;
  constexpr std::size_t kPx = (std::size_t)kT * kT;
  constexpr std::size_t N = 12, M = 14;
  msf::VideoFingerprint fa, fb;
  fa.timestamps.resize(N); fb.timestamps.resize(M);
  std::uint64_t s = 0x12345678ULL;
  for (std::size_t i = 0; i < N; ++i) {
    fa.timestamps[i] = (double)i * 2.0;
    fa.hashes.push_back(lcg(s));
    fa.mirrorHashes.push_back(lcg(s));
    for (std::size_t k = 0; k < kPx; ++k)
      fa.thumb48.push_back((std::uint8_t)((i * 37 + k * 11) & 0xFF));
  }
  for (std::size_t j = 0; j < M; ++j) {
    fb.timestamps[j] = (double)j * 2.0 + 0.5;
    // Every third frame reuses fa's hash so Hamming gates fire and the
    // MSSIM pack path is exercised; the rest are noise.
    fb.hashes.push_back(j % 3 == 0 ? fa.hashes[j % N] : lcg(s));
    fb.mirrorHashes.push_back(0);
    for (std::size_t k = 0; k < kPx; ++k)
      fb.thumb48.push_back((std::uint8_t)((j * 53 + k * 7) & 0xFF));
  }
  // One all-zero thumb block exercises the xHas=0 Hamming-only gate.
  for (std::size_t k = 0; k < kPx; ++k) fa.thumb48[k] = 0;

  msf::VideoSimilarityOptions o;
  o.thresholdPercent = 30.0;
  const double cpu1 = msf::video_similarity(fa, fb, o);
  const double cpu2 = msf::video_similarity(fa, fb, o);
  if (cpu1 != cpu2) { std::cerr << "nondeterministic CPU score\n"; return 1; }
  if (!(cpu1 > 0.0 && cpu1 <= 100.0)) { std::cerr << "insane CPU score\n"; return 2; }
  std::cout << "  [ok] deterministic cpuScore=" << cpu1 << "\n";

  msf::GpuBackend gpu;
  const bool available = gpu.available();
  double gpuScore = -1;
  if (available) {
    msf::VideoSimilarityStats st;
    auto go = o; go.gpu = &gpu; go.stats = &st;
    gpuScore = msf::video_similarity(fa, fb, go);
    const double gpuScore2 = msf::video_similarity(fa, fb, go);
    if (gpuScore != gpuScore2) { std::cerr << "nondeterministic GPU score\n"; return 3; }
    if (std::abs(cpu1 - gpuScore) > 0.2) {
      std::cerr << "cpu/gpu mismatch\n"; return 4;
    }
    std::cout << "  [ok] gpu agrees submitted=" << st.gpuSubmitted
              << " completed=" << st.gpuCompleted << "\n";
    // 0.9.4.162 (P1): the all-GPU workload records no CPU fallback time.
    if (st.cpuMs != 0.0) { std::cerr << "unexpected cpu fallback on gpu path\n"; return 5; }
  }

  // 0.9.4.162 (P1): CPU-fallback MSSIM time is measured, not zero.
  {
    msf::VideoSimilarityStats st;
    auto co = o; co.gpu = nullptr; co.stats = &st;
    (void)msf::video_similarity(fa, fb, co);
    if (!(st.cpuMs > 0.0)) { std::cerr << "cpu fallback time not measured\n"; return 5; }
    std::cout << "  [ok] cpuMs measured=" << st.cpuMs << "\n";
  }

  // Timed workload: same pair, fixed iteration count.
  constexpr int kIters = 400;
  {
    msf::VideoSimilarityOptions to = o;
    msf::GpuBackend g2;
    const bool av = g2.available();
    msf::VideoSimilarityStats st;
    if (av) { to.gpu = &g2; to.stats = &st; }
    // Warmup (allocators, GPU init) outside the measurement.
    (void)msf::video_similarity(fa, fb, to);
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIters; ++i) (void)msf::video_similarity(fa, fb, to);
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t0).count();
    std::printf("video_sim_perf_ms=%.2f iters=%d pairs=%dx%d gpu=%s\n", ms,
                kIters, (int)N, (int)M, av ? "yes" : "no");
  }
  std::cout << "video_sim_perf=ok\n";
  return 0;
}
