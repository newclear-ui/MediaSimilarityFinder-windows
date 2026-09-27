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

  if (std::getenv("MSF_BASELINE_DUMP_WALKER")) {
    const std::size_t wp = lastJson.find("\"walker\":");
    if (wp != std::string::npos)
      std::printf("walker_json=%s\n",
                  lastJson.substr(wp, lastJson.find('}', wp) - wp + 1).c_str());
  }
  return 0;
}
