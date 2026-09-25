#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace msf {
struct BenchmarkConfig {
  std::string root;
  std::string build;
  std::string engine;
  std::string db;
  unsigned distance = 8;
  int cpuWorkers = 0;
  bool gpuEnabled = true;
  std::size_t gpuBatch = 256;
  bool scanImages = true;
  bool scanVideos = true;
  bool cudaAvailable = false;
};
struct SlowFile {
  std::string path;
  double ms = 0;
  std::uint64_t bytes = 0;
  double durationSec = 0;
  std::size_t frames = 0;
};
struct ResourceSample {
  double tMs = 0;
  double cpuProc = 0;
  double cpuSys = 0;
  double memMB = 0;
  bool gpu = false;
};
class BenchmarkRecorder {
public:
  static constexpr std::size_t kSlowTop = 20;
  static constexpr int kSampleMs = 250;
  static constexpr std::size_t kMaxSamples = 50000;
  void start(const BenchmarkConfig& cfg);
  void reset();
  void addImageStageMs(double ms);
  void addVideoStageMs(double ms);
  void addAnalyzeMs(double ms);
  void addWalkMs(double ms);
  void addRevalidateMs(double ms);
  void addImage(std::uint64_t bytes, double decodeMs, double hashMs, double cropMs, bool usedGpu, const std::string& path);
  void addGpuBatchMs(double ms);
  void addVideo(std::uint64_t bytes, double durationSec, double buildMs, std::size_t frames, const std::string& path);
  void startSampler(std::function<bool()> gpuActive);
  void stopSampler();
  void finalize(bool completed, std::size_t scanned, std::size_t analyzed, std::size_t unchanged,
                std::size_t candidates, std::size_t matches, std::size_t groups, double reductionPct,
                std::uint64_t gpuImages, std::uint64_t gpuFallback);
  std::string toJson() const;
  bool hasData() const { return started_; }
private:
  static std::string escapeJson(const std::string& s);
  void sampleOnce(double tMs);
  bool started_ = false;
  BenchmarkConfig cfg_;
  std::string startedAt_;
  double wallMs_ = 0;
  long long startTick_ = 0;
  double walkMs_ = 0, imageStageMs_ = 0, videoStageMs_ = 0, analyzeMs_ = 0, revalidateMs_ = 0;
  std::atomic<std::uint64_t> imgCount_{0}, imgBytes_{0}, imgGpu_{0};
  std::atomic<long long> imgDecodeNs_{0}, imgHashNs_{0}, imgCropNs_{0}, imgGpuNs_{0};
  std::atomic<std::uint64_t> vidCount_{0}, vidBytes_{0}, vidFrames_{0};
  std::atomic<long long> vidBuildNs_{0};
  std::atomic<double> vidPlaySec_{0};
  mutable std::mutex slowMutex_;
  std::vector<SlowFile> slowImages_, slowVideos_;
  std::atomic<bool> sampling_{false};
  std::function<bool()> gpuActiveFn_;
  std::thread sampler_;
  mutable std::mutex sampleMutex_;
  std::vector<ResourceSample> samples_;
  bool samplesTruncated_ = false;
  long long prevProcK_ = -1, prevProcU_ = -1, prevSysI_ = -1, prevSysK_ = -1, prevSysU_ = -1, prevTick_ = -1;
  int cpuCount_ = 1;
  bool completed_ = false;
  std::size_t scanned_ = 0, analyzed_ = 0, unchanged_ = 0, candidates_ = 0, matches_ = 0, groups_ = 0;
  double reductionPct_ = 0;
  std::uint64_t gpuImages_ = 0, gpuFallback_ = 0;
};
}
