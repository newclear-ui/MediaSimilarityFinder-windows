#pragma once
#include "scanner.h"
#include "database.h"
#include "scan_pipeline.h"
#include "resource_policy.h"
#include "video_fingerprint.h"
#include "gpu_backend.h"
#include "index_manager.h"
#include "candidate_index.h"
#include "benchmark.h"
#include <atomic>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstddef>
#include <cstdint>
namespace msf {
struct SearchMatch { std::string leftPath,rightPath; double percent=0; };
// Compact result reference for large-result consumers. It avoids duplicating file-path
// strings for every match; indices refer to MediaSearchEngine::files() for the scan.
struct SearchMatchRef { std::size_t leftIndex=0, rightIndex=0; double percent=0; };
struct SearchReport { std::size_t scanned=0, added=0, modified=0, unchanged=0, removed=0, analyzed=0, candidates=0, groups=0, gpuImages=0, gpuFallbackImages=0; double candidateReductionPercent=0; std::vector<SearchMatch> matches; bool completed=true; std::size_t indexedVideos=0, videoCandidatePairs=0, videoTemporalChecks=0, videoMatches=0; };
struct ScanControl {
 std::atomic_bool cancel{false};
 std::atomic_bool pause{false};
  std::function<void(std::size_t,std::size_t,const std::string&)> progress;
  // Directory-walk phase reporter (file count so far). Fires before any
  // analysis; lets the UI show listing progress on huge folders instead of
  // sitting at 0% with only disk I/O visible.
  std::function<void(std::size_t)> listing;
  std::function<void(std::size_t)> walked;
 // Optional streaming delivery for large result sets. When retainMatches is false,
 // SearchReport does not materialize the full match list in memory.
 std::function<void(const SearchMatch&)> onMatch;
 // Preferred low-allocation callback for large result sets / virtualized GUI models.
 std::function<void(const SearchMatchRef&)> onMatchRef;
  bool retainMatches=true;
  // 0 means unlimited when retainMatches is true. A positive value bounds the
  // report-owned match vector while onMatch can still receive every match.
  std::size_t maxRetainedMatches=0;
  // Paths excluded from analysis and results (e.g. GUI ignore list).
  // Compared against the canonical UTF-8 paths produced by the scanner.
  std::unordered_set<std::string> ignoredPaths;
  // Kind selection: set false to skip images or videos entirely (GUI option).
  bool scanImages=true, scanVideos=true;
  bool benchmarkEnabled=true;
  double revalidateMs=0;
  std::string buildVersion;
};
class MediaSearchEngine {
public:
  // Match-verdict generation, independent of the 0.9.2.x build numbers.
  // "M.m.p": patch = threshold/weight/gate tuning; minor = new stage or rule;
  // major = verdict architecture change. Any bump revalidates stored pairs on
  // the next scan (no rescan). Stored in the index DB (meta.engine_version).
   static constexpr const char* kEngineVersion = "1.2.0";
   static constexpr const char* kSamplingGeneration = "3";
  bool openIndex(const std::string& dbPath);
  bool openIndexForRoot(const std::string& rootPath, const std::string& applicationDirectory);
 // Persist the current duplicate-pair set so a later session can reload it.
 // Rows are replaced wholesale, but callers always save the full accumulation
 // (previously stored plus newly found), so pairs for files currently missing
 // from disk are preserved for resuming unfinished work. Safe on cancel: the
 // caller may pass a partial set to keep the last completed progress.
  bool saveMatches(const std::vector<SearchMatch>& matches);
 // Load the pairs persisted by the last completed scan. Paths use the same
 // canonical UTF-8 form as scan results, so reload maps 1:1 onto files().
  std::vector<SearchMatch> loadMatches() const;
 // Re-verify stored pairs against the current verdict logic without a full
 // rescan (no directory walk, no re-analysis). Images re-run bestMatch plus
 // the SSIM gate on DB fingerprints (grey-zone decodes only); videos re-run
 // the full pipeline verdict through the shared cache-backed temporal engine
 // (cache hits skip re-decode). Pairs whose files changed on disk or left the
 // index are kept, never dropped (union semantics for unfinished work). Drops
 // only pairs both files verify against at the scan-time line. Stamps the
 // current engine version unless cancelled (returns false, nothing stamped).
 // Pair-bounded and one-time per engine bump; cancel-aware. Counters may be null.
 bool revalidateMatches(ScanControl* control=nullptr, int* kept=nullptr, int* dropped=nullptr);
  // Releases the index database so its files can be moved or removed.
  // Required on Windows, where open files cannot be deleted.
  void close();
 void setResourcePolicy(const ResourcePolicy& policy){ policy_=policy; }
 void setExpensiveStageGuard(std::function<bool()> guard){ expensiveStageGuard_=std::move(guard); }
 ResourcePolicy resourcePolicy() const { return policy_; }
 SearchReport scan(const std::string& root, unsigned maxDistance=8, ScanControl* control=nullptr);
 bool upsertFingerprint(const std::string& path, std::uint64_t fingerprint, int kind, std::uint64_t size = 0, std::int64_t modified = 0, std::uint64_t mirrorFingerprint = 0, std::uint64_t crop4x3 = 0, std::uint64_t crop1x1 = 0, std::uint64_t crop9x16 = 0, std::uint64_t mirrorCrop4x3 = 0, std::uint64_t mirrorCrop1x1 = 0, std::uint64_t mirrorCrop9x16 = 0);
 bool removePath(const std::string& path);
 void rebuildCandidateIndexes();
 std::vector<SearchMatch> compareFingerprint(std::uint64_t fingerprint, int kind, double thresholdPercent, const std::string& excludePath = {}, std::uint64_t mirrorFingerprint = 0, std::uint64_t crop4x3 = 0, std::uint64_t crop1x1 = 0, std::uint64_t crop9x16 = 0, std::uint64_t mirrorCrop4x3 = 0, std::uint64_t mirrorCrop1x1 = 0, std::uint64_t mirrorCrop9x16 = 0) const;
const std::vector<MediaFile>& files() const { return files_; }
  // Live GPU-accelerated image counter. Images hashed on the CUDA backend
  // increment this during scan(); the GUI reads it for the status bar instead
  // of polling nvidia-smi, whose polling window cannot see sub-millisecond
  // batches. Reset to 0 at the start of every scan().
  std::uint64_t gpuImagesProcessed() const { return gpuImagesProcessed_.load(); }
  bool gpuActive() const { return gpuActive_.load(std::memory_order_relaxed); }
  bool hasBenchmark() const { return bench_.hasData(); }
  std::string benchmarkJson() const { return bench_.toJson(); }
  void beginBenchmark(const BenchmarkConfig& cfg, bool withSampler);
  void abortBenchmark() { bench_.abortUnfinished(); }
  bool getColorThumb(const std::string& path, int& w, int& h, std::vector<unsigned char>& bgra) const;
  bool getVideoThumb(const std::string& path, std::vector<unsigned char>& gray48) const;
  private:
  Database db_; std::vector<MediaFile> files_; ResourcePolicy policy_{}; VideoFingerprintEngine videoEngine_; std::function<bool()> expensiveStageGuard_; IndexPaths managedIndex_{}; bool managedIndexActive_=false;
  mutable GpuBackend videoGpu_;
  std::atomic<std::uint64_t> gpuImagesProcessed_{0};
  std::atomic<bool> gpuActive_{false};
  BenchmarkRecorder bench_;
  struct ColorThumb { int w=0, h=0; std::vector<unsigned char> bgra; };
  static constexpr std::size_t kColorThumbMax = 2048;
  mutable std::mutex thumbMutex_;
  mutable std::list<std::pair<std::string,ColorThumb>> thumbList_;
  mutable std::unordered_map<std::string,std::list<std::pair<std::string,ColorThumb>>::iterator> thumbMap_;
  void putColorThumb(const std::string& path, int w, int h, std::vector<unsigned char>&& bgra) const;
 std::vector<FileState> candidateStates_;
 CandidateIndex imageCandidates_;
 CandidateIndex videoCandidates_;
 CandidateIndex videoCrop4x3_, videoCrop1x1_, videoCrop9x16_;
 CandidateIndex imageCrop4x3_, imageCrop1x1_, imageCrop9x16_;
};
}
