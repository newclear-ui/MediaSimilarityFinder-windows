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
void BenchmarkRecorder::reset() {
  stopSampler();
  started_ = false;
  finished_ = false;
}
void BenchmarkRecorder::abortUnfinished() {
  if (started_ && !finished_)
    finalize(false, 0, 0, 0, 0, 0, 0, 0.0, 0, 0);
}
void BenchmarkRecorder::start(const BenchmarkConfig& cfg) {
  stopSampler();
  cfg_ = cfg;
  startedAt_ = localTimeStr();
  startTick_ = nowNs();
  wallMs_ = walkMs_ = imageStageMs_ = videoStageMs_ = analyzeMs_ = revalidateMs_ = 0;
  imgCount_ = 0; imgBytes_ = 0; imgGpu_ = 0;
  imgDecodeNs_ = 0; imgHashNs_ = 0; imgCropNs_ = 0; imgGpuNs_ = 0;
  vidCount_ = 0; vidBytes_ = 0; vidFrames_ = 0; vidBuildNs_ = 0;
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
  reductionPct_ = 0;
  gpuImages_ = gpuFallback_ = 0;
  vidGpu_.store(0, std::memory_order_relaxed); vidGpuFallback_.store(0, std::memory_order_relaxed); vidGpuNs_.store(0, std::memory_order_relaxed);
  completed_ = false;
  finished_ = false;
  started_ = true;
}
void BenchmarkRecorder::addImageStageMs(double ms) { imageStageMs_ += ms; }
void BenchmarkRecorder::addVideoStageMs(double ms) { videoStageMs_ += ms; }
void BenchmarkRecorder::addAnalyzeMs(double ms) { analyzeMs_ += ms; }
void BenchmarkRecorder::addWalkMs(double ms) { walkMs_ += ms; }
void BenchmarkRecorder::addRevalidateMs(double ms) { revalidateMs_ += ms; }
void BenchmarkRecorder::addImage(std::uint64_t bytes, double decodeMs, double hashMs, double cropMs, bool usedGpu, const std::string& path) {
  imgCount_.fetch_add(1, std::memory_order_relaxed);
  imgBytes_.fetch_add(bytes, std::memory_order_relaxed);
  if (usedGpu) imgGpu_.fetch_add(1, std::memory_order_relaxed);
  imgDecodeNs_.fetch_add((long long)(decodeMs * 1e6), std::memory_order_relaxed);
  imgHashNs_.fetch_add((long long)(hashMs * 1e6), std::memory_order_relaxed);
  imgCropNs_.fetch_add((long long)(cropMs * 1e6), std::memory_order_relaxed);
  SlowFile item{path, decodeMs + hashMs + cropMs, bytes, 0, 0};
  std::lock_guard<std::mutex> g(slowMutex_);
  appendSlow(slowImages_, std::move(item));
}
void BenchmarkRecorder::addGpuBatchMs(double ms) {
  imgGpuNs_.fetch_add((long long)(ms * 1e6), std::memory_order_relaxed);
}
void BenchmarkRecorder::addVideo(std::uint64_t bytes, double durationSec, double buildMs, std::size_t frames, const std::string& path) {
  vidCount_.fetch_add(1, std::memory_order_relaxed);
  vidBytes_.fetch_add(bytes, std::memory_order_relaxed);
  vidFrames_.fetch_add(frames, std::memory_order_relaxed);
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
}
void BenchmarkRecorder::finalize(bool completed, std::size_t scanned, std::size_t analyzed, std::size_t unchanged,
                                 std::size_t candidates, std::size_t matches, std::size_t groups, double reductionPct,
                                 std::uint64_t gpuImages, std::uint64_t gpuFallback) {
  if (finished_) return;
  stopSampler();
  wallMs_ = (double)(nowNs() - startTick_) / 1e6;
  completed_ = completed;
  scanned_ = scanned; analyzed_ = analyzed; unchanged_ = unchanged;
  candidates_ = candidates; matches_ = matches; groups_ = groups;
  reductionPct_ = reductionPct;
  gpuImages_ = gpuImages; gpuFallback_ = gpuFallback;
  finished_ = true;
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
    << "\"root\":\"" << escapeJson(cfg_.root) << "\"},";
  o << "\"config\":{\"distance\":" << cfg_.distance << ",\"cpuWorkers\":" << cfg_.cpuWorkers
    << ",\"detail\":" << (cfg_.detail ? "true" : "false")
    << ",\"gpuEnabled\":" << (cfg_.gpuEnabled ? "true" : "false") << ",\"gpuBatch\":" << cfg_.gpuBatch
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
  o << "\"resources\":{\"sampleMs\":" << kSampleMs << ",\"samples\":" << nS
    << ",\"truncated\":" << (truncated ? "true" : "false") << ",\"cpuProcMean\":" << cpuMean
    << ",\"cpuProcMax\":" << cpuMax << ",\"cpuProcStd\":" << cpuStd << ",\"cpuSysMean\":" << sysMean
    << ",\"memMBMax\":" << memMax << ",\"gpuDutyPct\":" << gpuDuty
    << ",\"gpuLongestIdleMs\":" << (double)idleMax * kSampleMs << ",\"series\":[";
  {
    std::lock_guard<std::mutex> g(sampleMutex_);
    bool first = true;
    for (const auto& s : samples_) {
      if (!first) o << ",";
      first = false;
      o << "[" << s.tMs << "," << s.cpuProc << "," << s.cpuSys << "," << s.memMB << "," << (s.gpu ? 1 : 0) << "]";
    }
  }
  o << "]},";
  o << "\"matches\":{\"candidates\":" << candidates_ << ",\"pairs\":" << streamedMatches_.load(std::memory_order_relaxed)
    << ",\"retainedPairs\":" << matches_ << ",\"groups\":" << groups_
    << ",\"reductionPct\":" << reductionPct_ << ",\"gpuImages\":" << gpuImages_ << ",\"gpuFallback\":" << gpuFallback_ << "}}";
  return o.str();
}
}
