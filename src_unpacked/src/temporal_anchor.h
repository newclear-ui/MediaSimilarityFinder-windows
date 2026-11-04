#pragma once
// TemporalAnchorV1 (0.9.4.172): independent video reading path that tolerates
// leading insertions (ads/intros/black leaders) while verifying main-content
// time continuity with a few low-resolution frames.
//
// Directive: "Temporal Anchor ... (preserve V3, compare in parallel)".
// - LegacyV3 (current V3 segmented + DTW) is untouched and stays selectable.
// - TemporalAnchorV1 runs independently; results carry strategyId and are
//   compared on the same dataset/pairs. No production-default change until
//   accuracy gates pass; V3 results and caches are never affected.
// - No production sparse seeks: anchor positions feed the existing single
//   sequential sweep (E-3B ExactnessPolicy::RefuseAll stands).
// - vertical flip is NOT supported (normal + horizontal mirror only).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "video_policy.h"  // VideoReadStrategy (single definition)
namespace msf {
// One low-resolution evidence frame of the compact temporal index.
struct AnchorFrame {
  double timestamp = 0;     // video-time seconds
  std::uint64_t phash = 0;  // normal pHash (32x32 gray)
  std::uint64_t mirror = 0; // horizontally mirrored pHash
  double mean = 0;          // mean gray 0..255
  double sd = 0;            // stddev gray (low-info screen, same floor as V3)
  double delta = 0;         // mean-abs change vs previous index frame
  bool lowInfo = false;     // sd < kAnchorMinVariance (never forced evidence)
};
// Compact temporal index: the only thing TemporalAnchorV1 compares.
struct TemporalAnchorIndex {
  double duration = 0;
  std::vector<AnchorFrame> seq;  // ascending video-time
  bool built = false;
};
// One candidate start (leading-insertion search hit).
struct StartCandidate {
  double startSec = 0;    // estimated main-content start in this file
  double confidence = 0;  // 0..1 (NOT a calibrated probability)
};
// One verified time anchor.
struct AnchorVerdict {
  double tRef = 0;       // reference (original) video-time
  double tCand = 0;      // candidate video-time (tRef + offset)
  double score = 0;      // 0..100 anchor score
  bool valid = false;    // independent main-content evidence
  bool lowInfo = false;  // anchor frames mostly flat (not counted when true)
  bool covered = false;  // both sides had frames (mismatch counts as evidence)
  double offset = 0;     // tCand - tRef for this anchor
};
// Ending handling (credits and leaders are NOT treated alike).
enum class EndingMode { NoCredits = 0, PreCredits = 1, Uncertain = 2 };
// Final verdict. DUP needs >=2 independent main anchors + consistent
// offset; ending-only or common-credits-only never confirms DUP; thin
// evidence stays UNCERTAIN (never forced).
enum class AnchorVerdictKind { Diff = 0, Uncertain = 1, Dup = 2 };
struct TemporalAnchorResult {
  static constexpr const char* kStrategyId = "TemporalAnchorV1";
  std::string strategyId = kStrategyId;
  double openingProbeDuration = 0;  // 3/5/10 s variant actually used
  int openingProbeFrames = 0;
  double leadingSearchWindow = 0;  // seconds searched (initial 180)
  double startA = 0, startB = 0;   // main-content starts
  double alignmentOffset = 0;      // startB - startA
  double startMatchConfidence = 0;
  double estimatedContentA = 0, estimatedContentB = 0;  // duration minus leader
  std::vector<AnchorVerdict> anchors;
  double offsetDrift = 0;      // max-min anchor offset
  int validAnchorCount = 0;    // independent main anchors (ending excluded)
  int lowInformationMatches = 0;
  int coveredFailedAnchors = 0;  // covered but mismatched mains (edit signal)
  bool offsetDrifted = false;    // a main anchor re-verified off-offset
  EndingMode endingMode = EndingMode::Uncertain;
  AnchorVerdictKind verdict = AnchorVerdictKind::Diff;
  double score = 0;  // 0..100 (distribution measured separately; 50/87.5 keep meaning)
  // Cost split (§4: sampled != decoded != compared).
  double decodeMs = 0;
  std::size_t decodedFrames = 0, sampledFrames = 0, anchorFrames = 0;
  double alignmentMs = 0, compareMs = 0;
  int additionalProbes = 0;
  bool cacheHit = false;  // .172: no persistent anchor cache (always false)
  std::string cacheIdentity = "anchor-v1";
};
// (VideoReadStrategy is defined once in video_policy.h.)
// Diagnostic switch: MSF_VIDEO_ANCHOR=1 enables anchor index builds in the
// engine and the validation harness. Default off.
bool temporalAnchorEnabled();
// Anchor sample plan: dense 1 fps over the whole timeline (single
// sequential sweep; decode cost is the full file either way, see §4).
// Density is what makes arbitrary leading offsets searchable: the opening
// probe (1-10 s) must find its match wherever the content landed inside
// the 180 s search window, and verification anchors must resolve any
// offset afterwards. Sparse windows alone cannot do either (a 45 s leader
// lands in a plan desert). Pure function of duration; sorted ascending.
std::vector<double> buildTemporalAnchorPlan(double durationSec);
// Convert decoded 32x32 frames (timestamp-ordered) into the compact index.
// pHash/mirror come from the existing perceptual hash; mean/sd/delta from
// the gray pixels (delta = mean-abs change vs previous frame).
struct VideoFrame;
TemporalAnchorIndex makeTemporalAnchorIndex(const std::vector<VideoFrame>& frames32,
                                            double durationSec);
// Leading-insertion search: find reference opening sequence inside the
// candidate index within [0, windowSec]. Ordered, mirror-tolerant,
// single-frame hits rejected, flat frames down-weighted (dark main content
// is never auto-classified as leader). Returns candidates (may be several;
// the caller must NOT confirm one immediately).
std::vector<StartCandidate> findLeadingOffset(const TemporalAnchorIndex& ref,
                                              const TemporalAnchorIndex& cand,
                                              double probeLenSec, double windowSec);
// Full comparison (§3.2-§3.6 + §5 verdict policy). Pure function of indexes.
double temporalAnchorSimilarity(const TemporalAnchorIndex& a, const TemporalAnchorIndex& b,
                                TemporalAnchorResult& out);
// Test-visible thresholds.
constexpr double kAnchorMinVariance = 6.0;   // low-info floor (same as V3)
constexpr double kAnchorFramePass = 87.5;    // per-frame pass mark
constexpr double kAnchorOffsetTol = 2.0;     // offset consistency (seconds)
constexpr double kAnchorConfidenceFloor = 0.30;  // start-candidate floor
}  // namespace msf
