#include "benchmark.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
int main() {
  msf::BenchmarkRecorder rec;
  if (rec.hasData()) return 1;
  msf::BenchmarkConfig cfg;
  cfg.root = "C:/media";
   cfg.build = "0.9.4.3"; cfg.engine = "1.5.0"; cfg.db = "1.0.3";
  cfg.distance = 8; cfg.cpuWorkers = 4; cfg.gpuBatch = 64; cfg.gpuBackend = "CUDA";
  cfg.scanImages = true; cfg.scanVideos = true; cfg.cudaAvailable = false;
  rec.start(cfg);
  if (!rec.hasData()) return 2;
  rec.addWalkMs(120.0);
  rec.addRevalidateMs(30.0);
  rec.addImageStageMs(400.0);
  rec.addVideoStageMs(150.0);
  rec.addAnalyzeMs(60.0);
  rec.addGpuBatchMs(25.0);
  for (int i = 0; i < 30; ++i)
    rec.addImage(1000ULL * (i + 1), 1.0 + i, 0.5, 0.25, i % 3 == 0, "img" + std::to_string(i) + ".jpg");
  for (int i = 0; i < 25; ++i)
    rec.addVideo(1000000ULL * (i + 1), 60.0 * (i + 1), 10.0 * (i + 1), 30 * (i + 1), "vid" + std::to_string(i) + ".mp4");
  rec.addVideoGpu(true, false, 3.0);
  bool gpu = false;
  rec.startSampler([&gpu]() { return gpu; });
  std::this_thread::sleep_for(std::chrono::milliseconds(650));
   rec.stopSampler();
   for (int i = 0; i < 12; ++i) rec.addStreamedMatch();
   rec.finalize(true, 100, 55, 45, 200, 12, 6, 75.5, 18, 2);
  const std::string js = rec.toJson();
  auto need = [&](const char* s) {
    if (js.find(s) == std::string::npos) { std::cerr << "missing: " << s << "\n"; return false; }
    return true;
  };
   if (!need("\"build\":\"0.9.4.3\"")) return 3;
  if (!need("\"completed\":true")) return 4;
  if (!need("\"count\":30")) return 5;
  if (!need("\"gpuHashed\":10")) return 6;
  if (!need("\"count\":25")) return 7;
  if (!need("\"gpuDutyPct\":0")) return 8;
  if (!need("\"series\":[[")) return 9;
  if (!need("\"slowest\":[{")) return 10;
  if (!need("img29.jpg")) return 11;
  if (!need("vid24.mp4")) return 12;
   if (!need("\"matches\":{\"candidates\":200")) return 13;
   if (!need("\"pairs\":12")) return 14;
   if (!need("\"retainedPairs\":12")) return 15;
   if (!need("\"secPerPlayMin\"")) return 16;
   if (!need("\"gpuBatchMs\":25.000")) return 17;
   if (!need("\"summary\":{\"wallMs\"")) return 18;
   if (!need("\"config\":{\"distance\":8")) return 19;
   if (!need("\"resources\":{\"sampleMs\":250")) return 20;
   if (!need("\"videos\":{\"count\":25")) return 21;
   if (!need("\"gpuVideos\":1")) return 22;
   if (!need("\"diskVolume\":\"C:\"")) return 23;
   if (!need("\"ioReadBpsMax\"")) return 24;
   if (!need("\"procIoReadBytes\"")) return 25;
   if (!need("\"cpuProcStd\"")) return 26;
   if (!need("\"gpuLongestIdleMs\"")) return 27;
   // Node A: schema version is independent of app/engine/db versions.
   if (!need("\"schemaVersion\":1")) return 30;
   if (!need("\"runId\":\"")) return 31;
   if (!need("\"completionReason\":\"completed\"")) return 32;
   if (!need("\"cancelled\":false")) return 33;
   if (!need("\"gpuBackend\":\"CUDA\"")) return 34;
  // Node A: sampler ran, so resource states are measured (not zero-as-value).
  if (!need("\"sampleState\":\"measured\"")) return 35;
  // Node A: disk state follows counter availability — measured where PDH
  // LogicalDisk counters exist, not_available where they do not (this
  // machine). Either way the state must agree with the availability flag.
  {
    const bool avail = js.find("\"diskAvailable\":true") != std::string::npos;
    const char* want = avail ? "\"diskState\":\"measured\"" : "\"diskState\":\"not_available\"";
    if (!need(want)) return 36;
    if (!avail && js.find("\"diskAvailable\":false") == std::string::npos) { std::cerr << "missing: diskAvailable flag\n"; return 59; }
  }
   // Node A: legacy 5-arg addVideo leaves decoded/sampled unmeasured (null).
   if (!need("\"decodedFrames\":null")) return 37;
   if (!need("\"decodedFramesState\":\"not_measured\"")) return 38;
   if (!need("\"sampledFrames\":null")) return 39;
   if (!need("\"sampledFramesState\":\"not_measured\"")) return 40;
   // Node A: recorded vs never-recorded stages stay distinguishable.
   if (!need("\"enumeration\":{\"ms\":120.000,\"state\":\"measured\"}")) return 41;
   if (!need("\"incremental\":{\"ms\":0.000,\"state\":\"not_measured\"}")) return 42;
   if (!need("\"gpuQueueWaitState\":\"not_measured\"")) return 43;
   // Node A: scheduler/calibration placeholders default to not_measured.
   if (!need("\"scheduler\":{\"state\":\"not_measured\"")) return 44;
   if (!need("\"calibration\":{\"state\":\"not_measured\"")) return 45;
   if (!need("\"files\":{\"started\":0,\"completed\":0,\"remaining\":0,\"state\":\"not_measured\"}")) return 46;
  rec.abortUnfinished();
   if (!rec.hasData()) return 28;
  // Node A measured path: explicit frame counts, file progress, scheduler and
  // calibration records, and a sampler-free run whose zeros are not_measured.
  {
    msf::BenchmarkRecorder r2;
    msf::BenchmarkConfig c2;
    c2.root = "C:/media"; c2.build = "0.9.4.3"; c2.engine = "1.5.0"; c2.db = "1.0.3";
    c2.distance = 8; c2.cpuWorkers = 2; c2.gpuBatch = 32; c2.gpuBackend = "CPU";
    r2.start(c2);
    r2.addVideo(1000ULL, 10.0, 5.0, 8, "v.mp4", 10, 12);
    r2.addIncrementalMs(7.0);
    r2.addImageGpuQueueMs(1.5);
    r2.scheduler().markMeasured();
    r2.scheduler().selectedBackend = "CPU";
    r2.scheduler().backendFallbacks = 1;
    r2.calibration().markMeasured();
    r2.calibration().started = r2.calibration().completed = true;
    r2.calibration().confidence = 0.5;
    r2.setFileProgress(10, 9, 1);
    r2.finalize(true, 10, 9, 1, 4, 1, 1, 50.0, 0, 0);
    const std::string j2 = r2.toJson();
    auto need2 = [&](const char* s) {
      if (j2.find(s) == std::string::npos) { std::cerr << "missing2: " << s << "\n"; return false; }
      return true;
    };
    if (!need2("\"decodedFrames\":10,\"decodedFramesState\":\"measured\"")) return 47;
    if (!need2("\"sampledFrames\":12,\"sampledFramesState\":\"measured\"")) return 48;
    if (!need2("\"incremental\":{\"ms\":7.000,\"state\":\"measured\"}")) return 49;
    if (!need2("\"gpuQueueWaitState\":\"measured\"")) return 50;
    if (!need2("\"scheduler\":{\"state\":\"measured\"")) return 51;
    if (!need2("\"selectedBackend\":\"CPU\"")) return 52;
    if (!need2("\"calibration\":{\"state\":\"measured\"")) return 53;
    if (!need2("\"files\":{\"started\":10,\"completed\":9,\"remaining\":1,\"state\":\"measured\"}")) return 54;
    // No sampler: numeric zeros with an explicit not_measured state.
    if (!need2("\"sampleState\":\"not_measured\"")) return 55;
    if (!need2("\"gpuDutyPct\":0")) return 56;
  }
  // Node A: a root without a drive letter leaves disk not_available (not 0).
  {
    msf::BenchmarkRecorder r3;
    msf::BenchmarkConfig c3;
    c3.root = "relative/path"; c3.build = "0.9.4.3"; c3.engine = "1.5.0"; c3.db = "1.0.3";
    r3.start(c3);
    r3.finalize(true, 0, 0, 0, 0, 0, 0, 0.0, 0, 0);
    const std::string j3 = r3.toJson();
    if (j3.find("\"diskState\":\"not_available\"") == std::string::npos) { std::cerr << "missing3: diskState\n"; return 57; }
    if (j3.find("\"diskAvailable\":false") == std::string::npos) { std::cerr << "missing3: diskAvailable\n"; return 58; }
  }
  rec.reset();
   if (rec.hasData()) return 29;
  std::cout << "benchmark=ok\n";
  return 0;
}
