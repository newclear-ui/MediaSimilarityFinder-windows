#include "benchmark.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <pdh.h>
#endif
namespace msf {
namespace {
long long nowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::string localTimeStr() {
  const std::time_t t = std::time(nullptr);
  std::tm tmv{};
#ifdef _WIN32
  localtime_s(&tmv, &t);
#else
  localtime_r(&t, &tmv);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmv);
  return buf;
}
void appendSlow(std::vector<SlowFile>& v, SlowFile item) {
  if (v.size() >= TelemetryRecorder::kSlowTop && item.ms <= v.back().ms) return;
  v.push_back(std::move(item));
  std::sort(v.begin(), v.end(), [](const SlowFile& a, const SlowFile& b) { return a.ms > b.ms; });
  if (v.size() > TelemetryRecorder::kSlowTop) v.pop_back();
}
} // namespace
std::string TelemetryRecorder::escapeJson(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\b': o += "\\b"; break;
      case '\f': o += "\\f"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          o += buf;
        } else {
          o += (char)c;
        }
    }
  }
  return o;
}
void TelemetryRecorder::openDiskCounters() {
#ifdef _WIN32
  closeDiskCounters();
  diskVolume_.clear();
  diskAvailable_ = false;
  diskWasAvailable_ = false;
  // Locale-safe: English counter names work on any Windows display language.
  if (cfg_.root.size() >= 2 && cfg_.root[1] == ':' &&
      ((cfg_.root[0] >= 'A' && cfg_.root[0] <= 'Z') ||
       (cfg_.root[0] >= 'a' && cfg_.root[0] <= 'z'))) {
    diskVolume_.assign(1, (char)std::toupper(cfg_.root[0]));
    diskVolume_ += ':';
    PDH_HQUERY q = nullptr;
    if (PdhOpenQueryA(nullptr, 0, &q) == ERROR_SUCCESS) {
      PDH_HCOUNTER rd = nullptr, wr = nullptr;
      const std::string base = "\\\\LogicalDisk(" + diskVolume_ + ")\\";
      // NOTE: no first-collect requirement here. The first
      // PdhCollectQueryData right after adding rate counters commonly
      // returns PDH_NO_DATA (needs two samples); treating that as fatal
      // disabled disk monitoring for the whole run (observed as
      // diskAvailable:false with a valid volume). Baseline collection is
      // best-effort; per-tick sampling tolerates failures.
      const bool ok = PdhAddEnglishCounterA(q, (base + "Disk Read Bytes/sec").c_str(), 0, &rd) == ERROR_SUCCESS &&
                      PdhAddEnglishCounterA(q, (base + "Disk Write Bytes/sec").c_str(), 0, &wr) == ERROR_SUCCESS;
      PdhCollectQueryData(q);
      if (ok) {
        diskQuery_ = q;
        diskReadCounter_ = rd;
        diskWriteCounter_ = wr;
        diskAvailable_ = true;
        diskWasAvailable_ = true;
      } else {
        if (q) PdhCloseQuery(q);
      }
    }
  }
#else
  diskVolume_.clear();
  diskAvailable_ = false;
#endif
}
void TelemetryRecorder::closeDiskCounters() {
#ifdef _WIN32
  if (diskQuery_) {
    PdhCloseQuery(static_cast<PDH_HQUERY>(diskQuery_));
    diskQuery_ = nullptr;
    diskReadCounter_ = nullptr;
    diskWriteCounter_ = nullptr;
  }
#endif
  diskAvailable_ = false;
}
void TelemetryRecorder::reset() {
  stopSampler();
  started_ = false;
  finished_ = false;
}
void TelemetryRecorder::setCancelled(const std::string& reason) {
  cancelled_ = true;
  if (!reason.empty()) completionReason_ = reason;
}
void TelemetryRecorder::setPaused(bool paused) { paused_ = paused; }
void TelemetryRecorder::setFailed(const std::string& stage, const std::string& reason) {
  failed_ = true;
  failedStage_ = stage;
  if (!reason.empty()) completionReason_ = reason;
}
void TelemetryRecorder::setCompletionReason(const std::string& reason) { completionReason_ = reason; }
void TelemetryRecorder::setFileProgress(std::size_t started, std::size_t completed, std::size_t remaining) {
  filesStarted_ = started; filesCompleted_ = completed; filesRemaining_ = remaining;
  fileProgressRecorded_ = true;
}
void TelemetryRecorder::setKindScanned(std::size_t imgScanned, std::size_t vidScanned) {
  imgScanned_ = imgScanned; vidScanned_ = vidScanned;
  kindBreakdownSet_ = true;
}
void TelemetryRecorder::setMatchBreakdown(std::size_t imgPairs, std::size_t imgGroups, std::size_t imgDupFiles,
                                          std::size_t vidPairs, std::size_t vidGroups, std::size_t vidDupFiles) {
  imgPairs_ = imgPairs; imgGroups_ = imgGroups; imgDupFiles_ = imgDupFiles;
  vidPairs_ = vidPairs; vidGroups_ = vidGroups; vidDupFiles_ = vidDupFiles;
  kindBreakdownSet_ = true;
}
void TelemetryRecorder::abortUnfinished() {
  if (started_ && !finished_) {
    setCancelled("aborted");
    finalize(false, 0, 0, 0, 0, 0, 0, 0.0, 0, 0);
  }
}
void TelemetryRecorder::setDatasetFingerprint(const DatasetFingerprint& fp) {
  datasetFp_ = fp;
}

void TelemetryRecorder::start(const TelemetryConfig& cfg) {
  stopSampler();
  cfg_ = cfg;
  // Each run starts from an unmeasured dataset identity. The caller attaches
  // the real one immediately after start(); until then the JSON must say
  // not_available rather than carry a stale value from a previous run.
  datasetFp_ = DatasetFingerprint{};
  // Same rule for the D9a analyze split: a fresh run must never inherit the
  // previous run's stage times or verify counters.
  analyzeTel_ = AnalyzeTelemetry{};
  analyzeTelRecorded_ = false;
  startedAt_ = localTimeStr();
  finishedAt_.clear();
  phase_.clear();
#ifdef _WIN32
  // Total physical RAM once per run. Lets memMBMax be judged against the
  // machine on low-memory systems. Failure leaves 0, never a guess.
  {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms) && ms.ullTotalPhys > 0)
      memSystemMB_ = (double)ms.ullTotalPhys / (1024.0 * 1024.0);
    else
      memSystemMB_ = 0;
  }
#else
  memSystemMB_ = 0;
#endif
  static std::atomic<std::uint64_t> runCounter{0};
  runId_ = startedAt_ + "-" + std::to_string(runCounter.fetch_add(1, std::memory_order_relaxed) + 1);
  startTick_ = nowNs();
  wallMs_ = walkMs_ = imageStageMs_ = videoStageMs_ = analyzeMs_ = revalidateMs_ = 0;
  incrementalMs_ = candidateIndexMs_ = similarityMs_ = persistenceMs_ = 0;
  walkRecorded_ = imageStageRecorded_ = videoStageRecorded_ = false;
  analyzeRecorded_ = revalidateRecorded_ = incrementalRecorded_ = false;
  candidateIndexRecorded_ = similarityRecorded_ = persistenceRecorded_ = false;
  imgCount_ = 0; imgBytes_ = 0; imgGpu_ = 0;
  imgDecodeNs_ = 0; imgHashNs_ = 0; imgCropNs_ = 0; imgGpuNs_ = 0;
  imgQueueWaitNs_ = 0; imgTransferNs_ = 0; imgExecNs_ = 0; imgPackNs_ = 0; imgCpuHashNs_ = 0;
  imgBatchCount_ = 0; imgBatchItems_ = 0; imgBatchMaxDepth_ = 0;
  imgH2dNs_ = 0; imgKernelNs_ = 0; imgD2hNs_ = 0; imgSyncNs_ = 0; imgGpuTotalNs_ = 0;
  imgGpuTimedBatches_ = 0;
  imgH2dRecorded_ = imgKernelRecorded_ = imgD2hRecorded_ = false;
  imgSyncRecorded_ = imgGpuTotalRecorded_ = false;
  imgDecodeRecorded_ = imgHashRecorded_ = imgCropRecorded_ = imgGpuRecorded_ = false;
  imgQueueWaitRecorded_ = imgTransferRecorded_ = imgExecRecorded_ = false;
  vidCount_ = 0; vidBytes_ = 0; vidFrames_ = 0; vidBuildNs_ = 0;
  vidDecodedFrames_ = 0; vidSampledFrames_ = 0;
  vidDecodedRecorded_ = vidSampledRecorded_ = false;
  vidRangeCount_ = 0; vidRangeFiles_ = 0;
  vidRangeMaxNs_ = 0;
  walkQueued_ = 0; walkDequeued_ = 0; walkMaxDepth_ = 0; walkStarved_ = 0;
  walkCapacity_ = 0; walkBlocked_ = 0;
  vidPlaySec_ = 0;
  {
    std::lock_guard<std::mutex> g(slowMutex_);
    slowImages_.clear(); slowVideos_.clear();
  }
  {
    std::lock_guard<std::mutex> g(sampleMutex_);
    samples_.clear(); samplesTruncated_ = false;
  }
  prevProcK_ = prevProcU_ = prevSysI_ = prevSysK_ = prevSysU_ = prevTick_ = -1;
#ifdef _WIN32
  SYSTEM_INFO si{};
  GetSystemInfo(&si);
  cpuCount_ = si.dwNumberOfProcessors > 0 ? (int)si.dwNumberOfProcessors : 1;
#else
  cpuCount_ = 1;
#endif
  scanned_ = analyzed_ = unchanged_ = candidates_ = matches_ = groups_ = 0;
  streamedMatches_.store(0, std::memory_order_relaxed);
  procIoReadBytes_.store(0, std::memory_order_relaxed);
  procIoWriteBytes_.store(0, std::memory_order_relaxed);
  procIoReadOps_.store(0, std::memory_order_relaxed);
  procIoWriteOps_.store(0, std::memory_order_relaxed);
  openDiskCounters();
  reductionPct_ = 0;
  gpuImages_ = gpuFallback_ = 0;
  vidGpu_.store(0, std::memory_order_relaxed); vidGpuFallback_.store(0, std::memory_order_relaxed); vidGpuNs_.store(0, std::memory_order_relaxed);
  completed_ = false;
  cancelled_ = paused_ = failed_ = false;
  failedStage_.clear(); completionReason_.clear();
  filesStarted_ = filesCompleted_ = filesRemaining_ = 0;
  fileProgressRecorded_ = false;
  scheduler_ = SchedulerTelemetry{};
  calibration_ = CalibrationTelemetry{};
  samplerStarted_ = false;
  finished_ = false;
  started_ = true;
}
void TelemetryRecorder::addImageStageMs(double ms) { imageStageMs_ += ms; imageStageRecorded_ = true; }
void TelemetryRecorder::addVideoStageMs(double ms) { videoStageMs_ += ms; videoStageRecorded_ = true; }
void TelemetryRecorder::setAnalyzeTelemetry(const AnalyzeTelemetry& t) {
  analyzeTel_ = t;
  analyzeTelRecorded_ = true;
}

void TelemetryRecorder::addAnalyzeMs(double ms) { analyzeMs_ += ms; analyzeRecorded_ = true; }
void TelemetryRecorder::addWalkMs(double ms) { walkMs_ += ms; walkRecorded_ = true; }
void TelemetryRecorder::addRevalidateMs(double ms) { revalidateMs_ += ms; revalidateRecorded_ = true; }
void TelemetryRecorder::addIncrementalMs(double ms) { incrementalMs_ += ms; incrementalRecorded_ = true; }
void TelemetryRecorder::addCandidateIndexMs(double ms) { candidateIndexMs_ += ms; candidateIndexRecorded_ = true; }
void TelemetryRecorder::addSimilarityMs(double ms) { similarityMs_ += ms; similarityRecorded_ = true; }
void TelemetryRecorder::addPersistenceMs(double ms) { persistenceMs_ += ms; persistenceRecorded_ = true; }
void TelemetryRecorder::addImage(std::uint64_t bytes, double decodeMs, double hashMs, double cropMs, bool usedGpu, const std::string& path) {
  imgCount_.fetch_add(1, std::memory_order_relaxed);
  imgBytes_.fetch_add(bytes, std::memory_order_relaxed);
  if (usedGpu) imgGpu_.fetch_add(1, std::memory_order_relaxed);
  imgDecodeNs_.fetch_add((long long)(decodeMs * 1e6), std::memory_order_relaxed);
  imgHashNs_.fetch_add((long long)(hashMs * 1e6), std::memory_order_relaxed);
  imgCropNs_.fetch_add((long long)(cropMs * 1e6), std::memory_order_relaxed);
  imgDecodeRecorded_ = imgHashRecorded_ = imgCropRecorded_ = true;
  // P4: the slowest list ranks files that did real work only. Zero-cost
  // entries (failed decodes measured as 0ms) would pad the list and hide
  // genuine algorithmic outliers, which is what this list exists to find.
  if (decodeMs + hashMs + cropMs <= 0.0) return;
  SlowFile item{path, decodeMs + hashMs + cropMs, bytes, 0, 0};
  std::lock_guard<std::mutex> g(slowMutex_);
  appendSlow(slowImages_, std::move(item));
}
void TelemetryRecorder::beginImageBatch(std::size_t items) {
  imgBatchCount_.fetch_add(1, std::memory_order_relaxed);
  imgBatchItems_.fetch_add(items, std::memory_order_relaxed);
  imgBatchMaxDepth_.store(1, std::memory_order_relaxed);
}
void TelemetryRecorder::endImageBatch() {}
void TelemetryRecorder::addImagePackMs(double ms) { imgPackNs_.fetch_add((long long)(ms * 1e6), std::memory_order_relaxed); }
void TelemetryRecorder::addImageCpuHashMs(double ms) { imgCpuHashNs_.fetch_add((long long)(ms * 1e6), std::memory_order_relaxed); }
void TelemetryRecorder::addGpuBatchMs(double ms) {
  imgGpuNs_.fetch_add((long long)(ms * 1e6), std::memory_order_relaxed);
  imgGpuRecorded_ = true;
}
void TelemetryRecorder::addImageGpuQueueMs(double ms) {
  imgQueueWaitNs_.fetch_add((long long)(ms * 1e6), std::memory_order_relaxed);
  imgQueueWaitRecorded_ = true;
}
void TelemetryRecorder::addImageTransferMs(double ms) {
  imgTransferNs_.fetch_add((long long)(ms * 1e6), std::memory_order_relaxed);
  imgTransferRecorded_ = true;
}
void TelemetryRecorder::addImageExecMs(double ms) {
  imgExecNs_.fetch_add((long long)(ms * 1e6), std::memory_order_relaxed);
  imgExecRecorded_ = true;
}
// D4a: one call per GPU batch that the backend could actually time. The
// recorded flags are set only here, so a backend that cannot time anything
// leaves every D4a metric at not_measured.
void TelemetryRecorder::addImageGpuDeviceTiming(double h2dDeviceMs, double kernelDeviceMs,
                                                double d2hDeviceMs, double syncHostMs,
                                                double hostTotalMs, bool usedGpu) {
  if (usedGpu) {
    imgH2dNs_.fetch_add((long long)(h2dDeviceMs * 1e6), std::memory_order_relaxed);
    imgKernelNs_.fetch_add((long long)(kernelDeviceMs * 1e6), std::memory_order_relaxed);
    imgD2hNs_.fetch_add((long long)(d2hDeviceMs * 1e6), std::memory_order_relaxed);
    imgSyncNs_.fetch_add((long long)(syncHostMs * 1e6), std::memory_order_relaxed);
    imgGpuTotalNs_.fetch_add((long long)(hostTotalMs * 1e6), std::memory_order_relaxed);
    imgGpuTimedBatches_.fetch_add(1, std::memory_order_relaxed);
    imgH2dRecorded_ = imgKernelRecorded_ = imgD2hRecorded_ = true;
    imgSyncRecorded_ = imgGpuTotalRecorded_ = true;
  }
}
void TelemetryRecorder::recordWalkerEnqueue(std::size_t depthAfterPush) {
  walkQueued_.fetch_add(1, std::memory_order_relaxed);
  std::uint64_t prev = walkMaxDepth_.load(std::memory_order_relaxed);
  while ((std::uint64_t)depthAfterPush > prev &&
         !walkMaxDepth_.compare_exchange_weak(prev, (std::uint64_t)depthAfterPush,
                                              std::memory_order_relaxed)) {}
}
void TelemetryRecorder::recordWalkerDequeue(std::size_t depthAfterPop) {
  (void)depthAfterPop;
  walkDequeued_.fetch_add(1, std::memory_order_relaxed);
}
void TelemetryRecorder::noteWalkerStarved() {
  walkStarved_.fetch_add(1, std::memory_order_relaxed);
}
void TelemetryRecorder::setWalkerCapacity(std::size_t capacity) {
  walkCapacity_.store((std::uint64_t)capacity, std::memory_order_relaxed);
}
void TelemetryRecorder::noteWalkerBlocked() {
  walkBlocked_.fetch_add(1, std::memory_order_relaxed);
}
void TelemetryRecorder::recordVideoRange(std::size_t files, double maxFileMs) {
  vidRangeCount_.fetch_add(1, std::memory_order_relaxed);
  vidRangeFiles_.fetch_add((std::uint64_t)files, std::memory_order_relaxed);
  const long long ns = (long long)(maxFileMs * 1e6);
  long long prev = vidRangeMaxNs_.load(std::memory_order_relaxed);
  while (ns > prev && !vidRangeMaxNs_.compare_exchange_weak(prev, ns, std::memory_order_relaxed)) {}
}
void TelemetryRecorder::addVideo(std::uint64_t bytes, double durationSec, double buildMs, std::size_t frames, const std::string& path,
                                 std::size_t decodedFrames, std::size_t sampledFrames, bool cacheHit) {
  vidCount_.fetch_add(1, std::memory_order_relaxed);
  vidBytes_.fetch_add(bytes, std::memory_order_relaxed);
  vidFrames_.fetch_add(frames, std::memory_order_relaxed);
  // decodedFrames/sampledFrames stay separate counts (Node A): a cache hit
  // decodes nothing (decoded 0 is measured), while an unknown sample plan is
  // NotMeasured, never numeric zero.
  if (decodedFrames != kFramesNotProvided) {
    vidDecodedFrames_.fetch_add(decodedFrames, std::memory_order_relaxed);
    vidDecodedRecorded_ = true;
  }
  if (sampledFrames != kFramesNotProvided) {
    vidSampledFrames_.fetch_add(sampledFrames, std::memory_order_relaxed);
    vidSampledRecorded_ = true;
  }
  vidBuildNs_.fetch_add((long long)(buildMs * 1e6), std::memory_order_relaxed);
  double prev = vidPlaySec_.load(std::memory_order_relaxed);
  while (!vidPlaySec_.compare_exchange_weak(prev, prev + durationSec, std::memory_order_relaxed)) {}
  // P4: the slowest list ranks files that did real work only. A cache-hit
  // rebuild decodes nothing and costs ~0ms, so it would pad the list and hide
  // genuine algorithmic outliers. The engine flags this explicitly (cacheHit);
  // a caller that omits decoded/sampled is a real analysis and stays listed.
  if (cacheHit) return;
  SlowFile item{path, buildMs, bytes, durationSec, frames};
  std::lock_guard<std::mutex> g(slowMutex_);
  appendSlow(slowVideos_, std::move(item));
}
void TelemetryRecorder::addVideoPlan(int decision, int reason, bool sparseAccepted, bool sparseRejected,
                                     long long sparseSeeks, long long sparseDecoded, long long landingViolations) {
    vidPlanTotal_.fetch_add(1, std::memory_order_relaxed);
    // decision 0 = SequentialPreferred, 1 = SparseSeekCandidate, 2 = SparseSeekUnavailable
    if (decision == 1) vidPlanSparse_.fetch_add(1, std::memory_order_relaxed);
    else if (decision == 2) vidPlanUnavail_.fetch_add(1, std::memory_order_relaxed);
    else vidPlanSeq_.fetch_add(1, std::memory_order_relaxed);
    if (sparseAccepted) vidPlanAccepted_.fetch_add(1, std::memory_order_relaxed);
    if (sparseRejected) vidPlanRejected_.fetch_add(1, std::memory_order_relaxed);
    vidPlanLandingViol_.fetch_add((std::uint64_t)(landingViolations > 0 ? 1 : 0), std::memory_order_relaxed);
    vidPlanSparseSeeks_.fetch_add(sparseSeeks, std::memory_order_relaxed);
    vidPlanSparseDecoded_.fetch_add(sparseDecoded, std::memory_order_relaxed);
    if (reason >= 0 && reason < kPlanReasonCount)
        vidPlanReason_[reason].fetch_add(1, std::memory_order_relaxed);
}

void TelemetryRecorder::addVideoGpu(bool used, bool fallback, double gpuMs) {
  if (used) vidGpu_.fetch_add(1, std::memory_order_relaxed);
  if (fallback) vidGpuFallback_.fetch_add(1, std::memory_order_relaxed);
  vidGpuNs_.fetch_add((long long)(gpuMs * 1e6), std::memory_order_relaxed);
}
void TelemetryRecorder::sampleOnce(double tMs) {
  ResourceSample s;
  s.tMs = tMs;
  s.wallTime = localTimeStr();
  s.gpu = gpuActiveFn_ ? gpuActiveFn_() : false;
#ifdef _WIN32
  FILETIME fc, fe, fk, fu;
  if (GetProcessTimes(GetCurrentProcess(), &fc, &fe, &fk, &fu)) {
    ULARGE_INTEGER k, u;
    k.LowPart = fk.dwLowDateTime; k.HighPart = fk.dwHighDateTime;
    u.LowPart = fu.dwLowDateTime; u.HighPart = fu.dwHighDateTime;
    FILETIME fi, sk, su;
    if (GetSystemTimes(&fi, &sk, &su)) {
      ULARGE_INTEGER si, skk, suu;
      si.LowPart = fi.dwLowDateTime; si.HighPart = fi.dwHighDateTime;
      skk.LowPart = sk.dwLowDateTime; skk.HighPart = sk.dwHighDateTime;
      suu.LowPart = su.dwLowDateTime; suu.HighPart = su.dwHighDateTime;
      const long long tick = nowNs();
      if (prevTick_ >= 0 && tick > prevTick_) {
        const double wall = (double)(tick - prevTick_) / 1e9;
        const double proc = (double)((long long)(k.QuadPart - prevProcK_) + (long long)(u.QuadPart - prevProcU_)) / 1e7;
        s.cpuProc = 100.0 * proc / (wall * cpuCount_);
        const long long tot = (long long)(si.QuadPart - prevSysI_) + (long long)(skk.QuadPart - prevSysK_) + (long long)(suu.QuadPart - prevSysU_);
        if (tot > 0) s.cpuSys = 100.0 * (1.0 - (double)(long long)(si.QuadPart - prevSysI_) / (double)tot);
      }
      prevProcK_ = (long long)k.QuadPart; prevProcU_ = (long long)u.QuadPart;
      prevSysI_ = (long long)si.QuadPart; prevSysK_ = (long long)skk.QuadPart; prevSysU_ = (long long)suu.QuadPart;
      prevTick_ = tick;
    }
  }
  PROCESS_MEMORY_COUNTERS pm{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pm, sizeof(pm)))
    s.memMB = (double)pm.WorkingSetSize / (1024.0 * 1024.0);
  if (diskQuery_) {
    if (PdhCollectQueryData(static_cast<PDH_HQUERY>(diskQuery_)) == ERROR_SUCCESS) {
      PDH_FMT_COUNTERVALUE v{};
      if (PdhGetFormattedCounterValue(static_cast<PDH_HCOUNTER>(diskReadCounter_), PDH_FMT_DOUBLE, nullptr, &v) == ERROR_SUCCESS &&
          v.CStatus == ERROR_SUCCESS)
        s.ioReadBps = v.doubleValue;
      if (PdhGetFormattedCounterValue(static_cast<PDH_HCOUNTER>(diskWriteCounter_), PDH_FMT_DOUBLE, nullptr, &v) == ERROR_SUCCESS &&
          v.CStatus == ERROR_SUCCESS)
        s.ioWriteBps = v.doubleValue;
    }
  }
  IO_COUNTERS io{};
  if (GetProcessIoCounters(GetCurrentProcess(), &io)) {
    procIoReadBytes_.store(io.ReadTransferCount, std::memory_order_relaxed);
    procIoWriteBytes_.store(io.WriteTransferCount, std::memory_order_relaxed);
    procIoReadOps_.store(io.ReadOperationCount, std::memory_order_relaxed);
    procIoWriteOps_.store(io.WriteOperationCount, std::memory_order_relaxed);
  }
#else
  (void)tMs;
#endif
  std::lock_guard<std::mutex> g(sampleMutex_);
  if (samples_.size() < kMaxSamples) samples_.push_back(s);
  else samplesTruncated_ = true;
}
void TelemetryRecorder::startSampler(std::function<bool()> gpuActive) {
  stopSampler();
  gpuActiveFn_ = std::move(gpuActive);
  sampling_.store(true, std::memory_order_relaxed);
  samplerStarted_ = true;
  const long long t0 = nowNs();
  sampler_ = std::thread([this, t0]() {
    for (;;) {
      const double tMs = (double)(nowNs() - t0) / 1e6;
      sampleOnce(tMs);
      for (int i = 0; i < kSampleMs / 25; ++i) {
        if (!sampling_.load(std::memory_order_relaxed)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
      }
    }
  });
}
void TelemetryRecorder::stopSampler() {
  sampling_.store(false, std::memory_order_relaxed);
  if (sampler_.joinable()) sampler_.join();
  gpuActiveFn_ = nullptr;
  closeDiskCounters();
}
void TelemetryRecorder::finalize(bool completed, std::size_t scanned, std::size_t analyzed, std::size_t unchanged,
                                 std::size_t candidates, std::size_t matches, std::size_t groups, double reductionPct,
                                 std::uint64_t gpuImages, std::uint64_t gpuFallback) {
    if (finished_) return;
    stopSampler();
    wallMs_ = (double)(nowNs() - startTick_) / 1e6;
    finishedAt_ = localTimeStr();
    // Guarantee at least one resource sample. A fast scan can finish before
    // the 250 ms sampler thread fires, leaving series empty and wallTime
    // unverifiable. One synchronous sample at finish time is a real
    // measurement (CPU, memory, IO counters as they stand), not a fill-in.
    // sampleOnce() takes sampleMutex_ itself, so the emptiness check must not
    // hold the lock across the call.
    bool needBaseline = false;
    {
      std::lock_guard<std::mutex> g(sampleMutex_);
      needBaseline = samples_.empty();
    }
    if (needBaseline) sampleOnce(wallMs_);
  completed_ = completed;
  if (completionReason_.empty()) completionReason_ = completed ? "completed" : "cancelled";
  scanned_ = scanned; analyzed_ = analyzed; unchanged_ = unchanged;
  candidates_ = candidates; matches_ = matches; groups_ = groups;
  reductionPct_ = reductionPct;
  gpuImages_ = gpuImages; gpuFallback_ = gpuFallback;
  finished_ = true;
}
std::string SchedulerTelemetry::toJson() const {
  std::ostringstream o;
  o << std::fixed << std::setprecision(3);
  o << "{\"state\":\"" << measureStateName(state) << "\""
    << ",\"initialCpuCapacity\":" << initialCpuCapacity << ",\"initialGpuCapacity\":" << initialGpuCapacity
    << ",\"currentCpuCapacity\":" << currentCpuCapacity << ",\"currentGpuCapacity\":" << currentGpuCapacity
    << ",\"cpuWorkShare\":" << cpuWorkShare << ",\"gpuWorkShare\":" << gpuWorkShare
    << ",\"cpuQueueDepth\":" << cpuQueueDepth << ",\"gpuQueueDepth\":" << gpuQueueDepth
    << ",\"cpuQueueWaitMs\":" << cpuQueueWaitMs << ",\"gpuQueueWaitMs\":" << gpuQueueWaitMs
    << ",\"adjustmentCount\":" << adjustmentCount << ",\"throttlingEvents\":" << throttlingEvents
    << ",\"externalLoadThrottling\":" << externalLoadThrottling
    << ",\"selectedBackend\":\"" << TelemetryRecorder::escapeJson(selectedBackend) << "\""
    << ",\"backendFallbacks\":" << backendFallbacks << "}";
  return o.str();
}
std::string CalibrationTelemetry::toJson() const {
  std::ostringstream o;
  o << std::fixed << std::setprecision(3);
  o << "{\"state\":\"" << measureStateName(state) << "\""
    << ",\"started\":" << (started ? "true" : "false") << ",\"completed\":" << (completed ? "true" : "false")
    << ",\"durationMs\":" << durationMs << ",\"confidence\":" << confidence
    << ",\"cpuThroughput\":" << cpuThroughput << ",\"gpuThroughput\":" << gpuThroughput
    << ",\"resizeThroughput\":" << resizeThroughput << ",\"decodeThroughput\":" << decodeThroughput
    << ",\"transferCostMs\":" << transferCostMs << ",\"queueLatencyMs\":" << queueLatencyMs
    << ",\"cpuState\":\"" << measureStateName(cpuState) << "\""
    << ",\"gpuState\":\"" << measureStateName(gpuState) << "\""
    << ",\"resizeState\":\"" << measureStateName(resizeState) << "\""
    << ",\"decodeState\":\"" << measureStateName(decodeState) << "\""
    << ",\"transferState\":\"" << measureStateName(transferState) << "\""
    << ",\"queueState\":\"" << measureStateName(queueState) << "\""
    << ",\"profileId\":\"" << TelemetryRecorder::escapeJson(profileId) << "\""
    << ",\"profileVersion\":\"" << TelemetryRecorder::escapeJson(profileVersion) << "\"}";
  return o.str();
}
std::string TelemetryRecorder::toJson() const {
  const auto imgN = imgCount_.load(), vidN = vidCount_.load();
  const double imgDecodeMs = (double)imgDecodeNs_.load() / 1e6;
  const double imgHashMs = (double)imgHashNs_.load() / 1e6;
  const double imgCropMs = (double)imgCropNs_.load() / 1e6;
  const double vidBuildMs = (double)vidBuildNs_.load() / 1e6;
  const double vidGpuMs = (double)vidGpuNs_.load() / 1e6;
  const double playSec = vidPlaySec_.load();
  double cpuMean = 0, cpuMax = 0, cpuVar = 0, sysMean = 0, memMax = 0, gpuDuty = 0;
  double ioReadMax = 0, ioWriteMax = 0, ioReadSum = 0, ioWriteSum = 0;
  long long idleRun = 0, idleMax = 0;
  std::size_t nS = 0, gpuOn = 0;
  bool truncated = false;
  {
    std::lock_guard<std::mutex> g(sampleMutex_);
    nS = samples_.size();
    truncated = samplesTruncated_;
    double sum = 0, sumSq = 0, sysSum = 0;
    for (const auto& s : samples_) {
      sum += s.cpuProc; sumSq += s.cpuProc * s.cpuProc;
      sysSum += s.cpuSys;
      cpuMax = std::max(cpuMax, s.cpuProc);
      memMax = std::max(memMax, s.memMB);
      ioReadMax = std::max(ioReadMax, s.ioReadBps);
      ioWriteMax = std::max(ioWriteMax, s.ioWriteBps);
      ioReadSum += s.ioReadBps;
      ioWriteSum += s.ioWriteBps;
      if (s.gpu) { ++gpuOn; idleRun = 0; }
      else { ++idleRun; idleMax = std::max(idleMax, idleRun); }
    }
    if (nS > 0) {
      cpuMean = sum / nS; sysMean = sysSum / nS;
      cpuVar = sumSq / nS - cpuMean * cpuMean;
      gpuDuty = 100.0 * (double)gpuOn / (double)nS;
    }
  }
  const double cpuStd = cpuVar > 0 ? std::sqrt(cpuVar) : 0;
  std::ostringstream o;
  o << std::fixed << std::setprecision(3);
  o << "{\"meta\":{\"app\":\"MediaSimilarityFinder\",\"build\":\"" << escapeJson(cfg_.build) << "\","
    << "\"engine\":\"" << escapeJson(cfg_.engine) << "\",\"db\":\"" << escapeJson(cfg_.db) << "\",\"purpose\":\"" << telemetryPurposeName(cfg_.purpose) << "\","
    << "\"startedAt\":\"" << startedAt_ << "\",\"completed\":" << (completed_ ? "true" : "false") << ","
    // Absolute finish, recorded at finalize(), never derived from wallMs.
    << "\"finishedAt\":\"" << finishedAt_ << "\""
    // Cancellation phase: where the run was when Stop landed. Empty unless
    // the run actually cancelled, so a normal run carries no phase claim.
    << ",\"cancelledDuring\":\"" << escapeJson(cancelled_ ? phase_ : std::string()) << "\","
    // root is a location. dataset is the identity of the bytes under it, so
    // two runs can prove they used the same input. Fingerprint is null unless
    // it was actually measured; a missing root stays not_available, never 0.
    << "\"root\":\"" << escapeJson(cfg_.root) << "\""
    << ",\"dataset\":{\"state\":\"" << escapeJson(datasetFp_.state) << "\""
    << ",\"fingerprintVersion\":" << kDatasetFingerprintVersion
    << ",\"fingerprint\":"
    << (datasetFp_.fingerprint.empty() ? std::string("null")
                                       : ("\"" + datasetFp_.fingerprint + "\""))
    << ",\"fileCount\":" << datasetFp_.fileCount
    << ",\"totalBytes\":" << datasetFp_.totalBytes
    // Fingerprint-phase telemetry. durationMs measures the whole walk+hash;
    // bytesRead counts actually hashed bytes (equals totalBytes on success).
    // Lets a multi-GB fingerprint phase be diagnosed from the log alone.
    << ",\"durationMs\":" << datasetFp_.durationMs
    << ",\"bytesRead\":" << datasetFp_.bytesRead << "}"
    << ",\"schemaVersion\":" << kBenchmarkSchemaVersion << ",\"runId\":\"" << escapeJson(runId_) << "\""
    << ",\"cancelled\":" << (cancelled_ ? "true" : "false") << ",\"paused\":" << (paused_ ? "true" : "false")
    << ",\"failed\":" << (failed_ ? "true" : "false") << ",\"failedStage\":\"" << escapeJson(failedStage_) << "\""
    << ",\"completionReason\":\"" << escapeJson(completionReason_) << "\"},";
  o << "\"config\":{\"distance\":" << cfg_.distance << ",\"cpuWorkers\":" << cfg_.cpuWorkers
    << ",\"detail\":" << (cfg_.detail ? "true" : "false")
    << ",\"gpuEnabled\":" << (cfg_.gpuEnabled ? "true" : "false") << ",\"gpuBatch\":" << cfg_.gpuBatch
    << ",\"gpuBackend\":\"" << escapeJson(cfg_.gpuBackend) << "\""
    << ",\"scanImages\":" << (cfg_.scanImages ? "true" : "false") << ",\"scanVideos\":" << (cfg_.scanVideos ? "true" : "false")
    << ",\"cudaAvailable\":" << (cfg_.cudaAvailable ? "true" : "false") << "},";
  o << "\"summary\":{\"wallMs\":" << wallMs_ << ",\"walkMs\":" << walkMs_ << ",\"revalidateMs\":" << revalidateMs_
    << ",\"imageStageMs\":" << imageStageMs_ << ",\"videoStageMs\":" << videoStageMs_ << ",\"analyzeMs\":" << analyzeMs_
    << ",\"scanned\":" << scanned_ << ",\"analyzed\":" << analyzed_ << ",\"unchanged\":" << unchanged_
    // Total scanned bytes, exactly images.bytes + videos.bytes. Distinct from
    // dataset.totalBytes, which sizes the fingerprint input, not the analysis.
    << ",\"totalScannedBytes\":" << (imgBytes_.load() + vidBytes_.load())
    << ",\"filesPerSec\":" << (wallMs_ > 0 ? 1000.0 * (double)analyzed_ / wallMs_ : 0) << "},";
  o << "\"images\":{\"count\":" << imgN << ",\"scanned\":" << imgScanned_
    << ",\"scannedState\":\"" << measureStateName(kindBreakdownSet_ ? MeasureState::Measured : MeasureState::NotMeasured) << "\""
    << ",\"bytes\":" << imgBytes_.load()
    << ",\"gpuHashed\":" << imgGpu_.load() << ",\"cpuHashed\":" << (imgN - imgGpu_.load())
    << ",\"decodeMs\":" << imgDecodeMs << ",\"hashMs\":" << imgHashMs << ",\"cropMs\":" << imgCropMs
    << ",\"gpuBatchMs\":" << (double)imgGpuNs_.load() / 1e6
    << ",\"gpuQueueWaitMs\":" << (double)imgQueueWaitNs_.load() / 1e6
    << ",\"gpuTransferMs\":" << (double)imgTransferNs_.load() / 1e6
    << ",\"gpuExecMs\":" << (double)imgExecNs_.load() / 1e6
    << ",\"packMs\":" << (double)imgPackNs_.load() / 1e6
    << ",\"cpuHashMs\":" << (double)imgCpuHashNs_.load() / 1e6
    << ",\"batchCount\":" << imgBatchCount_.load()
    << ",\"batchItems\":" << imgBatchItems_.load()
    << ",\"batchMaxDepth\":" << imgBatchMaxDepth_.load()
    << ",\"batchState\":\"" << measureStateName(imgBatchCount_.load() > 0 ? MeasureState::Measured : MeasureState::NotMeasured) << "\""
    << ",\"decodeState\":\"" << measureStateName(imgDecodeRecorded_ ? MeasureState::Measured : MeasureState::NotMeasured) << "\""
    << ",\"hashState\":\"" << measureStateName(imgHashRecorded_ ? MeasureState::Measured : MeasureState::NotMeasured) << "\""
    << ",\"gpuQueueWaitState\":\"" << measureStateName(imgQueueWaitRecorded_ ? MeasureState::Measured : MeasureState::NotMeasured) << "\""
    << ",\"gpuTransferState\":\"" << measureStateName(imgTransferRecorded_ ? MeasureState::Measured : MeasureState::NotMeasured) << "\""
    << ",\"gpuExecState\":\"" << measureStateName(imgExecRecorded_ ? MeasureState::Measured : MeasureState::NotMeasured) << "\""
    // D4a: backend-internal split. Device time per boundary, plus the host
    // wait that actually blocks the caller. Each keeps its own state so an
    // untimed backend stays not_measured instead of 0.
    << ",\"gpuH2dDeviceMs\":" << (double)imgH2dNs_.load() / 1e6
    << ",\"gpuKernelDeviceMs\":" << (double)imgKernelNs_.load() / 1e6
    << ",\"gpuD2hDeviceMs\":" << (double)imgD2hNs_.load() / 1e6
    << ",\"gpuSyncHostMs\":" << (double)imgSyncNs_.load() / 1e6
    << ",\"gpuHostTotalMs\":" << (double)imgGpuTotalNs_.load() / 1e6
    << ",\"gpuTimedBatches\":" << imgGpuTimedBatches_.load()
    << ",\"gpuH2dState\":\"" << measureStateName(imgH2dRecorded_ ? MeasureState::Measured : MeasureState::NotMeasured) << "\""
    << ",\"gpuKernelState\":\"" << measureStateName(imgKernelRecorded_ ? MeasureState::Measured : MeasureState::NotMeasured) << "\""
    << ",\"gpuD2hState\":\"" << measureStateName(imgD2hRecorded_ ? MeasureState::Measured : MeasureState::NotMeasured) << "\""
    << ",\"gpuSyncState\":\"" << measureStateName(imgSyncRecorded_ ? MeasureState::Measured : MeasureState::NotMeasured) << "\""
    << ",\"gpuHostTotalState\":\"" << measureStateName(imgGpuTotalRecorded_ ? MeasureState::Measured : MeasureState::NotMeasured) << "\""
    << ",\"meanDecodeMs\":" << (imgN ? imgDecodeMs / imgN : 0) << ",\"meanHashMs\":" << (imgN ? imgHashMs / imgN : 0)
    << ",\"slowest\":[";
  {
    std::lock_guard<std::mutex> g(slowMutex_);
    bool first = true;
    for (const auto& s : slowImages_) {
      if (!first) o << ",";
      first = false;
      o << "{\"path\":\"" << escapeJson(s.path) << "\",\"ms\":" << s.ms << ",\"bytes\":" << s.bytes << "}";
    }
  }
  o << "]},";
  o << "\"videos\":{\"count\":" << vidN << ",\"scanned\":" << vidScanned_
    << ",\"scannedState\":\"" << measureStateName(kindBreakdownSet_ ? MeasureState::Measured : MeasureState::NotMeasured) << "\""
    << ",\"bytes\":" << vidBytes_.load() << ",\"frames\":" << vidFrames_.load()
     << ",\"decodedFrames\":" << (vidDecodedRecorded_ ? std::to_string(vidDecodedFrames_.load()) : std::string("null"))
     << ",\"decodedFramesState\":\"" << measureStateName(vidDecodedRecorded_ ? MeasureState::Measured : MeasureState::NotMeasured) << "\""
     << ",\"sampledFrames\":" << (vidSampledRecorded_ ? std::to_string(vidSampledFrames_.load()) : std::string("null"))
     << ",\"sampledFramesState\":\"" << measureStateName(vidSampledRecorded_ ? MeasureState::Measured : MeasureState::NotMeasured) << "\""
     << ",\"playSec\":" << playSec << ",\"buildMs\":" << vidBuildMs
     << ",\"gpuVideos\":" << vidGpu_.load() << ",\"gpuFallbackVideos\":" << vidGpuFallback_.load() << ",\"gpuHashMs\":" << vidGpuMs
    // E-3B: the sampling planner. `sparseRejected` is reported next to
    // `sparseAccepted` so a fallback is never invisible, and the landing
    // violation count explains rejections rather than leaving them unexplained.
    << ",\"sampling\":{\"total\":" << vidPlanTotal_.load()
    << ",\"sequential\":" << vidPlanSeq_.load()
    << ",\"sparseSelected\":" << vidPlanSparse_.load()
    << ",\"unavailable\":" << vidPlanUnavail_.load()
    << ",\"sparseAccepted\":" << vidPlanAccepted_.load()
    << ",\"sparseRejected\":" << vidPlanRejected_.load()
    << ",\"landingViolationFiles\":" << vidPlanLandingViol_.load()
    << ",\"sparseSeeks\":" << vidPlanSparseSeeks_.load()
    << ",\"sparseDecodedFrames\":" << vidPlanSparseDecoded_.load()
    << ",\"reasons\":{";
    {
        static const char* kReasonNames[kPlanReasonCount] = {
            "ExactSparseVerified","CostNotAdvantageous","ExactnessUnverified","GOPUnknown",
            "HEVCFallback","SparseUnsupported","PlannerDisabled","SequentialSafe","reserved"
        };
        for (int i = 0; i < kPlanReasonCount; ++i)
            o << (i ? "," : "") << "\"" << kReasonNames[i] << "\":" << vidPlanReason_[i].load();
    }
    o << "}}"
    << ",\"meanBuildMs\":" << (vidN ? vidBuildMs / vidN : 0)
    << ",\"secPerPlayMin\":" << (playSec > 0 ? (vidBuildMs / 1000.0) / (playSec / 60.0) : 0)
    << ",\"secPerGB\":" << (vidBytes_.load() > 0 ? (vidBuildMs / 1000.0) / ((double)vidBytes_.load() / 1e9) : 0)
    << ",\"ranges\":" << vidRangeCount_.load() << ",\"rangeFiles\":" << vidRangeFiles_.load()
    << ",\"maxRangeFileMs\":" << (double)vidRangeMaxNs_.load() / 1e6
    << ",\"rangeState\":\"" << measureStateName(vidRangeCount_.load() > 0 ? MeasureState::Measured : MeasureState::NotMeasured) << "\""
    << ",\"slowest\":[";
  {
    std::lock_guard<std::mutex> g(slowMutex_);
    bool first = true;
    for (const auto& s : slowVideos_) {
      if (!first) o << ",";
      first = false;
      o << "{\"path\":\"" << escapeJson(s.path) << "\",\"ms\":" << s.ms << ",\"bytes\":" << s.bytes
        << ",\"durationSec\":" << s.durationSec << ",\"frames\":" << s.frames << "}";
    }
  }
  o << "]},";
  // D1b walker queue: unbounded by construction (producer never blocks);
  // queued/dequeued/maxDepth/starvedTicks recorded, never zero-filled.
  o << "\"walker\":{\"queued\":" << walkQueued_.load() << ",\"dequeued\":" << walkDequeued_.load()
    << ",\"maxDepth\":" << walkMaxDepth_.load() << ",\"starvedTicks\":" << walkStarved_.load()
    << ",\"capacity\":" << walkCapacity_.load() << ",\"blockedTicks\":" << walkBlocked_.load()
    << ",\"state\":\"" << measureStateName(walkQueued_.load() > 0 ? MeasureState::Measured : MeasureState::NotMeasured) << "\"},";
  const MeasureState sampleState = samplerStarted_ ? MeasureState::Measured : MeasureState::NotMeasured;
  const MeasureState diskState = diskWasAvailable_ ? MeasureState::Measured : MeasureState::NotAvailable;
  o << "\"resources\":{\"sampleMs\":" << kSampleMs << ",\"samples\":" << nS
    << ",\"sampleState\":\"" << measureStateName(sampleState) << "\""
    << ",\"cpuState\":\"" << measureStateName(sampleState) << "\""
    << ",\"gpuDutyState\":\"" << measureStateName(sampleState) << "\""
    << ",\"diskState\":\"" << measureStateName(diskState) << "\""
    << ",\"truncated\":" << (truncated ? "true" : "false") << ",\"cpuProcMean\":" << cpuMean
    << ",\"cpuProcMax\":" << cpuMax << ",\"cpuProcStd\":" << cpuStd << ",\"cpuSysMean\":" << sysMean
    << ",\"memMBMax\":" << memMax
    // Total physical RAM for judging the process peak on low-memory systems.
    // 0 means not captured, never a guess.
    << ",\"memSystemMB\":" << memSystemMB_
    << ",\"gpuDutyPct\":" << gpuDuty
    << ",\"gpuLongestIdleMs\":" << (double)idleMax * kSampleMs
    << ",\"diskVolume\":\"" << escapeJson(diskVolume_) << "\""
    << ",\"diskAvailable\":" << (diskWasAvailable_ ? "true" : "false")
    << ",\"ioReadBpsMax\":" << ioReadMax << ",\"ioReadBpsMean\":" << (nS ? ioReadSum / nS : 0)
    << ",\"ioWriteBpsMax\":" << ioWriteMax << ",\"ioWriteBpsMean\":" << (nS ? ioWriteSum / nS : 0)
    << ",\"procIoReadBytes\":" << procIoReadBytes_.load() << ",\"procIoWriteBytes\":" << procIoWriteBytes_.load()
    << ",\"procIoReadOps\":" << procIoReadOps_.load() << ",\"procIoWriteOps\":" << procIoWriteOps_.load()
    << ",\"series\":[";
  {
    std::lock_guard<std::mutex> g(sampleMutex_);
    bool first = true;
    for (const auto& s : samples_) {
      if (!first) o << ",";
      first = false;
      o << "[" << s.tMs << "," << s.cpuProc << "," << s.cpuSys << "," << s.memMB << "," << (s.gpu ? 1 : 0) << "," << s.ioReadBps << "," << s.ioWriteBps
        << ",\"" << s.wallTime << "\"]";
    }
  }
  o << "]},";
  o << "\"matches\":{\"candidates\":" << candidates_ << ",\"pairs\":" << streamedMatches_.load(std::memory_order_relaxed)
    << ",\"retainedPairs\":" << matches_ << ",\"groups\":" << groups_
    << ",\"reductionPct\":" << reductionPct_     << ",\"gpuImages\":" << gpuImages_ << ",\"gpuFallback\":" << gpuFallback_
    // Per-kind user-facing summary. A cluster never spans kinds, so the
    // per-kind groups partition the total. States follow the measured/
    // not_measured rule: unset means the scan never reached matching.
    << ",\"imagePairs\":" << imgPairs_ << ",\"imageGroups\":" << imgGroups_ << ",\"imageDuplicateFiles\":" << imgDupFiles_
    << ",\"videoPairs\":" << vidPairs_ << ",\"videoGroups\":" << vidGroups_ << ",\"videoDuplicateFiles\":" << vidDupFiles_
    << ",\"breakdownState\":\"" << measureStateName(kindBreakdownSet_ ? MeasureState::Measured : MeasureState::NotMeasured) << "\"},";
  // D9a: analyze internal split. The four stage times are non-overlapping
  // slices of the analyze total, so their sum never exceeds it. A stage that
  // was never entered is not_measured rather than 0 -- "measured as 0 ms"
  // and "did not run" are different claims.
  {
    const bool ran = analyzeTelRecorded_ && analyzeTel_.analyzeRan;
    const bool verifyLookups = analyzeTel_.verifyCacheHits + analyzeTel_.verifyDecodeMisses > 0;
    const bool hasVerify = ran && analyzeTel_.verifyCalls > 0;
    std::ostringstream a;
    a << std::fixed << std::setprecision(3);
    a << "\"analyze\":{\"state\":\"" << measureStateName(ran ? MeasureState::Measured : MeasureState::NotMeasured) << "\"";
    a << ",\"indexMs\":" << analyzeTel_.indexMs << ",\"indexState\":\""
      << measureStateName(ran ? MeasureState::Measured : MeasureState::NotMeasured) << "\"";
    a << ",\"scanMs\":" << analyzeTel_.scanMs << ",\"scanState\":\""
      << measureStateName(ran ? MeasureState::Measured : MeasureState::NotMeasured) << "\"";
    a << ",\"verifyMs\":" << analyzeTel_.verifyMs << ",\"verifyState\":\""
      << measureStateName(ran ? MeasureState::Measured : MeasureState::NotMeasured) << "\"";
    a << ",\"videoMs\":" << analyzeTel_.videoMs << ",\"videoState\":\""
      << measureStateName(analyzeTel_.videoStageEntered ? MeasureState::Measured : MeasureState::NotMeasured) << "\"";
    a << ",\"verifyCalls\":" << analyzeTel_.verifyCalls
      << ",\"verifyDecodeMisses\":" << analyzeTel_.verifyDecodeMisses
      << ",\"verifyCacheHits\":" << analyzeTel_.verifyCacheHits;
    a << ",\"verifyState_counters\":\"" << measureStateName(hasVerify ? MeasureState::Measured : MeasureState::NotMeasured) << "\"";
    // Derived only when their denominators are real. Dividing by zero would
    // manufacture a 0.0 that reads like a measurement.
    a << ",\"verifyHitRate\":";
    if (verifyLookups) a << (double)analyzeTel_.verifyCacheHits / (double)(analyzeTel_.verifyCacheHits + analyzeTel_.verifyDecodeMisses);
    else a << "null";
    a << ",\"verifyHitRateState\":\"" << measureStateName(verifyLookups ? MeasureState::Measured : MeasureState::NotMeasured) << "\"";
    a << ",\"msPerVerifyCall\":";
    if (hasVerify) a << (analyzeTel_.verifyMs / (double)analyzeTel_.verifyCalls);
    else a << "null";
    a << ",\"msPerVerifyCallState\":\"" << measureStateName(hasVerify ? MeasureState::Measured : MeasureState::NotMeasured) << "\"";
    a << ",\"ssimEvals\":" << analyzeTel_.ssimEvals
      << ",\"frameSsimEvals\":" << analyzeTel_.frameSsimEvals
      << ",\"videoTemporalPairs\":" << analyzeTel_.videoTemporalPairs;
    // D9c: exclusive breakdown of verifyMs. Each field is a disjoint code
    // region and verifyOtherMs is the remainder, so the seven times plus other
    // reconstruct verifyMs without double counting. verifySumMs is published
    // next to them so a consumer can check that identity instead of trusting
    // it, and verifyBreakdownOverMs exposes any drift.
    //
    // These come from an instrumented build, so they describe a build paying for
    // its own timers. Read them as a relative distribution, not a production
    // performance figure.
    const auto& v = analyzeTel_;
    const double vSum = v.verifyKeyMs + v.verifyDecodeMs + v.verifyCacheStoreMs
                      + v.verifyCacheCopyMs + v.verifyCropMs + v.verifyFlipMs
                      + v.verifyFrameSsimMs + v.verifyOtherMs;
    const bool breakdown = hasVerify && v.verifyBufferLookups > 0;
    auto stageMs = [&](const char* n, double ms) {
      a << ",\"" << n << "\":" << ms << ",\"" << n << "State\":\""
        << measureStateName(breakdown ? MeasureState::Measured : MeasureState::NotMeasured) << "\"";
    };
    stageMs("verifyKeyMs", v.verifyKeyMs);
    stageMs("verifyDecodeMs", v.verifyDecodeMs);
    stageMs("verifyCacheStoreMs", v.verifyCacheStoreMs);
    stageMs("verifyCacheCopyMs", v.verifyCacheCopyMs);
    stageMs("verifyCropMs", v.verifyCropMs);
    stageMs("verifyFlipMs", v.verifyFlipMs);
    stageMs("verifyFrameSsimMs", v.verifyFrameSsimMs);
    stageMs("verifyOtherMs", v.verifyOtherMs);
    a << ",\"verifySumMs\":" << vSum;
    a << ",\"verifyBreakdownOverMs\":" << (vSum - v.verifyMs);
    a << ",\"verifyBreakdownState\":\"" << measureStateName(breakdown ? MeasureState::Measured : MeasureState::NotMeasured) << "\"";
    a << ",\"verifyBufferLookups\":" << v.verifyBufferLookups
      << ",\"verifyQuickHashReads\":" << v.verifyQuickHashReads
      << ",\"verifyQuickHashBytes\":" << v.verifyQuickHashBytes
      << ",\"verifyDecodes\":" << v.verifyDecodes
      << ",\"verifyCacheCopies\":" << v.verifyCacheCopies
      << ",\"verifyCropCalls\":" << v.verifyCropCalls
      << ",\"verifyFlipCalls\":" << v.verifyFlipCalls;
    // D9d: the decode breakdown D9c could not produce. copyMs contains the real
    // image decode because WIC decompresses lazily inside CopyPixels; the field
    // comment says so and the report repeats it, so the number is never misread
    // as a plain memcpy.
    auto d9dStage = [&](const char* n, double ms) {
      a << ",\"" << n << "\":" << ms << ",\"" << n << "State\":\""
        << measureStateName(breakdown ? MeasureState::Measured : MeasureState::NotMeasured) << "\"";
    };
    const auto& d = v.decode;
    d9dStage("decodeTotalMs", d.totalMs);
    d9dStage("decodeComInitMs", d.comInitMs);
    d9dStage("decodeFactoryMs", d.factoryMs);
    d9dStage("decodeOpenMs", d.openMs);
    d9dStage("decodeMetadataMs", d.metadataMs);
    d9dStage("decodeOrientMs", d.orientMs);
    d9dStage("decodeResizeMs", d.resizeMs);
    d9dStage("decodeConvertMs", d.convertMs);
    d9dStage("decodeCopyMs", d.copyMs);
    d9dStage("decodePgmFallbackMs", d.pgmFallbackMs);
    a << ",\"decodeSubSumMs\":" << (d.comInitMs + d.factoryMs + d.openMs + d.metadataMs
        + d.orientMs + d.resizeMs + d.convertMs + d.copyMs);
    a << ",\"decodeCalls\":" << d.calls
      << ",\"decodeAspectCalls\":" << d.aspectCalls
      << ",\"decodeWicSucceeded\":" << d.wicSucceeded
      << ",\"decodePgmFallbacks\":" << d.pgmFallbacks
      << ",\"decodeOrientApplied\":" << d.orientApplied
      << ",\"decodeFailures\":" << d.failures;
    // Wait and hold are deliberately not summed: small hold plus large wait means
    // contention, small hold plus small wait means the lock is not the problem.
    a << ",\"cacheMutexWaitMs\":" << v.cacheMutexWaitMs
      << ",\"cacheMutexHoldMs\":" << v.cacheMutexHoldMs
      << ",\"cacheMutexAcquires\":" << v.cacheMutexAcquires;
    // D1: why open and factory are expensive. factory2Ms + factoryFallbackMs
    // reconstructs factoryMs, and the fallback count says whether the measured
    // factory cost is one activation or two. osFileOpenProbeMs is a reference
    // measurement, deliberately NOT part of the decode path.
    a << ",\"factory2Attempts\":" << d.factory2Attempts
      << ",\"factory2Successes\":" << d.factory2Successes
      << ",\"factory2Fallbacks\":" << d.factory2Fallbacks
      << ",\"factory2FirstFailHr\":" << d.factory2FirstFailHr
      << ",\"factory2Ms\":" << d.factory2Ms
      << ",\"factoryFallbackMs\":" << d.factoryFallbackMs;
    a << ",\"factorySplitMs\":" << (d.factory2Ms + d.factoryFallbackMs)
      << ",\"factorySplitOverMs\":" << (d.factory2Ms + d.factoryFallbackMs - d.factoryMs);
    a << ",\"osFileOpenProbeMs\":" << d.osFileOpenProbeMs
      << ",\"osFileOpenProbeCount\":" << d.osFileOpenProbeCount
      << ",\"osFileOpenProbeFails\":" << d.osFileOpenProbeFails
      << ",\"openHrFailCount\":" << d.openHrFailCount
      << ",\"openHrFirstFailCode\":" << d.openHrFirstFailCode;
    // D3: the verify-miss path decodes the same file twice, and until now both
    // calls landed in the single DecodeTelemetry reported above, so the second
    // one's cost was invisible. These are the same stages split per call.
    // decodeSplitState is measured only when at least one of the two actually
    // ran; a run that never reached the decode path reports not_measured rather
    // than a zero that would read as "the second decode was free".
    {
      const auto& fu = v.decodeFull;
      const auto& as = v.decodeAspect;
      const bool split = (fu.calls + as.aspectCalls) > 0;
      const MeasureState st = split ? MeasureState::Measured : MeasureState::NotMeasured;
      a << ",\"decodeFullCalls\":" << fu.calls
        << ",\"decodeFullTotalMs\":" << fu.totalMs
        << ",\"decodeFullState\":\"" << measureStateName(st) << "\""
        // decodePreserveAspect() increments aspectCalls, never calls, so the
        // second accumulator's call count lives in aspectCalls. Using .calls
        // here would report a permanent zero.
        << ",\"decodeAspectOnlyCalls\":" << as.aspectCalls
        << ",\"decodeAspectOnlyTotalMs\":" << as.totalMs
        << ",\"decodeAspectOnlyState\":\"" << measureStateName(st) << "\"";
      auto d3Stage = [&](const char* n, double a1, double b1) {
        a << ",\"" << n << "\":" << (a1 + b1) << ",\"" << n << "State\":\""
          << measureStateName(split ? MeasureState::Measured : MeasureState::NotMeasured) << "\"";
      };
      d3Stage("decodeFullOpenMs", fu.openMs, 0.0);
      d3Stage("decodeAspectOpenMs", as.openMs, 0.0);
      d3Stage("decodeFullCopyMs", fu.copyMs, 0.0);
      d3Stage("decodeAspectCopyMs", as.copyMs, 0.0);
      d3Stage("decodeFullFactoryMs", fu.factoryMs, 0.0);
      d3Stage("decodeAspectFactoryMs", as.factoryMs, 0.0);
      // D3 identity: the two calls must reconstruct the combined D9d total.
      // A non-zero gap means a stage stopped being exclusive.
      a << ",\"decodeSplitOverMs\":" << (fu.totalMs + as.totalMs - d.totalMs)
        << ",\"decodeSplitState\":\"" << measureStateName(st) << "\"";
    }
    a << "},";
    o << a.str();
  }
  auto stageObj = [&](const char* name, double ms, bool recorded) {
    o << "\"" << name << "\":{\"ms\":" << ms << ",\"state\":\"" << measureStateName(recorded ? MeasureState::Measured : MeasureState::NotMeasured) << "\"},";
  };
  o << "\"stages\":{";
  stageObj("enumeration", walkMs_, walkRecorded_);
  stageObj("incremental", incrementalMs_, incrementalRecorded_);
  stageObj("imageAnalysis", imageStageMs_, imageStageRecorded_);
  stageObj("videoAnalysis", videoStageMs_, videoStageRecorded_);
  stageObj("candidateIndex", candidateIndexMs_, candidateIndexRecorded_);
  stageObj("similarity", similarityMs_, similarityRecorded_);
  stageObj("persistence", persistenceMs_, persistenceRecorded_);
  stageObj("revalidation", revalidateMs_, revalidateRecorded_);
  o << "\"total\":{\"ms\":" << wallMs_ << ",\"state\":\"measured\"}},";
  o << "\"files\":{\"started\":" << filesStarted_ << ",\"completed\":" << filesCompleted_
    << ",\"remaining\":" << filesRemaining_
    << ",\"state\":\"" << measureStateName(fileProgressRecorded_ ? MeasureState::Measured : MeasureState::NotMeasured) << "\"},";
  o << "\"scheduler\":" << scheduler_.toJson() << ",";
  o << "\"calibration\":" << calibration_.toJson() << "}";
  return o.str();
}
}
