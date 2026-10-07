#pragma once
#include "analyze_telemetry.h"
#include "dataset_fingerprint.h"
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
// Node A (0.9.4 development line): TelemetryRecorder is the shared runtime
// instrumentation layer for the Adaptive Scheduler and video-decode work,
// not just a result display. Callers declare their purpose explicitly:
// UserDiagnostic (GUI real-search diagnostics) or Benchmark (CLI controlled
// comparison). A measured zero and "never measured" must never share one
// representation.
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
enum class TelemetryPurpose { UserDiagnostic, Benchmark };
inline const char* telemetryPurposeName(TelemetryPurpose p) { return p == TelemetryPurpose::Benchmark ? "Benchmark" : "UserDiagnostic"; }
struct TelemetryConfig {
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
  TelemetryPurpose purpose = TelemetryPurpose::UserDiagnostic;
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
    // Absolute wall-clock anchor in the same localTimeStr() convention as
    // meta.startedAt/finishedAt. tMs stays the relative axis; wallTime lets two
    // runs' timelines be compared directly. Second resolution by convention.
    std::string wallTime;
    double cpuProc = 0;
    double cpuSys = 0;
    double memMB = 0;
    bool gpu = false;
    double ioReadBps = 0;
    double ioWriteBps = 0;
    };
// Node A: scheduler-decision record. Structure only ??the Node B Adaptive
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
// Node A: calibration record. Structure only ??Node C implements calibration.
struct CalibrationTelemetry {
  MeasureState state = MeasureState::NotMeasured;
  bool started = false, completed = false;
  double durationMs = 0, confidence = 0;
  double cpuThroughput = 0, gpuThroughput = 0;
  double resizeThroughput = 0, decodeThroughput = 0;
  double transferCostMs = 0, queueLatencyMs = 0;
  // C2: per-metric states. Unmeasured values are never bare zeros anymore
  // (schema v2); readers must check states, not magnitudes.
  MeasureState cpuState = MeasureState::NotMeasured;
  MeasureState gpuState = MeasureState::NotMeasured;
  MeasureState resizeState = MeasureState::NotMeasured;
  MeasureState decodeState = MeasureState::NotMeasured;
  MeasureState transferState = MeasureState::NotMeasured;
  MeasureState queueState = MeasureState::NotMeasured;
  std::string profileId, profileVersion;
  void markMeasured() { state = MeasureState::Measured; }
  std::string toJson() const;
};
class TelemetryRecorder {
public:
  static constexpr std::size_t kSlowTop = 20;
  static constexpr int kSampleMs = 250;
  static constexpr std::size_t kMaxSamples = 50000;
  // Independent of app/engine/db versions; bump only on telemetry JSON
  // document change. NOTE: this number versions the telemetry document's
  // compatibility, not the CLI benchmark workflow; "benchmark" in the name
  // is legacy schema identity and is kept so existing readers keep working.
  // v2: calibration metric states (C2 first fills CalibrationTelemetry).
  // v3: D1a image-batch observability keys (packMs, cpuHashMs, batchCount,
  // batchItems, batchMaxDepth, batchState).
  // v4: D1b walker-queue and video-range keys.
  // v5: D2 per-range slowest-file key (maxRangeFileMs).
  // v6: D3-Minimal walker capacity + blocked ticks.
  // v7: D4a backend-internal GPU timing split (h2d/kernel/d2h device ms,
  //     host sync wait, host total) with per-metric states.
  // v8: D8a dataset identity (fingerprint + version + counts + state) so
  //     repeated runs can prove they used the same input data.
  // v9: D9a analyze internal stage split (index/scan/verify/video) plus
  //     verify cache/SSIM counters and their derived rates.
  static constexpr int kBenchmarkSchemaVersion = 9;
  // Sentinel for "frame count not provided by this caller".
  static constexpr std::size_t kFramesNotProvided = (std::numeric_limits<std::size_t>::max)();
  void start(const TelemetryConfig& cfg);
  void reset();
  // D8a: attaches dataset identity. Called by the engine right after start()
  // so every run carries proof of which input data produced it. A
  // non-"measured" result records state + null fingerprint, never a zero.
  void setDatasetFingerprint(const DatasetFingerprint& fp);
  const DatasetFingerprint& datasetFingerprint() const { return datasetFp_; }
  bool sampling() const { return sampling_.load(std::memory_order_relaxed); }
  bool finished() const { return finished_; }
  void addImageStageMs(double ms);
  void addVideoStageMs(double ms);
  void addAnalyzeMs(double ms);
  // D9a: analyze internal stage split + verify counters. The engine copies
  // the sink filled by ScanPipeline; the recorder only renders it, so
  // verification code never depends on engine telemetry. A stage that never
  // ran is reported not_measured, never 0.
  void setAnalyzeTelemetry(const AnalyzeTelemetry& t);
  void addWalkMs(double ms);
  void addRevalidateMs(double ms);
  // Node A global stages (additive; unrecorded stages stay NotMeasured).
  void addIncrementalMs(double ms);
  void addCandidateIndexMs(double ms);
  void addSimilarityMs(double ms);
  void addPersistenceMs(double ms);
  void addImage(std::uint64_t bytes, double decodeMs, double hashMs, double cropMs, bool usedGpu, const std::string& path);
  void addGpuBatchMs(double ms);
  // Node D1a image batch boundary observability.
  void beginImageBatch(std::size_t items);
  void endImageBatch();
  void addImagePackMs(double ms);
  void addImageCpuHashMs(double ms);
  // Node A image GPU sub-stages (structure only until queue/transfer split lands).
  void addImageGpuQueueMs(double ms);
  void addImageTransferMs(double ms);
  void addImageExecMs(double ms);
  // D4a: backend-internal timing for one GPU hash batch. The device-side
  // values come from timing points inside the GPU backend; syncMs and
  // totalMs are host wall time. Never call with partial data: either the
  // backend measured the whole call or the recorder is left untouched so the
  // metrics stay not_measured (never a zero standing in for missing time).
  void addImageGpuDeviceTiming(double h2dDeviceMs, double kernelDeviceMs,
                               double d2hDeviceMs, double syncHostMs,
                               double hostTotalMs, bool usedGpu);
  // Node D1b: walker-queue and video-range observability. Depth values are
  // passed in by the engine (exact queue.size() under its lock); the
  // recorder only accumulates counts and the maximum.
  void recordWalkerEnqueue(std::size_t depthAfterPush);
  void recordWalkerDequeue(std::size_t depthAfterPop);
  void noteWalkerStarved();
  // D3-Minimal: capacity is config (like sampleMs), blockedTicks counts
  // producer waits entered while full (100 ms units, roughly).
  void setWalkerCapacity(std::size_t capacity);
  void noteWalkerBlocked();
  // D1b range count/files; D2 adds the slowest file in the range so join
  // waste (rangeWall - maxFile) is quantifiable from JSON alone.
  void recordVideoRange(std::size_t files, double maxFileMs);
  void addVideo(std::uint64_t bytes, double durationSec, double buildMs, std::size_t frames, const std::string& path,
                std::size_t decodedFrames = kFramesNotProvided, std::size_t sampledFrames = kFramesNotProvided,
                bool cacheHit = false);
  void addVideoGpu(bool used, bool fallback, double gpuMs);
// E-3B: records one planner decision for one video file. Called for every
// analysed file, including cache hits, because the decision is cheap input
// analysis and is exactly what a reader needs to see when a scan looks odd.
void addVideoPlan(int decision, int reason, bool sparseAccepted, bool sparseRejected,
                  long long sparseSeeks, long long sparseDecoded, long long landingViolations);
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
  // Per-kind user-facing summary. Set alongside finalize() from the engine's
  // per-kind counters. Zero by default; serialized with measured/not_measured
  // states like every other optional telemetry value.
  void setKindScanned(std::size_t imgScanned, std::size_t vidScanned);
  void setMatchBreakdown(std::size_t imgPairs, std::size_t imgGroups, std::size_t imgDupFiles,
                         std::size_t vidPairs, std::size_t vidGroups, std::size_t vidDupFiles);
  // Scan-phase API for cancellation diagnosis. Call at each phase entry;
  // the value is only surfaced as meta.cancelledDuring when cancelled.
  void setPhase(const std::string& phase) { phase_ = phase; }
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
  TelemetryConfig cfg_;
  DatasetFingerprint datasetFp_{};
  AnalyzeTelemetry analyzeTel_{};
  bool analyzeTelRecorded_ = false;
    std::string startedAt_;
    // Wall-clock finish, recorded at finalize() in the same localTimeStr()
    // convention as startedAt_. Never derived from wallMs.
    std::string finishedAt_;
    // Current scan phase for cancellation diagnosis ("fingerprint", "walk",
    // "analyze"). Set at each phase entry; serialized as cancelledDuring only
    // when the run actually cancelled, else empty.
    std::string phase_;
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
  std::atomic<long long> imgQueueWaitNs_{0}, imgTransferNs_{0}, imgExecNs_{0}, imgPackNs_{0}, imgCpuHashNs_{0};
    std::atomic<std::uint64_t> imgBatchCount_{0}, imgBatchItems_{0}, imgBatchMaxDepth_{0};
    // D4a: per-batch backend-internal timing. Kept separate per boundary so
    // the JSON can show which element dominates instead of one lumped value.
    std::atomic<long long> imgH2dNs_{0}, imgKernelNs_{0}, imgD2hNs_{0};
    std::atomic<long long> imgSyncNs_{0}, imgGpuTotalNs_{0};
    std::atomic<std::uint64_t> imgGpuTimedBatches_{0};
    bool imgH2dRecorded_ = false, imgKernelRecorded_ = false, imgD2hRecorded_ = false;
    bool imgSyncRecorded_ = false, imgGpuTotalRecorded_ = false;
  bool imgDecodeRecorded_ = false, imgHashRecorded_ = false, imgCropRecorded_ = false, imgGpuRecorded_ = false;
  bool imgQueueWaitRecorded_ = false, imgTransferRecorded_ = false, imgExecRecorded_ = false;
  std::atomic<std::uint64_t> vidCount_{0}, vidBytes_{0}, vidFrames_{0};
  std::atomic<std::uint64_t> vidDecodedFrames_{0}, vidSampledFrames_{0};
  bool vidDecodedRecorded_ = false, vidSampledRecorded_ = false;
  // D1b: async video-range granularity (ranges launched, files admitted).
  // D2: slowest file per range (join-waste accounting).
  std::atomic<std::uint64_t> vidRangeCount_{0}, vidRangeFiles_{0};
  std::atomic<long long> vidRangeMaxNs_{0};
  std::atomic<long long> vidBuildNs_{0};
  std::atomic<std::uint64_t> vidGpu_{0}, vidGpuFallback_{0};
  std::atomic<long long> vidGpuNs_{0};
  // E-3B adaptive sampling planner. A fallback must never be silent, so the
  // decision, the reason, whether a sparse result was accepted, and why one was
  // rejected are all counted separately.
  std::atomic<std::uint64_t> vidPlanTotal_{0}, vidPlanSeq_{0}, vidPlanSparse_{0}, vidPlanUnavail_{0};
  std::atomic<std::uint64_t> vidPlanAccepted_{0}, vidPlanRejected_{0}, vidPlanLandingViol_{0};
  std::atomic<long long> vidPlanSparseSeeks_{0}, vidPlanSparseDecoded_{0};
  // Reasons are an enum today; a fixed slot per reason keeps the JSON stable
  // without inventing a dynamic key space.
  static constexpr int kPlanReasonCount = 9;
  std::atomic<std::uint64_t> vidPlanReason_[kPlanReasonCount];
  // D1b walker queue: unbounded by construction, so the producer never
  // blocks (no producerBlocked counter exists); the consumer records idle
  // polls while the walker is alive as starved ticks.
  std::atomic<std::uint64_t> walkQueued_{0}, walkDequeued_{0}, walkMaxDepth_{0};
  std::atomic<std::uint64_t> walkStarved_{0};
  std::atomic<std::uint64_t> walkCapacity_{0}, walkBlocked_{0};
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
  // Total physical system RAM in MiB, captured once at start(). Lets the
  // process peak (memMBMax) be judged against the machine, which is what
  // matters on low-memory systems. 0 means not captured.
  double memSystemMB_ = 0;
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
  std::size_t imgScanned_ = 0, vidScanned_ = 0;
  std::size_t imgPairs_ = 0, imgGroups_ = 0, imgDupFiles_ = 0;
  std::size_t vidPairs_ = 0, vidGroups_ = 0, vidDupFiles_ = 0;
  bool kindBreakdownSet_ = false;
};
}
