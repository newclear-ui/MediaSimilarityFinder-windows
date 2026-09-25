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
  cfg.build = "0.9.2.91"; cfg.engine = "1.1.0"; cfg.db = "1.0.2";
  cfg.distance = 8; cfg.cpuWorkers = 4; cfg.gpuBatch = 64;
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
  bool gpu = false;
  rec.startSampler([&gpu]() { return gpu; });
  std::this_thread::sleep_for(std::chrono::milliseconds(650));
  rec.stopSampler();
  rec.finalize(true, 100, 55, 45, 200, 12, 6, 75.5, 18, 2);
  const std::string js = rec.toJson();
  auto need = [&](const char* s) {
    if (js.find(s) == std::string::npos) { std::cerr << "missing: " << s << "\n"; return false; }
    return true;
  };
  if (!need("\"build\":\"0.9.2.91\"")) return 3;
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
  if (!need("\"secPerPlayMin\"")) return 14;
  if (!need("\"gpuBatchMs\":25.000")) return 15;
  if (!need("\"summary\":{\"wallMs\"")) return 16;
  if (!need("\"config\":{\"distance\":8")) return 17;
  if (!need("\"resources\":{\"sampleMs\":250")) return 18;
  if (!need("\"videos\":{\"count\":25")) return 19;
  if (!need("\"cpuProcStd\"")) return 20;
  if (!need("\"gpuLongestIdleMs\"")) return 21;
  std::cout << "benchmark=ok\n";
  return 0;
}
