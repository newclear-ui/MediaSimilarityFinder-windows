#pragma once
#include "scanner.h"
#include "database.h"
#include "scan_pipeline.h"
#include "resource_policy.h"
#include "video_fingerprint.h"
#include "index_manager.h"
#include "candidate_index.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>
#include <cstddef>
#include <cstdint>
namespace msf {
struct SearchMatch { std::string leftPath,rightPath; double percent=0; };
// Compact result reference for large-result consumers. It avoids duplicating file-path
// strings for every match; indices refer to MediaSearchEngine::files() for the scan.
struct SearchMatchRef { std::size_t leftIndex=0, rightIndex=0; double percent=0; };
struct SearchReport { std::size_t scanned=0, added=0, modified=0, unchanged=0, removed=0, analyzed=0, candidates=0, groups=0, gpuImages=0, gpuFallbackImages=0; double candidateReductionPercent=0; std::vector<SearchMatch> matches; };
struct ScanControl {
 std::atomic_bool cancel{false};
 std::atomic_bool pause{false};
 std::function<void(std::size_t,std::size_t,const std::string&)> progress;
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
};
class MediaSearchEngine {
public:
  bool openIndex(const std::string& dbPath);
  bool openIndexForRoot(const std::string& rootPath, const std::string& applicationDirectory);
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
private:
 Database db_; std::vector<MediaFile> files_; ResourcePolicy policy_{}; VideoFingerprintEngine videoEngine_; std::function<bool()> expensiveStageGuard_; IndexPaths managedIndex_{}; bool managedIndexActive_=false;
 std::vector<FileState> candidateStates_;
 CandidateIndex imageCandidates_;
 CandidateIndex videoCandidates_;
 CandidateIndex videoCrop4x3_, videoCrop1x1_, videoCrop9x16_;
 CandidateIndex imageCrop4x3_, imageCrop1x1_, imageCrop9x16_;
};
}
