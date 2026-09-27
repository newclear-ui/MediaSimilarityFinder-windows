#include "media_search_engine.h"
#include "benchmark.h"
#include "dataset_fingerprint.h"
#include "path_utils.h"
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

// D8b review probe: run the standard dataset repeatedly on one machine and
// report the distribution, not a single number.
//
// Purpose is decision support, not optimization. D4b (transfer/compute
// overlap) and Full D3 (queue topology) are both blocked on "what actually
// dominates wall time on a representative dataset". This prints the fields
// that answer that question, plus repetition/variation so a single run
// cannot be mistaken for a verdict.
//
// It prints selected fields rather than the whole JSON so a human can read
// the run without a JSON parser. No claim is made here; the numbers feed the
// pre-register of whichever step is chosen next.

namespace {
double numAfter(const std::string& js, const std::string& key, bool& found) {
  const std::string pat = "\"" + key + "\":";
  const std::size_t p = js.find(pat);
  found = p != std::string::npos;
  if (!found) return 0.0;
  return std::strtod(js.c_str() + p + pat.size(), nullptr);
}
// Reads a key only inside the named nested object, because a bare search can
// hit an identically named key elsewhere in the document.
double numIn(const std::string& js, const std::string& obj, const std::string& key) {
  const std::size_t o = js.find("\"" + obj + "\":{");
  if (o == std::string::npos) return 0.0;
  const std::size_t end = js.find('}', o);
  if (end == std::string::npos) return 0.0;
  bool ok = false;
  return numAfter(js.substr(o, end - o), key, ok);
}
std::string strAfter(const std::string& js, const std::string& key) {
  const std::string pat = "\"" + key + "\":\"";
  const std::size_t p = js.find(pat);
  if (p == std::string::npos) return std::string();
  const std::size_t s = p + pat.size();
  const std::size_t e = js.find('"', s);
  return e == std::string::npos ? std::string() : js.substr(s, e - s);
}
double meanOf(std::vector<double> v) {
  if (v.empty()) return 0.0;
  double s = 0.0; for (double x : v) s += x;
  return s / (double)v.size();
}
double maxOf(const std::vector<double>& v) {
  double m = 0.0; for (double x : v) m = x > m ? x : m; return m;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: msf_dataset_baseline <root> <app-dir> [runs]\n";
    return 2;
  }
  const std::string root = argv[1];
  const std::string app = argv[2];
  const int runs = argc >= 4 ? std::atoi(argv[3]) : 3;

  const msf::DatasetFingerprint fp = msf::computeDatasetFingerprint(root);
  if (fp.state != "measured") {
    std::cerr << "dataset not measured: " << fp.state << "\n";
    return 3;
  }
  std::printf("dataset_fingerprint=%s files=%llu bytes=%llu version=%d\n", fp.fingerprint.c_str(),
              (unsigned long long)fp.fileCount, (unsigned long long)fp.totalBytes,
              msf::kDatasetFingerprintVersion);

  std::vector<double> wall, h2d, kernel, d2h, syncHost, hostTotal, batches;
  std::vector<double> walkQueued, walkDequeued, walkMaxDepth, blocked, starved, imgStage, gpuBatchMs;
  std::string lastJson;
  for (int r = 0; r < runs; ++r) {
    // A distinct app dir per run keeps every index cold. Reusing one would
    // make run 2+ skip analysis (analyzed=0, gpu_timing not_measured), which
    // would silently turn the measurement into a no-op.
    const std::string runApp = app + "_run" + std::to_string(r);
    std::error_code ec;
    std::filesystem::remove_all(msf::path_from_utf8(runApp), ec);
    std::filesystem::create_directories(msf::path_from_utf8(runApp), ec);

    msf::MediaSearchEngine engine;
    if (!engine.openIndexForRoot(root, runApp)) { std::cerr << "openIndexForRoot failed\n"; return 4; }
    msf::ScanControl control;
    control.buildVersion = "0.9.4.21";
    const auto t0 = std::chrono::steady_clock::now();
    const msf::SearchReport rep = engine.scan(root, 8, &control);
    const double wallMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const std::string js = engine.benchmarkJson();
    lastJson = js;
    std::filesystem::remove_all(msf::path_from_utf8(runApp), ec);

    bool ok = false;
    wall.push_back(wallMs);
    h2d.push_back(numAfter(js, "gpuH2dDeviceMs", ok));
    kernel.push_back(numAfter(js, "gpuKernelDeviceMs", ok));
    d2h.push_back(numAfter(js, "gpuD2hDeviceMs", ok));
    syncHost.push_back(numAfter(js, "gpuSyncHostMs", ok));
    hostTotal.push_back(numAfter(js, "gpuHostTotalMs", ok));
    batches.push_back(numAfter(js, "gpuTimedBatches", ok));
    // The walker record is a nested object; read it in scope so a same-named
    // key elsewhere in the document cannot be picked up by accident.
    walkQueued.push_back(numIn(js, "walker", "queued"));
    walkDequeued.push_back(numIn(js, "walker", "dequeued"));
    walkMaxDepth.push_back(numIn(js, "walker", "maxDepth"));
    blocked.push_back(numIn(js, "walker", "blockedTicks"));
    starved.push_back(numIn(js, "walker", "starvedTicks"));
    imgStage.push_back(numAfter(js, "imageStageMs", ok));
    gpuBatchMs.push_back(numAfter(js, "gpuBatchMs", ok));
    std::printf("run=%d wall_ms=%.1f scanned=%zu analyzed=%zu groups=%zu gpu_images=%llu\n", r,
                wallMs, rep.scanned, rep.analyzed, rep.groups,
                (unsigned long long)rep.gpuImages);
  }

  const std::string state = strAfter(lastJson, "gpuKernelState");
  std::printf("\n--- D4b question: what dominates GPU hash wall time ---\n");
  std::printf("gpu_timing_state=%s  (not_measured -> no D4b conclusion is possible)\n", state.c_str());
  std::printf("gpu_h2d_device_ms        mean=%.4f max=%.4f\n", meanOf(h2d), maxOf(h2d));
  std::printf("gpu_kernel_device_ms     mean=%.4f max=%.4f\n", meanOf(kernel), maxOf(kernel));
  std::printf("gpu_d2h_device_ms        mean=%.4f max=%.4f\n", meanOf(d2h), maxOf(d2h));
  std::printf("gpu_sync_host_ms         mean=%.4f max=%.4f\n", meanOf(syncHost), maxOf(syncHost));
  std::printf("gpu_host_total_ms        mean=%.4f max=%.4f\n", meanOf(hostTotal), maxOf(hostTotal));
  std::printf("gpu_timed_batches        mean=%.1f\n", meanOf(batches));
  const double devSum = meanOf(h2d) + meanOf(kernel) + meanOf(d2h);
  std::printf("device_sum_ms            mean=%.4f  (host_total - device_sum = %.4f)\n", devSum,
              meanOf(hostTotal) - devSum);
  std::printf("host_side_share          mean=%.1f%%\n",
              meanOf(hostTotal) > 0 ? 100.0 * (meanOf(hostTotal) - devSum) / meanOf(hostTotal) : 0.0);

  std::printf("\n--- Full D3 question: is the walker queue imbalanced ---\n");
  std::printf("walker_queued             mean=%.1f max=%.1f\n", meanOf(walkQueued), maxOf(walkQueued));
  std::printf("walker_dequeued           mean=%.1f max=%.1f\n", meanOf(walkDequeued), maxOf(walkDequeued));
  std::printf("walker_max_depth          mean=%.1f max=%.1f (capacity 4096)\n", meanOf(walkMaxDepth),
              maxOf(walkMaxDepth));
  std::printf("walker_blocked_ticks      mean=%.1f max=%.1f\n", meanOf(blocked), maxOf(blocked));
  std::printf("walker_starved_ticks      mean=%.1f max=%.1f\n", meanOf(starved), maxOf(starved));
  // The decisive number: how much of the scan the queue could even affect.
  const double meanWall = meanOf(wall);
  std::printf("walker_max_depth_share    mean=%.4f%% of capacity\n",
              meanOf(walkMaxDepth) / 4096.0 * 100.0);
  std::printf("gpu_batch_share_of_wall   mean=%.4f%%  (ceiling for any D4b gain)\n",
              meanWall > 0 ? meanOf(gpuBatchMs) / meanWall * 100.0 : 0.0);

  std::printf("\n--- context ---\n");
  std::printf("wall_ms                 mean=%.1f max=%.1f\n", meanOf(wall), maxOf(wall));
  std::printf("image_stage_ms          mean=%.1f\n", meanOf(imgStage));
  std::printf("gpu_batch_ms            mean=%.3f\n", meanOf(gpuBatchMs));
  std::printf("runs=%d (single-run conclusions are not valid)\n", runs);

  // Where the wall time actually goes. The D stages optimize queues and
  // transfer overlap, so a decision needs the share of wall that those
  // stages even touch -- not just their own numbers.
  {
    bool ok = false;
    const double benchWall = numAfter(lastJson, "wallMs", ok);
    auto share = [&](const char* key) {
      bool found = false;
      return numAfter(lastJson, key, found);
    };
    std::printf("\n--- stage breakdown (where wall time actually goes) ---\n");
    std::printf("summary_wall_ms         mean=%.1f (engine-reported, excludes setup)\n", benchWall);
    struct Row { const char* key; const char* label; };
    const Row rows[] = {
        {"walkMs", "walk"},
        {"imageStageMs", "image stage"},
        {"videoStageMs", "video stage"},
        {"analyzeMs", "analyze"},
        {"revalidateMs", "revalidate"},
        {"incrementalMs", "incremental"},
        {"candidateIndexMs", "candidate index"},
        {"similarityMs", "similarity"},
        {"persistenceMs", "persistence"},
        {"gpuBatchMs", "  (of which gpu batch)"},
    };
    for (const Row& r : rows) {
      const double v = share(r.key);
      std::printf("%-24s mean=%10.1f ms  %6.2f%% of engine wall\n", r.label, v,
                  benchWall > 0 ? v / benchWall * 100.0 : 0.0);
    }
    // D-owned overhead: the walker queue and the GPU batch together. This is
    // the honest ceiling on what any D3/D4 change could return.
    const double dOwned = share("gpuBatchMs");
    std::printf("\nD3+D4 addressable ceiling: %.3f%% of engine wall\n",
                benchWall > 0 ? dOwned / benchWall * 100.0 : 0.0);
    std::printf("(outer host wall incl. process setup/teardown: %.1f ms)\n", meanOf(wall));
  }

  // --- D9a: analyze internal split. This is the point of Node I, so it is
  // printed unconditionally rather than hidden behind an env var. ---
  {
    const double idx = numIn(lastJson, "analyze", "indexMs");
    const double scn = numIn(lastJson, "analyze", "scanMs");
    const double vfy = numIn(lastJson, "analyze", "verifyMs");
    const double vid = numIn(lastJson, "analyze", "videoMs");
    const double calls = numIn(lastJson, "analyze", "verifyCalls");
    const double misses = numIn(lastJson, "analyze", "verifyDecodeMisses");
    const double hits = numIn(lastJson, "analyze", "verifyCacheHits");
    const double ssim = numIn(lastJson, "analyze", "ssimEvals");
    const double fssim = numIn(lastJson, "analyze", "frameSsimEvals");
    const double vpairs = numIn(lastJson, "analyze", "videoTemporalPairs");
    bool f1 = false;
    const double analyzeMs = numAfter(lastJson, "analyzeMs", f1);
    const double sub = idx + scn + vfy + vid;
    std::printf("\n--- D9a: analyze internal split ---\n");
    std::printf("  indexMs            %10.1f   state=%s\n", idx,
                strAfter(lastJson, "indexState").c_str());
    std::printf("  scanMs             %10.1f   state=%s (remainder: enumeration + Hamming + overhead)\n",
                scn, strAfter(lastJson, "scanState").c_str());
    std::printf("  verifyMs           %10.1f   state=%s\n", vfy,
                strAfter(lastJson, "verifyState").c_str());
    std::printf("  videoMs            %10.1f   state=%s\n", vid,
                strAfter(lastJson, "videoState").c_str());
    std::printf("  substage sum       %10.1f   vs analyzeMs %.1f -> %s\n", sub, analyzeMs,
                sub <= analyzeMs + 1.0 ? "OK (<=)" : "VIOLATION");
    if (analyzeMs > 0)
      std::printf("  share of analyze   index %.2f%%  scan %.2f%%  verify %.2f%%  video %.2f%%\n",
                  100.0 * idx / analyzeMs, 100.0 * scn / analyzeMs, 100.0 * vfy / analyzeMs,
                  100.0 * vid / analyzeMs);
    std::printf("verifyCalls          %.0f\n", calls);
    std::printf("verifyDecodeMisses   %.0f\n", misses);
    std::printf("verifyCacheHits      %.0f\n", hits);
    if (hits + misses > 0)
      std::printf("verifyHitRate        %.6f  (state=%s)\n", hits / (hits + misses),
                  strAfter(lastJson, "verifyHitRateState").c_str());
    else
      std::printf("verifyHitRate        (no lookups; state=%s)\n",
                  strAfter(lastJson, "verifyHitRateState").c_str());
    if (calls > 0)
      std::printf("msPerVerifyCall      %.6f ms\n", vfy / calls);
    else
      std::printf("msPerVerifyCall      (no verify calls)\n");
    std::printf("ssimEvals            %.0f\n", ssim);
    std::printf("frameSsimEvals       %.0f\n", fssim);
    std::printf("videoTemporalPairs   %.0f\n", vpairs);
    if (calls > 0) std::printf("ssimEvals/verifyCall %.2f\n", ssim / calls);
  }

  // --- D9c: where the expensive verify time actually goes. The stages are
  // disjoint code regions and "other" is the remainder of verifyMs, so the
  // list adds up to verifyMs without double counting; the sum check below
  // proves that on real data instead of asserting it in prose. Reported
  // unconditionally for the same reason as the D9a block. ---
  {
    bool f1 = false;
    const double vfyTotal = numIn(lastJson, "analyze", "verifyMs");
    const double lookups = numIn(lastJson, "analyze", "verifyBufferLookups");
    const double lookupsState = numIn(lastJson, "analyze", "verifyQuickHashReads");
    const double quickBytes = numIn(lastJson, "analyze", "verifyQuickHashBytes");
    const double decodes = numIn(lastJson, "analyze", "verifyDecodes");
    const double copies = numIn(lastJson, "analyze", "verifyCacheCopies");
    const double cropCalls = numIn(lastJson, "analyze", "verifyCropCalls");
    const double flipCalls = numIn(lastJson, "analyze", "verifyFlipCalls");
    // Expensive verifies: buffer lookups are 2 per verify, and a lookup is
    // only reached when the kFast short-circuit did not fire. Deriving the
    // count this way avoids inventing a second definition of "expensive".
    const double expensive = lookups / 2.0;

    const double keyMs   = numIn(lastJson, "analyze", "verifyKeyMs");
    const double decMs   = numIn(lastJson, "analyze", "verifyDecodeMs");
    const double stoMs   = numIn(lastJson, "analyze", "verifyCacheStoreMs");
    const double copMs   = numIn(lastJson, "analyze", "verifyCacheCopyMs");
    const double cropMs  = numIn(lastJson, "analyze", "verifyCropMs");
    const double flipMs  = numIn(lastJson, "analyze", "verifyFlipMs");
    const double fsMs    = numIn(lastJson, "analyze", "verifyFrameSsimMs");
    const double othMs   = numIn(lastJson, "analyze", "verifyOtherMs");
    const double sum     = numIn(lastJson, "analyze", "verifySumMs");
    const double over    = numIn(lastJson, "analyze", "verifyBreakdownOverMs");
    (void)numAfter(lastJson, "analyzeMs", f1);

    std::printf("\n--- D9c: expensive verify internal cost split (instrumented build) ---\n");
    if (lookups <= 0) {
      std::printf("  (no buffer lookups recorded; state=%s)\n",
                  strAfter(lastJson, "verifyBreakdownState").c_str());
    } else {
      const double per = expensive > 0 ? vfyTotal / expensive : 0.0;
      std::printf("  verifyMs total     %10.1f   expensive verifies %.0f -> %.3f ms/verify\n",
                  vfyTotal, expensive, per);
      std::printf("  %-18s %10.1f  %6.2f%%   %8.4f ms/verify\n", "key (stat+64KB hash)",
                  keyMs, 100.0 * keyMs / vfyTotal, keyMs / expensive);
      std::printf("  %-18s %10.1f  %6.2f%%   %8.4f ms/verify\n", "decode",
                  decMs, 100.0 * decMs / vfyTotal, decMs / expensive);
      std::printf("  %-18s %10.1f  %6.2f%%   %8.4f ms/verify\n", "cache store",
                  stoMs, 100.0 * stoMs / vfyTotal, stoMs / expensive);
      std::printf("  %-18s %10.1f  %6.2f%%   %8.4f ms/verify\n", "cache copy (hits)",
                  copMs, 100.0 * copMs / vfyTotal, copMs / expensive);
      std::printf("  %-18s %10.1f  %6.2f%%   %8.4f ms/verify  (crop+resize together)\n", "crop/aspect",
                  cropMs, 100.0 * cropMs / vfyTotal, cropMs / expensive);
      std::printf("  %-18s %10.1f  %6.2f%%   %8.4f ms/verify\n", "mirror flip",
                  flipMs, 100.0 * flipMs / vfyTotal, flipMs / expensive);
      std::printf("  %-18s %10.1f  %6.2f%%   %8.4f ms/verify\n", "frame_ssim",
                  fsMs, 100.0 * fsMs / vfyTotal, fsMs / expensive);
      std::printf("  %-18s %10.1f  %6.2f%%   %8.4f ms/verify  (remainder)\n", "other",
                  othMs, 100.0 * othMs / vfyTotal, othMs / expensive);
      std::printf("  sum                %10.1f   vs verifyMs %.1f -> %s (over %.4f ms)\n",
                  sum, vfyTotal, (sum <= vfyTotal + 1.0) ? "OK (<=)" : "VIOLATION", over);
      std::printf("  frame_ssim internals are NOT measured (single function, no separable sub-stage)\n");
      std::printf("stage counters\n");
      std::printf("verifyBufferLookups  %.0f\nverifyQuickHashReads %.0f\nverifyQuickHashBytes  %.0f (%.1f MB)\n",
                  lookups, lookupsState, quickBytes, quickBytes / 1048576.0);
      std::printf("verifyDecodes        %.0f\nverifyCacheCopies    %.0f\nverifyCropCalls      %.0f\nverifyFlipCalls      %.0f\n",
                  decodes, copies, cropCalls, flipCalls);
      if (expensive > 0)
        std::printf("quickHashBytes/verify %.0f  (%d lookups x 64 KiB upper bound)\n",
                    quickBytes / expensive, 2);
    }
  }

  if (std::getenv("MSF_BASELINE_DUMP_WALKER")) {
    const std::size_t wp = lastJson.find("\"walker\":");
    if (wp != std::string::npos)
      std::printf("walker_json=%s\n",
                  lastJson.substr(wp, lastJson.find('}', wp) - wp + 1).c_str());
  }
  // D9a: the analyze split is the point of this node, so it is always printed
  // rather than hidden behind an env var.
  if (std::getenv("MSF_BASELINE_DUMP_ANALYZE")) {
    const std::size_t ap = lastJson.find("\"analyze\":");
    if (ap != std::string::npos) {
      const std::size_t end = lastJson.find("},", ap);
      std::printf("analyze_json=%s\n",
                  lastJson.substr(ap, (end == std::string::npos ? ap + 900 : end) - ap + 1).c_str());
    }
  }
  return 0;
}
