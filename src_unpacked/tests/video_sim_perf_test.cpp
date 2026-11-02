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
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

  // 0.9.4.164 (P2): sub-threshold batches never reach the GPU.
  // Hand-built 2x2 identical-hash pair: every cell gates (sim=100), so each
  // row offers 2 pairs (<8) to the CPU fallback. A GpuBackend object is
  // passed (pointer non-null) but ssimBatch is never reached, so this is
  // deterministic on both trees with and without CUDA hardware.
  {
    msf::VideoFingerprint sa, sb;
    sa.timestamps = {0, 1}; sb.timestamps = {0, 1};
    sa.hashes = {0x0101010101010101ULL, 0x0202020202020202ULL};
    sb.hashes = sa.hashes;
    sa.mirrorHashes = {0, 0}; sb.mirrorHashes = {0, 0};
    sa.thumb48.assign(2 * kPx, 0x7F); sb.thumb48.assign(2 * kPx, 0x7F);
    msf::GpuBackend gsmall;
    msf::VideoSimilarityOptions so;
    so.thresholdPercent = 50.0;
    so.gpu = &gsmall;
    msf::VideoSimilarityStats sst;
    so.stats = &sst;
    const double s = msf::video_similarity(sa, sb, so);
    if (!(s > 99.0)) { std::cerr << "small-batch similarity wrong\n"; return 5; }
    if (sst.gpuSubmitted != 4) { std::cerr << "small-batch submitted wrong\n"; return 5; }
    if (sst.gpuCompleted != 0) { std::cerr << "small-batch completed nonzero\n"; return 5; }
    if (sst.smallBatchPairs != 4) { std::cerr << "small-batch count wrong\n"; return 5; }
    if (sst.failedBatchPairs != 0) { std::cerr << "unexpected batch failure\n"; return 5; }
    if (!(sst.cpuMs > 0.0)) { std::cerr << "small-batch cpu time missing\n"; return 5; }
    if (sst.gpuMs != 0.0) { std::cerr << "gpu time without gpu call\n"; return 5; }
    std::cout << "  [ok] small-batch accounting submitted=4 completed=0\n";
  }

  // 0.9.4.167 (V4): coverage accounting + strict gate (diagnostic).
  // Long side 12 frames, only the first 2 gate at threshold 60:
  // covShort=1, covLong=2/12, segs=1. Non-strict scores the DTW;
  // MSF_VIDEO_STRICT_COVERAGE=1 rejects (untuned placeholder gate).
  {
    msf::VideoFingerprint la, lb;
    const std::uint64_t H = 0x0101010101010101ULL;
    const std::uint64_t Z = ~H;
    for (int j = 0; j < 12; ++j) {
      la.timestamps.push_back((double)j);
      la.hashes.push_back(j < 2 ? H : Z);
      la.mirrorHashes.push_back(0);
    }
    for (int i = 0; i < 2; ++i) {
      lb.timestamps.push_back((double)i);
      lb.hashes.push_back(H);
      lb.mirrorHashes.push_back(0);
    }
    msf::VideoSimilarityOptions so;
    so.thresholdPercent = 60.0;
    msf::VideoSimilarityStats st;
    so.stats = &st;
    const double loose = msf::video_similarity(la, lb, so);
    if (!(loose > 50.0)) { std::cerr << "partial pair scores too low\n"; return 6; }
    if (st.covShort != 1.0) { std::cerr << "short coverage wrong\n"; return 6; }
    if (std::abs(st.covLong - 2.0 / 12.0) > 1e-9) { std::cerr << "long coverage wrong\n"; return 6; }
    if (st.covSegments != 1) { std::cerr << "segments wrong\n"; return 6; }
    if (st.scoredPairs != 1) { std::cerr << "scored count wrong\n"; return 6; }
    std::cout << "  [ok] coverage short=1 long=" << st.covLong << " segs=1\n";
#if defined(_WIN32)
    _putenv_s("MSF_VIDEO_STRICT_COVERAGE", "1");
#else
    setenv("MSF_VIDEO_STRICT_COVERAGE", "1", 1);
#endif
    msf::VideoSimilarityStats st2;
    auto go2 = so; go2.stats = &st2;
    const double strict = msf::video_similarity(la, lb, go2);
#if defined(_WIN32)
    _putenv_s("MSF_VIDEO_STRICT_COVERAGE", "");
#else
    unsetenv("MSF_VIDEO_STRICT_COVERAGE");
#endif
    if (strict != 0.0) { std::cerr << "strict gate did not reject\n"; return 7; }
    std::cout << "  [ok] strict gate rejects partial coverage\n";
  }

  // 0.9.4.169 (V4): coverage is decided after the SSIM blend. Hash-identical
  // frames whose thumbs are unrelated (random hash collisions) count in
  // rawCov* but must not count in covShort/covLong/covSegments, and must
  // match the DTW score (0). Flat-vs-noise thumbs give MSSIM ~0.01, so the
  // blend 0.4*100+0.6*~1 stays far below the 50 threshold.
  {
    constexpr std::size_t kPxC = (std::size_t)msf::VideoFingerprint::kThumbSize * msf::VideoFingerprint::kThumbSize;
    const std::uint64_t H = 0x0101010101010101ULL;
    msf::VideoFingerprint ca, cb;
    for (int i = 0; i < 4; ++i) {
      ca.timestamps.push_back((double)i); cb.timestamps.push_back((double)i);
      ca.hashes.push_back(H); cb.hashes.push_back(H);
      ca.mirrorHashes.push_back(0); cb.mirrorHashes.push_back(0);
    }
    ca.thumb48.assign(4 * kPxC, 0x7F);
    std::uint32_t lcg = 12345u;
    cb.thumb48.resize(4 * kPxC);
    for (auto& v : cb.thumb48) { lcg = lcg * 1664525u + 1013904223u; v = (std::uint8_t)(1 + (lcg >> 24) % 254); }
    msf::VideoSimilarityOptions co;
    co.thresholdPercent = 50.0;
    msf::VideoSimilarityStats cst;
    co.stats = &cst;
    const double sc = msf::video_similarity(ca, cb, co);
    if (cst.rawCovShort != 1.0 || cst.rawCovLong != 1.0) { std::cerr << "raw coverage should be saturated\n"; return 8; }
    if (cst.covShort != 0.0 || cst.covLong != 0.0 || cst.covSegments != 0) { std::cerr << "verified coverage counted SSIM-rejected cells\n"; return 8; }
    if (sc != 0.0) { std::cerr << "SSIM-rejected pair should score 0\n"; return 8; }
    // coverageGate may raise the coverage bar but never lowers it below the
    // pair threshold, and never changes the score.
    msf::VideoSimilarityOptions lo = co; lo.coverageGate = 10.0; msf::VideoSimilarityStats lst; lo.stats = &lst;
    if (msf::video_similarity(ca, cb, lo) != sc || lst.covShort != 0.0) { std::cerr << "coverageGate lowered below threshold\n"; return 8; }
    std::cout << "  [ok] post-SSIM coverage raw=1/1 verified=0/0 score=0\n";
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
