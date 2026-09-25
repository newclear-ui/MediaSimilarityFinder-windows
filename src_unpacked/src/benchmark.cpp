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
  if (v.size() >= BenchmarkRecorder::kSlowTop && item.ms <= v.back().ms) return;
  v.push_back(std::move(item));
  std::sort(v.begin(), v.end(), [](const SlowFile& a, const SlowFile& b) { return a.ms > b.ms; });
  if (v.size() > BenchmarkRecorder::kSlowTop) v.pop_back();
}
} // namespace
std::string BenchmarkRecorder::escapeJson(const std::string& s) {
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
void BenchmarkRecorder::openDiskCounters() {
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
void BenchmarkRecorder::closeDiskCounters() {
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
void BenchmarkRecorder::reset() {
  stopSampler();
  started_ = false;
  finished_ = false;
}
void BenchmarkRecorder::setCancelled(const std::string& reason) {
  cancelled_ = true;
  if (!reason.empty()) completionReason_ = reason;
}
void BenchmarkRecorder::setPaused(bool paused) { paused_ = paused; }
void BenchmarkRecorder::setFailed(const std::string& stage, const std::string& reason) {
  failed_ = true;
  failedStage_ = stage;
  if (!reason.empty()) completionReason_ = reason;
}
void BenchmarkRecorder::setCompletionReason(const std::string& reason) { completionReason_ = reason; }
void BenchmarkRecorder::setFileProgress(std::size_t started, std::size_t completed, std::size_t remaining) {
  filesStarted_ = started; filesCompleted_ = completed; filesRemaining_ = remaining;
  fileProgressRecorded_ = true;
}
void BenchmarkRecorder::abortUnfinished() {
  if (started_ && !finished_) {
    setCancelled("aborted");
    finalize(false, 0, 0, 0, 0, 0, 0, 0.0, 0, 0);
  }
}
void BenchmarkRecorder::start(const BenchmarkConfig& cfg) {
  stopSampler();
  cfg_ = cfg;
  startedAt_ = localTimeStr();
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
  imgQueueWaitNs_ = 0; imgTransferNs_ = 0; imgExecNs_ = 0;
  imgDecodeRecorded_ = imgHashRecorded_ = imgCropRecorded_ = imgGpuRecorded_ = false;
  imgQueueWaitRecorded_ = imgTransferRecorded_ = imgExecRecorded_ = false;
  vidCount_ = 0; vidBytes_ = 0; vidFrames_ = 0; vidBuildNs_ = 0;
  vidDecodedFrames_ = 0; vidSampledFrames_ = 0;
  vidDecodedRecorded_ = vidSampledRecorded_ = false;
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
void BenchmarkRecorder::addImageStageMs(double ms) { imageStageMs_ += ms; imageStageRecorded_ = true; }
void BenchmarkRecorder::addVideoStageMs(double ms) { videoStageMs_ += ms; videoStageRecorded_ = true; }
void BenchmarkRecorder::addAnalyzeMs(double ms) { analyzeMs_ += ms; analyzeRecorded_ = true; }
void BenchmarkRecorder::addWalkMs(double ms) { walkMs_ += ms; walkRecorded_ = true; }
void BenchmarkRecorder::addRevalidateMs(double ms) { revalidateMs_ += ms; revalidateRecorded_ = true; }
void BenchmarkRecorder::addIncrementalMs(double ms) { incrementalMs_ += ms; incrementalRecorded_ = true; }
void BenchmarkRecorder::addCandidateIndexMs(double ms) { candidateIndexMs_ += ms; candidateIndexRecorded_ = true; }
void BenchmarkRecorder::addSimilarityMs(double ms) { similarityMs_ += ms; similarityRecorded_ = true; }
void BenchmarkRecorder::addPersistenceMs(double ms) { persistenceMs_ += ms; persistenceRecorded_ = true; }
void BenchmarkRecorder::addImage(std::uint64_t bytes, double decodeMs, double hashMs, double cropMs, bool usedGpu, const std::string& path) {
  imgCount_.fetch_add(1, std::memory_order_relaxed);
  imgBytes_.fetch_add(bytes, std::memory_order_relaxed);
  if (usedGpu) imgGpu_.fetch_add(1, std::memory_order_relaxed);
  imgDecodeNs_.fetch_add((long long)(decodeMs * 1e6), std::memory_order_relaxed);
  imgHashNs_.fetch_add((long long)(hashMs * 1e6), std::memory_order_relaxed);
  imgCropNs_.fetch_add((long long)(cropMs * 1e6), std::memory_order_relaxed);
  imgDecodeRecorded_ = imgHashRecorded_ = imgCropRecorded_ = true;
  SlowFile item{path, decodeMs + hashMs + cropMs, bytes, 0, 0};
  std::lock_guard<std::mutex> g(slowMutex_);
  appendSlow(slowImages_, std::move(item));
}
void BenchmarkRecorder::addGpuBatchMs(double ms) {
  imgGpuNs_.fetch_add((long long)(ms * 1e6), std::memory_order_relaxed);
  imgGpuRecorded_ = true;
}
void BenchmarkRecorder::addImageGpuQueueMs(double ms) {
  imgQueueWaitNs_.fetch_add((long long)(ms * 1e6), std::memory_order_relaxed);
  imgQueueWaitRecorded_ = true;
}
void BenchmarkRecorder::addImageTransferMs(double ms) {
  imgTransferNs_.fetch_add((long long)(ms * 1e6), std::memory_order_relaxed);
  imgTransferRecorded_ = true;
}
void BenchmarkRecorder::addImageExecMs(double ms) {
  imgExecNs_.fetch_add((long long)(ms * 1e6), std::memory_order_relaxed);
  imgExecRecorded_ = true;
}
void BenchmarkRecorder::addVideo(std::uint64_t bytes, double durationSec, double buildMs, std::size_t frames, const std::string& path,
                                 std::size_t decodedFrames, std::size_t sampledFrames) {
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
  SlowFile item{path, buildMs, bytes, durationSec, frames};
  std::lock_guard<std::mutex> g(slowMutex_);
  appendSlow(slowVideos_, std::move(item));
}
void BenchmarkRecorder::addVideoGpu(bool used, bool fallback, double gpuMs) {
  if (used) vidGpu_.fetch_add(1, std::memory_order_relaxed);
  if (fallback) vidGpuFallback_.fetch_add(1, std::memory_order_relaxed);
  vidGpuNs_.fetch_add((long long)(gpuMs * 1e6), std::memory_order_relaxed);
}
void BenchmarkRecorder::sampleOnce(double tMs) {
  ResourceSample s;
  s.tMs = tMs;
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
void BenchmarkRecorder::startSampler(std::function<bool()> gpuActive) {
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
void BenchmarkRecorder::stopSampler() {
  sampling_.store(false, std::memory_order_relaxed);
  if (sampler_.joinable()) sampler_.join();
  gpuActiveFn_ = nullptr;
  closeDiskCounters();
}
void BenchmarkRecorder::finalize(bool completed, std::size_t scanned, std::size_t analyzed, std::size_t unchanged,
                                 std::size_t candidates, std::size_t matches, std::size_t groups, double reductionPct,
                                 std::uint64_t gpuImages, std::uint64_t gpuFallback) {
  if (finished_) return;
  stopSampler();
  wallMs_ = (double)(nowNs() - startTick_) / 1e6;
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
    << ",\"selectedBackend\":\"" << BenchmarkRecorder::escapeJson(selectedBackend) << "\""
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
    << ",\"profileId\":\"" << BenchmarkRecorder::escapeJson(profileId) << "\""
    << ",\"profileVersion\":\"" << BenchmarkRecorder::escapeJson(profileVersion) << "\"}";
  return o.str();
}
std::string BenchmarkRecorder::toJson() const {
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
    << "\"engine\":\"" << escapeJson(cfg_.engine) << "\",\"db\":\"" << escapeJson(cfg_.db) << "\","
    << "\"startedAt\":\"" << startedAt_ << "\",\"completed\":" << (completed_ ? "true" : "false") << ","
    << "\"root\":\"" << escapeJson(cfg_.root) << "\""
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
    << ",\"filesPerSec\":" << (wallMs_ > 0 ? 1000.0 * (double)analyzed_ / wallMs_ : 0) << "},";
  o << "\"images\":{\"count\":" << imgN << ",\"bytes\":" << imgBytes_.load()
    << ",\"gpuHashed\":" << imgGpu_.load() << ",\"cpuHashed\":" << (imgN - imgGpu_.load())
    << ",\"decodeMs\":" << imgDecodeMs << ",\"hashMs\":" << imgHashMs << ",\"cropMs\":" << imgCropMs
    << ",\"gpuBatchMs\":" << (double)imgGpuNs_.load() / 1e6
    << ",\"gpuQueueWaitMs\":" << (double)imgQueueWaitNs_.load() / 1e6
    << ",\"gpuTransferMs\":" << (double)imgTransferNs_.load() / 1e6
    << ",\"gpuExecMs\":" << (double)imgExecNs_.load() / 1e6
    << ",\"decodeState\":\"" << measureStateName(imgDecodeRecorded_ ? MeasureState::Measured : MeasureState::NotMeasured) << "\""
    << ",\"hashState\":\"" << measureStateName(imgHashRecorded_ ? MeasureState::Measured : MeasureState::NotMeasured) << "\""
    << ",\"gpuQueueWaitState\":\"" << measureStateName(imgQueueWaitRecorded_ ? MeasureState::Measured : MeasureState::NotMeasured) << "\""
    << ",\"gpuTransferState\":\"" << measureStateName(imgTransferRecorded_ ? MeasureState::Measured : MeasureState::NotMeasured) << "\""
    << ",\"gpuExecState\":\"" << measureStateName(imgExecRecorded_ ? MeasureState::Measured : MeasureState::NotMeasured) << "\""
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
  o << "\"videos\":{\"count\":" << vidN << ",\"bytes\":" << vidBytes_.load() << ",\"frames\":" << vidFrames_.load()
     << ",\"decodedFrames\":" << (vidDecodedRecorded_ ? std::to_string(vidDecodedFrames_.load()) : std::string("null"))
     << ",\"decodedFramesState\":\"" << measureStateName(vidDecodedRecorded_ ? MeasureState::Measured : MeasureState::NotMeasured) << "\""
     << ",\"sampledFrames\":" << (vidSampledRecorded_ ? std::to_string(vidSampledFrames_.load()) : std::string("null"))
     << ",\"sampledFramesState\":\"" << measureStateName(vidSampledRecorded_ ? MeasureState::Measured : MeasureState::NotMeasured) << "\""
     << ",\"playSec\":" << playSec << ",\"buildMs\":" << vidBuildMs
     << ",\"gpuVideos\":" << vidGpu_.load() << ",\"gpuFallbackVideos\":" << vidGpuFallback_.load() << ",\"gpuHashMs\":" << vidGpuMs
    << ",\"meanBuildMs\":" << (vidN ? vidBuildMs / vidN : 0)
    << ",\"secPerPlayMin\":" << (playSec > 0 ? (vidBuildMs / 1000.0) / (playSec / 60.0) : 0)
    << ",\"secPerGB\":" << (vidBytes_.load() > 0 ? (vidBuildMs / 1000.0) / ((double)vidBytes_.load() / 1e9) : 0)
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
  const MeasureState sampleState = samplerStarted_ ? MeasureState::Measured : MeasureState::NotMeasured;
  const MeasureState diskState = diskWasAvailable_ ? MeasureState::Measured : MeasureState::NotAvailable;
  o << "\"resources\":{\"sampleMs\":" << kSampleMs << ",\"samples\":" << nS
    << ",\"sampleState\":\"" << measureStateName(sampleState) << "\""
    << ",\"cpuState\":\"" << measureStateName(sampleState) << "\""
    << ",\"gpuDutyState\":\"" << measureStateName(sampleState) << "\""
    << ",\"diskState\":\"" << measureStateName(diskState) << "\""
    << ",\"truncated\":" << (truncated ? "true" : "false") << ",\"cpuProcMean\":" << cpuMean
    << ",\"cpuProcMax\":" << cpuMax << ",\"cpuProcStd\":" << cpuStd << ",\"cpuSysMean\":" << sysMean
    << ",\"memMBMax\":" << memMax << ",\"gpuDutyPct\":" << gpuDuty
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
      o << "[" << s.tMs << "," << s.cpuProc << "," << s.cpuSys << "," << s.memMB << "," << (s.gpu ? 1 : 0) << "," << s.ioReadBps << "," << s.ioWriteBps << "]";
    }
  }
  o << "]},";
  o << "\"matches\":{\"candidates\":" << candidates_ << ",\"pairs\":" << streamedMatches_.load(std::memory_order_relaxed)
    << ",\"retainedPairs\":" << matches_ << ",\"groups\":" << groups_
    << ",\"reductionPct\":" << reductionPct_ << ",\"gpuImages\":" << gpuImages_ << ",\"gpuFallback\":" << gpuFallback_ << "},";
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
