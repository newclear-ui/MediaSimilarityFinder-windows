#pragma once
#include "candidate_index.h"
#include "analyze_telemetry.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
namespace msf {
class VideoFingerprintEngine; // cache-backed temporal source (optional, see below)
class GpuBackend;
enum class MediaKind { Unknown, Image, Video };
struct MediaFile { std::string path; MediaKind kind=MediaKind::Unknown; std::uint64_t size=0,modified=0,fingerprint=0,mirrorFingerprint=0; std::uint64_t crop4x3=0,crop1x1=0,crop9x16=0,mirrorCrop4x3=0,mirrorCrop1x1=0,mirrorCrop9x16=0; double duration=0; std::vector<std::uint64_t> anchors; };
struct MediaMatch { std::size_t left=0,right=0; double percent=0; };
struct ScanStats { std::size_t files=0,indexed=0,candidates=0,groups=0,possiblePairs=0; double candidateReductionPercent=0; std::vector<MediaMatch> matches; std::size_t videoCandidates=0,videoTemporalChecks=0;
  // D9a: per-stage timing and verify counters from the batch analyze() pass.
  // Additive; every existing consumer keeps reading the fields above.
  AnalyzeTelemetry analyze{}; };
class ScanPipeline {
 std::vector<MediaFile> files_;
 // Running candidate indexes shared by batch analyze() and incremental
 // addAndMatch(). analyze() clears and rebuilds them, so batch semantics
 // never change; addAndMatch() accumulates for live streaming.
 CandidateIndex imageIdx_,videoIdx_,vc4_,vc1_,vc916_,c4_,c1_,c916_;
 std::vector<std::size_t> imageMap_,videoMap_;
public:
  using MatchCallback = std::function<void(const MediaMatch&)>;
  // Polled periodically during analyze(); return true to abort promptly.
  // Aborting is internal: analyze() returns partial stats, and the engine
  // observes the stop through its own control flag as on every stop path.
  using StopCheck = std::function<bool()>;
  void add(const MediaFile& f);
 // Incremental first stage: add f and emit matches against previously added
 // files only (same threshold verdicts as analyze()). The expensive video
 // temporal second stage stays exclusive to the final analyze() pass.
 void addAndMatch(const MediaFile& f, unsigned maxDistance, const MatchCallback& onMatch);
 void clear();
  ScanStats analyze(unsigned maxDistance=8);
  ScanStats analyze(unsigned maxDistance, const MatchCallback& onMatch);
  // 0.9.4.81 mode-aware final pass plus B-slice windowing.
  //   changedFiles/skipBothChangedImages: restrict which pairs are (re)verified
  //     (null/False = verify everything, the Sequential baseline).
  //   sliceGroups: when >0, each verification pass runs in windows of this many
  //     groups (files), invoking onSlice(phase, done, total) after each window
  //     and honoring stop between windows. 0 = one window (no explicit slicing).
  //     phase: 0 image-full, 1 video-full, 2 crop.
  //   onSlice: optional window callback, may be null.
  //   startImageGroups: resume offset; image groups below it are skipped and
  //     their pairs come from the persisted match set.
  ScanStats analyze(unsigned maxDistance, const MatchCallback& onMatch, const StopCheck& stop,
                    const std::vector<char>* changedFiles=nullptr, bool skipBothChangedImages=false,
                    std::size_t sliceGroups=0,
                    const std::function<void(int,std::size_t,std::size_t)>* onSlice=nullptr,
                    std::size_t startImageGroups=0);
  // Optional cache-backed engine for the expensive video temporal stage.
  // Without it analyze() decodes every video pair from scratch (its local
  // engine has no cache open); with it, cache hits skip the decode entirely.
  // Same verdicts either way ??build() output is content-determined. The
  // pointed engine must outlive the analyze() call; not owned.
  void setSharedTemporalEngine(const VideoFingerprintEngine* e) { temporalEngine_ = e; }
  void setVideoGpuBackend(GpuBackend* g) { videoGpu_ = g; }
  void setVideoGpuActivity(std::atomic<bool>* a) { videoGpuActivity_ = a; }
  const std::vector<MediaFile>& files() const;
 private:
  const VideoFingerprintEngine* temporalEngine_ = nullptr;
  GpuBackend* videoGpu_ = nullptr;
  std::atomic<bool>* videoGpuActivity_ = nullptr;
};
}
