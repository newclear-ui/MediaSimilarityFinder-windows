// Corpus-scale CPU/GPU benchmark for video pHash + 48x48 MSSIM paths.
// Synthetic but size-representative: thousands of pairs, as in a large scan.
// PASS contract: correctness within tolerance on GPU; timing always reported.
// On CPU-only builds the GPU sections are skipped and CPU timing is reported.
#include "fingerprint.h"
#include "gpu_backend.h"
#include "video_fingerprint.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
double nowMs() {
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
}  // namespace

int main() {
  constexpr int kT = msf::VideoFingerprint::kThumbSize;
  constexpr std::size_t kPx = static_cast<std::size_t>(kT) * kT;
  msf::GpuBackend gpu;
  const bool available = gpu.available();

  // ---- 1. 32x32 pHash batch: 1024 frames ----
  constexpr std::size_t kHashN = 1024;
  std::vector<std::uint8_t> gray32(kHashN * 1024);
  for (std::size_t i = 0; i < gray32.size(); ++i)
    gray32[i] = static_cast<std::uint8_t>((i * 37 + (i / 1024) * 131) & 0xFF);
  std::vector<std::uint64_t> cpuHashes(kHashN, 0);
  const double tHashCpu0 = nowMs();
  for (std::size_t i = 0; i < kHashN; ++i) {
    std::vector<std::uint8_t> px(gray32.begin() + i * 1024,
                                 gray32.begin() + (i + 1) * 1024);
    cpuHashes[i] = msf::perceptual_hash_pair(px, 32, 32).normal;
  }
  const double hashCpuMs = nowMs() - tHashCpu0;
  double hashGpuMs = 0;
  bool hashOk = true;
  if (available) {
    std::vector<std::uint64_t> gpuHashes(kHashN, 0);
    const double t0 = nowMs();
    const bool ok = gpu.hashBatch(gray32.data(), kHashN, gpuHashes.data());
    hashGpuMs = nowMs() - t0;
    if (!ok) return 1;
    std::size_t mism = 0;
    for (std::size_t i = 0; i < kHashN; ++i)
      if (gpuHashes[i] != cpuHashes[i]) ++mism;
    // Exact-match contract already covered by cuda_backend_test; allow a
    // small tolerance here so driver rounding cannot flake the benchmark.
    if (mism > kHashN / 100) {
      std::cerr << "phash_mismatch mism=" << mism << "\n";
      return 2;
    }
    hashOk = mism == 0;
  }

  // ---- 2. 48x48 MSSIM batch: 2048 pairs ----
  constexpr std::size_t kSsimN = 2048;
  std::vector<std::uint8_t> ssimA(kSsimN * kPx), ssimB(kSsimN * kPx);
  for (std::size_t i = 0; i < ssimA.size(); ++i) {
    ssimA[i] = static_cast<std::uint8_t>((i * 7 + (i / kPx) * 13) & 0xFF);
    ssimB[i] = static_cast<std::uint8_t>(
        (ssimA[i] + ((i % 11 == 0) ? 23 : 0)) & 0xFF);
  }
  std::vector<double> cpuScores(kSsimN, 0);
  const double tSsimCpu0 = nowMs();
  for (std::size_t i = 0; i < kSsimN; ++i)
    cpuScores[i] = msf::frame_ssim(&ssimA[i * kPx], &ssimB[i * kPx], kT, kT);
  const double ssimCpuMs = nowMs() - tSsimCpu0;
  double ssimGpuMs = 0;
  double maxDiff = 0;
  if (available) {
    std::vector<double> gpuScores(kSsimN, 0);
    const double t0 = nowMs();
    const bool ok =
        gpu.ssimBatch(ssimA.data(), ssimB.data(), kSsimN, gpuScores.data());
    ssimGpuMs = nowMs() - t0;
    if (!ok) return 3;
    for (std::size_t i = 0; i < kSsimN; ++i)
      maxDiff = std::max(maxDiff, std::abs(gpuScores[i] - cpuScores[i]));
    if (maxDiff > 1e-6) {
      std::cerr << "ssim_maxdiff=" << maxDiff << "\n";
      return 4;
    }
  }

  // ---- 3. DTW video_similarity with GPU row-batching (32x32 frames) ----
  msf::VideoFingerprint fa, fb;
  constexpr int kFrames = 32;
  std::vector<std::uint8_t> thumb(kPx);
  for (int y = 0; y < kT; ++y)
    for (int x = 0; x < kT; ++x)
      thumb[static_cast<std::size_t>(y) * kT + x] =
          static_cast<std::uint8_t>((x * 7 + y * 13) & 0xFF);
  for (int i = 0; i < kFrames; ++i) {
    fa.timestamps.push_back(i * 0.5);
    fb.timestamps.push_back(i * 0.5);
    const std::uint64_t h = 0x1111111111111111ULL + static_cast<std::uint64_t>(i);
    fa.hashes.push_back(h);
    fb.hashes.push_back(h);
    fa.mirrorHashes.push_back(0);
    fb.mirrorHashes.push_back(0);
    fa.thumb48.insert(fa.thumb48.end(), thumb.begin(), thumb.end());
    fb.thumb48.insert(fb.thumb48.end(), thumb.begin(), thumb.end());
  }
  msf::VideoSimilarityOptions cpuOpt;
  cpuOpt.thresholdPercent = 50.0;
  const double tDtwCpu0 = nowMs();
  const double cpuScore = msf::video_similarity(fa, fb, cpuOpt);
  const double dtwCpuMs = nowMs() - tDtwCpu0;
  double dtwGpuMs = 0, gpuScore = cpuScore;
  msf::VideoSimilarityStats gst;
  if (available) {
    auto go = cpuOpt;
    go.gpu = &gpu;
    go.stats = &gst;
    const double t0 = nowMs();
    gpuScore = msf::video_similarity(fa, fb, go);
    dtwGpuMs = nowMs() - t0;
    if (std::abs(cpuScore - gpuScore) > 0.2) {
      std::cerr << "dtw_mismatch cpu=" << cpuScore << " gpu=" << gpuScore
                << "\n";
      return 5;
    }
    if (!gst.gpuUsed || gst.gpuPairs == 0) return 6;
  }

  const double hashSpeedup =
      (available && hashGpuMs > 0) ? hashCpuMs / hashGpuMs : 0;
  const double ssimSpeedup =
      (available && ssimGpuMs > 0) ? ssimCpuMs / ssimGpuMs : 0;
  std::cout << "video_gpu_benchmark=ok gpu=" << (available ? "yes" : "no")
            << " hashN=" << kHashN << " hashCpuMs=" << hashCpuMs
            << " hashGpuMs=" << hashGpuMs << " hashExact=" << (hashOk ? 1 : 0)
            << " ssimN=" << kSsimN << " ssimCpuMs=" << ssimCpuMs
            << " ssimGpuMs=" << ssimGpuMs << " ssimMaxDiff=" << maxDiff
            << " dtwFrames=" << kFrames << " dtwCpuMs=" << dtwCpuMs
            << " dtwGpuMs=" << dtwGpuMs << " dtwGpuPairs=" << gst.gpuPairs
            << " hashSpeedup=" << hashSpeedup
            << " ssimSpeedup=" << ssimSpeedup << "\n";
  return 0;
}
