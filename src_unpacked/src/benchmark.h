#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace msf {
// Node A (0.9.4 development line): benchmark is the instrumentation layer for
// the Adaptive Scheduler and video-decode work, not just a result display.
// A measured zero and "never measured" must never share one representation.
enum class MeasureState { NotMeasured, Measured, NotAvailable, Partial, Failed, Fallback };
inline const char* measureStateName(MeasureState s) {
  switch (s) {
    case MeasureState::Measured: return "measured";
    case MeasureState::NotAvailable: return "not_available";
    case MeasureState::Partial: return "partial";
    case MeasureState::Failed: return "failed";
    case MeasureState::Fallback: return "fallback";
    default: return "not_measured";
  }
}
struct BenchmarkConfig {
  std::string root;
  std::string build;
  std::string engine;
  std::string db;
  bool detail = true;
  unsigned distance = 8;
  int cpuWorkers = 0;
  bool gpuEnabled = true;
  // Canonical GPU backend name as resolved by the GPU abstraction
  // ("CUDA", "CPU", ...). Empty = not recorded (pre-Node-A caller).
  std::string gpuBackend;
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
  double ioReadBps = 0;
  double ioWriteBps = 0;
};
// Node A: scheduler-decision record. Structure only — the Node B Adaptive
// Scheduler fills it; until then it stays NotMeasured and must not be read
// as "zero work share".
struct SchedulerTelemetry {
  MeasureState state = MeasureState::NotMeasured;
  double initialCpuCapacity = 0, initialGpuCapacity = 0;
  double currentCpuCapacity = 0, currentGpuCapacity = 0;
  double cpuWorkShare = 0, gpuWorkShare = 0;
  double cpuQueueDepth = 0, gpuQueueDepth = 0;
  double cpuQueueWaitMs = 0, gpuQueueWaitMs = 0;
  std::uint64_t adjustmentCount = 0, throttlingEvents = 0, externalLoadThrottling = 0;
  std::string selectedBackend;
  std::uint64_t backendFallbacks = 0;
  void markMeasured() { state = MeasureState::Measured; }
  std::string toJson() const;
};
// Node A: calibration record. Structure only — Node C implements calibration.
struct CalibrationTelemetry {
  MeasureState state = MeasureState::NotMeasured;
  bool started = false, completed = false;
  double durationMs = 0, confidence = 0;
  double cpuThroughput = 0, gpuThroughput = 0;
  double resizeThroughput = 0, decodeThroughput = 0;
  double transferCostMs = 0, queueLatencyMs = 0;
  std::string profileId, profileVersion;
  void markMeasured() { state = MeasureState::Measured; }
  std::string toJson() const;
};
class BenchmarkRecorder {
public:
  static constexpr std::size_t kSlowTop = 20;
  static constexpr int kSampleMs = 250;
  static constexpr std::size_t kMaxSamples = 50000;
  // Independent of app/engine/db versions; bump only on benchmark schema change.
  static constexpr int kBenchmarkSchemaVersion = 1;
  // Sentinel for "frame count not provided by this caller".
  static constexpr std::size_t kFramesNotProvided = (std::numeric_limits<std::size_t>::max)();
  void start(const BenchmarkConfig& cfg);
  void reset();
  bool sampling() const { return sampling_.load(std::memory_order_relaxed); }
  bool finished() const { return finished_; }
  void addImageStageMs(double ms);
  void addVideoStageMs(double ms);
  void addAnalyzeMs(double ms);
  void addWalkMs(double ms);
  void addRevalidateMs(double ms);
  // Node A global stages (additive; unrecorded stages stay NotMeasured).
  void addIncrementalMs(double ms);
  void addCandidateIndexMs(double ms);
  void addSimilarityMs(double ms);
  void addPersistenceMs(double ms);
  void addImage(std::uint64_t bytes, double decodeMs, double hashMs, double cropMs, bool usedGpu, const std::string& path);
  void addGpuBatchMs(double ms);
  // Node A image GPU sub-stages (structure only until queue/transfer split lands).
  void addImageGpuQueueMs(double ms);
  void addImageTransferMs(double ms);
  void addImageExecMs(double ms);
  void addVideo(std::uint64_t bytes, double durationSec, double buildMs, std::size_t frames, const std::string& path,
                std::size_t decodedFrames = kFramesNotProvided, std::size_t sampledFrames = kFramesNotProvided);
  void addVideoGpu(bool used, bool fallback, double gpuMs);
  void addStreamedMatch() { streamedMatches_.fetch_add(1, std::memory_order_relaxed); }
  void startSampler(std::function<bool()> gpuActive);
  void stopSampler();
  void abortUnfinished();
  // Node A cancellation / partial execution (additive; completed flag kept).
  void setCancelled(const std::string& reason);
  void setPaused(bool paused);
  void setFailed(const std::string& stage, const std::string& reason);
  void setCompletionReason(const std::string& reason);
  void setFileProgress(std::size_t started, std::size_t completed, std::size_t remaining);
  SchedulerTelemetry& scheduler() { return scheduler_; }
  const SchedulerTelemetry& scheduler() const { return scheduler_; }
  CalibrationTelemetry& calibration() { return calibration_; }
  const CalibrationTelemetry& calibration() const { return calibration_; }
  void finalize(bool completed, std::size_t scanned, std::size_t analyzed, std::size_t unchanged,
                std::size_t candidates, std::size_t matches, std::size_t groups, double reductionPct,
                std::uint64_t gpuImages, std::uint64_t gpuFallback);
  std::string toJson() const;
  bool hasData() const { return started_; }
  std::uint64_t videoGpuFallbacks() const { return vidGpuFallback_.load(); }
  static std::string escapeJson(const std::string& s);
private:
  void sampleOnce(double tMs);
  bool started_ = false;
  BenchmarkConfig cfg_;
  std::string startedAt_;
  std::string runId_;
  double wallMs_ = 0;
  long long startTick_ = 0;
  double walkMs_ = 0, imageStageMs_ = 0, videoStageMs_ = 0, analyzeMs_ = 0, revalidateMs_ = 0;
  double incrementalMs_ = 0, candidateIndexMs_ = 0, similarityMs_ = 0, persistenceMs_ = 0;
  bool walkRecorded_ = false, imageStageRecorded_ = false, videoStageRecorded_ = false;
  bool analyzeRecorded_ = false, revalidateRecorded_ = false, incrementalRecorded_ = false;
  bool candidateIndexRecorded_ = false, similarityRecorded_ = false, persistenceRecorded_ = false;
  std::atomic<std::uint64_t> imgCount_{0}, imgBytes_{0}, imgGpu_{0};
  std::atomic<long long> imgDecodeNs_{0}, imgHashNs_{0}, imgCropNs_{0}, imgGpuNs_{0};
  std::atomic<long long> imgQueueWaitNs_{0}, imgTransferNs_{0}, imgExecNs_{0};
  bool imgDecodeRecorded_ = false, imgHashRecorded_ = false, imgCropRecorded_ = false, imgGpuRecorded_ = false;
  bool imgQueueWaitRecorded_ = false, imgTransferRecorded_ = false, imgExecRecorded_ = false;
  std::atomic<std::uint64_t> vidCount_{0}, vidBytes_{0}, vidFrames_{0};
  std::atomic<std::uint64_t> vidDecodedFrames_{0}, vidSampledFrames_{0};
  bool vidDecodedRecorded_ = false, vidSampledRecorded_ = false;
  std::atomic<long long> vidBuildNs_{0};
  std::atomic<std::uint64_t> vidGpu_{0}, vidGpuFallback_{0};
  std::atomic<long long> vidGpuNs_{0};
  std::atomic<double> vidPlaySec_{0};
  mutable std::mutex slowMutex_;
  std::vector<SlowFile> slowImages_, slowVideos_;
  std::atomic<bool> sampling_{false};
  std::function<bool()> gpuActiveFn_;
  std::thread sampler_;
  mutable std::mutex sampleMutex_;
  std::vector<ResourceSample> samples_;
  bool samplerStarted_ = false;
  // Disk I/O of the scanned volume (PDH LogicalDisk, drive-wide) plus
  // process-attributed cumulative totals. Lets slow-file buildMs be compared
  // against drive saturation instead of guessing CPU vs I/O bound.
  void* diskQuery_ = nullptr;
  void* diskReadCounter_ = nullptr;
  void* diskWriteCounter_ = nullptr;
  std::string diskVolume_;
  bool diskAvailable_ = false;
  // Latched at open time: stopSampler() closes the query, but the samples
  // taken while it was open stay measured (never re-labeled not_available).
  bool diskWasAvailable_ = false;
  std::atomic<unsigned long long> procIoReadBytes_{0}, procIoWriteBytes_{0};
  std::atomic<unsigned long long> procIoReadOps_{0}, procIoWriteOps_{0};
  void openDiskCounters();
  void closeDiskCounters();
  bool samplesTruncated_ = false;
  long long prevProcK_ = -1, prevProcU_ = -1, prevSysI_ = -1, prevSysK_ = -1, prevSysU_ = -1, prevTick_ = -1;
  int cpuCount_ = 1;
  bool completed_ = false;
  bool cancelled_ = false, paused_ = false, failed_ = false;
  std::string failedStage_, completionReason_;
  std::size_t filesStarted_ = 0, filesCompleted_ = 0, filesRemaining_ = 0;
  bool fileProgressRecorded_ = false;
  SchedulerTelemetry scheduler_;
  CalibrationTelemetry calibration_;
  bool finished_ = false;
  std::atomic<std::uint64_t> streamedMatches_{0};
  std::size_t scanned_ = 0, analyzed_ = 0, unchanged_ = 0, candidates_ = 0, matches_ = 0, groups_ = 0;
  double reductionPct_ = 0;
  std::uint64_t gpuImages_ = 0, gpuFallback_ = 0;
};
}
