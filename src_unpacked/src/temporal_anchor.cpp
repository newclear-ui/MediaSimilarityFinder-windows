// See temporal_anchor.h. TemporalAnchorV1 (§3): opening probe, leading
// search, anchor verification, offset consistency, ending anchor, verdict.
#include "temporal_anchor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>

#include "fingerprint.h"
#include "similarity.h"
#include "video_decoder.h"

namespace msf {
namespace {
// Best of the four normal/mirror pairings (vertical flip unsupported).
double frameScore(const AnchorFrame& a, const AnchorFrame& b) {
  const double s00 = hash_similarity(a.phash, b.phash);
  const double s01 = hash_similarity(a.phash, b.mirror);
  const double s10 = hash_similarity(a.mirror, b.phash);
  const double s11 = hash_similarity(a.mirror, b.mirror);
  return std::max(std::max(s00, s01), std::max(s10, s11));
}
bool framePass(const AnchorFrame& a, const AnchorFrame& b) {
  return frameScore(a, b) >= kAnchorFramePass;
}
// First textured run (main-content start) within the first 60 s: 3
// consecutive non-flat frames. Returns ref.seq.size() when the reference
// itself is flat that far (caller falls back to a blind window).
std::size_t probeStartIndex(const TemporalAnchorIndex& ref) {
  for (std::size_t i = 0; i < ref.seq.size() && ref.seq[i].timestamp <= 60.0; ++i) {
    if (i + 2 < ref.seq.size() && !ref.seq[i].lowInfo && !ref.seq[i + 1].lowInfo &&
        !ref.seq[i + 2].lowInfo && ref.seq[i + 2].timestamp - ref.seq[i].timestamp <= 3.5)
      return i;
  }
  return ref.seq.size();
}
// Nearest index frame to t within tolSec, or npos.
std::size_t nearestAt(const std::vector<AnchorFrame>& s, double t, double tolSec) {
  std::size_t best = s.size();
  double bd = tolSec + 1e-9;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const double d = std::abs(s[i].timestamp - t);
    if (d <= tolSec && d < bd) {
      bd = d;
      best = i;
    }
  }
  return best;
}
}  // namespace

bool temporalAnchorEnabled() {
  static const bool on = [] {
    const char* v = std::getenv("MSF_VIDEO_ANCHOR");
    return v && std::string(v) == "1";
  }();
  return on;
}

std::vector<double> buildTemporalAnchorPlan(double durationSec) {
  std::vector<double> ts;
  if (!(durationSec > 0)) return ts;
  // Dense 1 fps substrate for search + verification at any offset.
  // (A sparse window plan cannot resolve a 45 s leader: the content lands
  // in a plan desert. The sweep itself always decodes the full file, so
  // density costs conversions/hashes, never more decoding.)
  // Cap guards absurd durations; shorts stay fully covered.
  const double cap = 14400.0;  // 4 h at 1 fps
  const double end = std::min(durationSec, cap);
  for (double t = 0; t < end; t += 1.0) ts.push_back(t);
  if (ts.empty() || ts.back() < durationSec - 1e-3) ts.push_back(durationSec);
  return ts;
}

TemporalAnchorIndex makeTemporalAnchorIndex(const std::vector<VideoFrame>& frames32,
                                            double durationSec) {
  TemporalAnchorIndex idx;
  idx.duration = durationSec;
  std::vector<std::uint8_t> prev;
  for (const auto& f : frames32) {
    if (f.width != 32 || f.height != 32 || f.gray.size() != 1024) continue;
    AnchorFrame a;
    a.timestamp = f.timestamp;
    const PerceptualHashPair hp = perceptual_hash_pair(f.gray, 32, 32);
    a.phash = hp.normal;
    a.mirror = hp.mirrored;
    double s = 0;
    for (unsigned char v : f.gray) s += v;
    a.mean = s / 1024.0;
    double sq = 0;
    for (unsigned char v : f.gray) {
      const double d = (double)v - a.mean;
      sq += d * d;
    }
    a.sd = std::sqrt(sq / 1024.0);
    a.lowInfo = a.sd < kAnchorMinVariance;
    // Edge proxy (credits/texture signal, §3.2): mean abs horizontal
    // difference on a stride-4 subsample of the 32x32 gray.
    double es = 0;
    std::size_t en = 0;
    for (int y = 0; y < 32; y += 4)
      for (int x = 0; x + 4 < 32; x += 4) {
        es += std::abs((int)f.gray[(std::size_t)y * 32 + x + 4] - (int)f.gray[(std::size_t)y * 32 + x]);
        ++en;
      }
    a.edge = en ? es / en : 0.0;
    if (!prev.empty()) {
      double d = 0;
      for (std::size_t i = 0; i < 1024; ++i) d += std::abs((int)f.gray[i] - (int)prev[i]);
      a.delta = d / 1024.0;
    }
    prev = f.gray;
    idx.seq.push_back(a);
  }
  idx.built = !idx.seq.empty();
  return idx;
}

std::vector<StartCandidate> findLeadingOffset(const TemporalAnchorIndex& ref,
                                              const TemporalAnchorIndex& cand,
                                              double probeLenSec, double windowSec) {
  std::vector<StartCandidate> out;
  if (!ref.built || !cand.built || ref.seq.empty()) return out;
  // §3.1/§3.2: the probe is the first TEXTURED run (main-content start),
  // not blind [0, L]. A black/fade opening is leader-like even in the
  // reference; probing it would match any black leader anywhere (the exact
  // false start §5 forbids). Fall back to [0, L] when the reference itself
  // is flat within 60 s.
  const std::size_t probeBase = probeStartIndex(ref);
  const std::size_t startIx = (probeBase < ref.seq.size()) ? probeBase : 0;
  const double probeT0 = ref.seq[startIx].timestamp;
  // Reference opening probe: frames within [probeT0, probeT0+probeLen].
  std::vector<std::size_t> probe;
  for (std::size_t i = startIx;
       i < ref.seq.size() && ref.seq[i].timestamp <= probeT0 + probeLenSec + 1e-9; ++i)
    probe.push_back(i);
  if (probe.size() < 3) return out;  // need a real sequence, not a frame
  // Try every candidate start frame inside the window as alignment origin.
  for (std::size_t s = 0; s < cand.seq.size() && cand.seq[s].timestamp <= windowSec + 1e-9; ++s) {
    // Ordered run: walk probe frames in order, each matched by a later
    // candidate frame (spacing 0.25-2.5 s tolerates re-encode cadence).
    std::size_t c = s;
    int matches = 0, steps = 0, lowInfoHits = 0;
    double qSum = 0;
    double prevCt = -1, firstHitT = -1;
    for (std::size_t pi = 0; pi < probe.size() && c < cand.seq.size(); ++pi) {
      bool hit = false;
      for (std::size_t k = c; k < cand.seq.size(); ++k) {
        const double gap = cand.seq[k].timestamp - (prevCt < 0 ? cand.seq[s].timestamp - 1.0 : prevCt);
        if (gap > 2.5) break;  // run broken: order/spacing violated
        if (gap < 0.25 && k != c) continue;
        if (framePass(ref.seq[probe[pi]], cand.seq[k])) {
          hit = true;
          c = k + 1;
          prevCt = cand.seq[k].timestamp;
          if (firstHitT < 0) firstHitT = cand.seq[k].timestamp;
          ++matches;
          qSum += frameScore(ref.seq[probe[pi]], cand.seq[k]);
          if (ref.seq[probe[pi]].lowInfo || cand.seq[k].lowInfo) ++lowInfoHits;
          break;
        }
      }
      ++steps;
      if (!hit) {
        // Allow ONE skipped probe frame (re-encode cadence), not two.
        if (pi + 1 < probe.size()) {
          ++pi;
          ++steps;
        } else {
          break;
        }
      }
    }
    // §3.1: a single frame never confirms a start; need an ordered run.
    if (matches < 3) continue;
    const double cov = (double)matches / (double)steps;
    const double lowFrac = (double)lowInfoHits / (double)(2 * matches);
    // Flat frames must not dominate the evidence (but dark main content is
    // never auto-classified: it only lowers confidence, never vetoes).
    const double conf = std::clamp(cov * (qSum / (100.0 * matches)) * (1.0 - 0.5 * lowFrac), 0.0, 1.0);
    if (conf < kAnchorConfidenceFloor) continue;
    // Report the first MATCHED frame time (a run may skip leading
    // non-matching frames at s and still align at firstHitT).
    out.push_back({firstHitT, conf});
  }
  std::sort(out.begin(), out.end(), [](const StartCandidate& a, const StartCandidate& b) {
    if (a.confidence != b.confidence) return a.confidence > b.confidence;
    return a.startSec < b.startSec;  // deterministic: earliest wins ties
  });
  if (out.size() > 4) out.resize(4);  // keep ambiguity, cap the list
  return out;
}

// V2 leading search: same contract as V1's findLeadingOffset, plus
// (a) non-flat frames only (low-info candidates are skipped, never matched),
// (b) lenient first-hit reach (window edge: content starting right at/past
// the window end, e.g. a 180 s leader; bounded by probeLen + 2.5 s).
std::vector<StartCandidate> findLeadingOffsetV2(const TemporalAnchorIndex& ref,
                                                const TemporalAnchorIndex& cand,
                                                double probeLenSec, double windowSec) {
  std::vector<StartCandidate> out;
  if (!ref.built || !cand.built || ref.seq.empty()) return out;
  const std::size_t startIx = probeStartIndex(ref);
  const std::size_t ps0 = (startIx < ref.seq.size()) ? startIx : 0;
  const double probeT0 = ref.seq[ps0].timestamp;
  std::vector<std::size_t> probe;
  for (std::size_t i = ps0;
       i < ref.seq.size() && ref.seq[i].timestamp <= probeT0 + probeLenSec + 1e-9; ++i)
    if (!ref.seq[i].lowInfo) probe.push_back(i);
  if (probe.size() < 3) return out;
  for (std::size_t s = 0; s < cand.seq.size() && cand.seq[s].timestamp <= windowSec + 1e-9; ++s) {
    std::size_t c = s;
    int matches = 0, steps = 0;
    double qSum = 0;
    double prevCt = -1, firstHitT = -1;
    for (std::size_t pi = 0; pi < probe.size() && c < cand.seq.size(); ++pi) {
      bool hit = false;
      for (std::size_t k = c; k < cand.seq.size(); ++k) {
        if (cand.seq[k].lowInfo) continue;  // never match flat frames
        const double gap = cand.seq[k].timestamp - (prevCt < 0 ? cand.seq[s].timestamp - 1.0 : prevCt);
        if (matches == 0 ? gap > probeLenSec + 2.5 : gap > 2.5) break;
        if (gap < 0.25 && k != c) continue;
        if (framePass(ref.seq[probe[pi]], cand.seq[k])) {
          hit = true;
          c = k + 1;
          prevCt = cand.seq[k].timestamp;
          if (firstHitT < 0) firstHitT = cand.seq[k].timestamp;
          ++matches;
          qSum += frameScore(ref.seq[probe[pi]], cand.seq[k]);
          break;
        }
      }
      ++steps;
      if (!hit) {
        if (pi + 1 < probe.size()) {
          ++pi;
          ++steps;
        } else {
          break;
        }
      }
    }
    if (matches < 3) continue;
    const double cov = (double)matches / (double)steps;
    const double conf = std::clamp(cov * (qSum / (100.0 * matches)), 0.0, 1.0);
    if (conf < kAnchorConfidenceFloor) continue;
    out.push_back({firstHitT, conf});
  }
  std::sort(out.begin(), out.end(), [](const StartCandidate& a, const StartCandidate& b) {
    if (a.confidence != b.confidence) return a.confidence > b.confidence;
    return a.startSec < b.startSec;
  });
  if (out.size() > 4) out.resize(4);
  return out;
}

namespace {
// One anchor check at reference time tRef (3 frames @~3 s) against the
// candidate at tRef + offset.
AnchorVerdict checkAnchor(const TemporalAnchorIndex& ref, const TemporalAnchorIndex& cand,
                          double tRef, double offset) {
  AnchorVerdict v;
  v.tRef = tRef;
  v.tCand = tRef + offset;
  int hits = 0, flat = 0, n = 0;
  double offSum = 0;
  for (double dt : {-3.0, 0.0, 3.0}) {
    const std::size_t ri = nearestAt(ref.seq, tRef + dt, 1.5);
    const std::size_t ci = nearestAt(cand.seq, tRef + offset + dt, 1.5);
    if (ri >= ref.seq.size() || ci >= cand.seq.size()) continue;
    ++n;
    const auto& rf = ref.seq[ri];
    const auto& cf = cand.seq[ci];
    if (rf.lowInfo || cf.lowInfo) ++flat;
    if (framePass(rf, cf)) {
      ++hits;
      v.score += frameScore(rf, cf);
      offSum += cf.timestamp - rf.timestamp;
    }
  }
  if (n == 0) return v;  // no coverage: not evidence either way
  v.covered = true;
  v.score = (n > 0) ? v.score / (100.0 * n) * 100.0 : 0;
  v.lowInfo = flat * 2 > n;  // mostly flat: not counted when true
  // Valid: >=2/3 frame matches with order preserved (nearestAt keeps time
  // order by construction) and real (non-flat) evidence.
  if (hits * 3 >= n * 2 && !v.lowInfo) {
    v.valid = true;
    v.offset = offSum / hits;
  }
  return v;
}
// Local anchor search around an expected position: the seed of the
// piecewise branch (§3.5). Scores the ref triple against every candidate
// center in [center-radius, center+radius] (1 s steps) and reports the
// best offset with >=2/3 frame matches, or NaN.
double findAnchorOffsetNear(const TemporalAnchorIndex& ref, const TemporalAnchorIndex& cand,
                            double tRef, double center, double radius, int& outHits) {
  outHits = 0;
  double bestOff = std::numeric_limits<double>::quiet_NaN();
  int bestHits = 0;
  double bestScore = -1;
  for (double tC = center - radius; tC <= center + radius + 1e-9; tC += 1.0) {
    int hits = 0, n = 0, flat = 0;
    double sc = 0;
    for (double dt : {-3.0, 0.0, 3.0}) {
      const std::size_t ri = nearestAt(ref.seq, tRef + dt, 1.5);
      const std::size_t ci = nearestAt(cand.seq, tC + dt, 1.5);
      if (ri >= ref.seq.size() || ci >= cand.seq.size()) continue;
      ++n;
      if (ref.seq[ri].lowInfo || cand.seq[ci].lowInfo) ++flat;
      if (framePass(ref.seq[ri], cand.seq[ci])) {
        ++hits;
        sc += frameScore(ref.seq[ri], cand.seq[ci]);
      }
    }
    if (n == 0 || flat * 2 > n) continue;  // no coverage or flat: not evidence
    if (hits * 3 >= n * 2 && (hits > bestHits || (hits == bestHits && sc > bestScore))) {
      bestHits = hits;
      bestScore = sc;
      bestOff = tC - tRef;
    }
  }
  outHits = bestHits;
  return bestOff;
}
}  // namespace

double temporalAnchorSimilarity(const TemporalAnchorIndex& a, const TemporalAnchorIndex& b,
                                TemporalAnchorResult& out) {
  const auto t0 = std::chrono::steady_clock::now();
  out = TemporalAnchorResult();
  if (!a.built || !b.built || a.seq.empty() || b.seq.empty()) {
    out.verdict = AnchorVerdictKind::Uncertain;
    return 0;
  }
  // §3.1/3.2: probe lengths 3/5/10 s compared; keep the best run.
  const double kWindow = 180.0;  // initial search window (extendable to 120-180)
  out.leadingSearchWindow = kWindow;
  std::vector<StartCandidate> cands;
  double bestLen = 5.0;
  for (double L : {3.0, 5.0, 10.0}) {
    auto c = findLeadingOffset(a, b, L, kWindow);
    if (!c.empty() && (cands.empty() || c.front().confidence > cands.front().confidence)) {
      cands = std::move(c);
      bestLen = L;
      if (cands.front().confidence >= 0.9) break;
    }
  }
  out.openingProbeDuration = bestLen;
  out.openingProbeFrames = 0;
  {
    // Count the actual textured probe, not the blind [0, L] window.
    const std::size_t ps = probeStartIndex(a);
    const std::size_t sx = (ps < a.seq.size()) ? ps : 0;
    const double pt0 = a.seq.empty() ? 0.0 : a.seq[sx].timestamp;
    for (std::size_t i = sx; i < a.seq.size() && a.seq[i].timestamp <= pt0 + bestLen + 1e-9; ++i)
      ++out.openingProbeFrames;
  }
  // Bidirectional: A->B gives startB, B->A gives startA. No assumption that
  // either file starts at its own first frame.
  std::vector<StartCandidate> rev;
  for (double L : {3.0, 5.0, 10.0}) {
    auto c = findLeadingOffset(b, a, L, kWindow);
    if (!c.empty() && (rev.empty() || c.front().confidence > rev.front().confidence)) {
      rev = std::move(c);
      if (rev.front().confidence >= 0.9) break;
    }
  }
  // Candidate starts (default 0/0 when nothing found: anchors decide).
  // The (0,0) no-leader hypothesis is ALWAYS evaluated alongside found
  // candidates: a missed probe must not veto the direct alignment (e.g.
  // dark openings where hash runs are unstable), and best-score selection
  // discards it whenever a shifted alignment verifies better.
  std::vector<std::pair<double, double>> starts = {{0.0, 0.0}};
  if (!cands.empty() && !rev.empty()) {
    starts.clear();
    for (const auto& cb : cands)
      for (const auto& ca : rev) starts.emplace_back(ca.startSec, cb.startSec);
    if (starts.size() > 4) starts.resize(4);
    starts.emplace_back(0.0, 0.0);
  } else if (!cands.empty()) {
    starts = {{0.0, cands.front().startSec}, {0.0, 0.0}};
  } else if (!rev.empty()) {
    starts = {{rev.front().startSec, 0.0}, {0.0, 0.0}};
  }
  const auto t1 = std::chrono::steady_clock::now();
  // §3.3-§3.6: evaluate start hypotheses; keep the best-supported one.
  double bestScore = -1;
  TemporalAnchorResult best;
  for (const auto& st : starts) {
    TemporalAnchorResult r;
    r.strategyId = TemporalAnchorResult::kStrategyId;
    r.leadingSearchWindow = kWindow;
    r.openingProbeDuration = bestLen;
    r.openingProbeFrames = out.openingProbeFrames;
    r.startA = st.first;
    r.startB = st.second;
    r.alignmentOffset = st.second - st.first;
    r.startMatchConfidence = std::max(cands.empty() ? 0.0 : cands.front().confidence,
                                      rev.empty() ? 0.0 : rev.front().confidence);
    r.estimatedContentA = std::max(0.0, a.duration - st.first);
    r.estimatedContentB = std::max(0.0, b.duration - st.second);
    const double off = r.alignmentOffset;
    // §3.4 anchors: post-start, 5/10/15 min (inside content only), each 3
    // frames @~3 s. Anchor count is not a quota: use existing windows only.
    std::vector<double> refs = {st.first + 5.0};
    for (double m : {300.0, 600.0, 900.0}) {
      const double t = st.first + m;
      if (t + 5.0 < a.duration && t + 5.0 < b.duration - (st.second - st.first)) refs.push_back(t);
    }
    int validMain = 0;
    int coveredMain = 0, coveredFailed = 0;
    double offMin = 1e9, offMax = -1e9;
    std::vector<std::pair<double, double>> drifted;  // (tRef, foundOffset)
    for (double t : refs) {
      AnchorVerdict v = checkAnchor(a, b, t, off);
      r.anchors.push_back(v);
      if (v.valid) {
        ++validMain;
        ++coveredMain;
        offMin = std::min(offMin, v.offset);
        offMax = std::max(offMax, v.offset);
      } else if (v.covered && !v.lowInfo) {
        // Covered but mismatched: possible mid-roll edit. Search nearby
        // for the anchor at another offset (piecewise-branch seed, §3.5).
        ++coveredMain;
        ++coveredFailed;
        int fh = 0;
        const double found = findAnchorOffsetNear(a, b, t, t + off, 120.0, fh);
        if (fh >= 2 && std::isfinite(found) && std::abs(found - off) > kAnchorOffsetTol)
          drifted.emplace_back(t, found);
      } else if (v.lowInfo) {
        ++coveredMain;
        ++r.lowInformationMatches;
      }
    }
    r.validAnchorCount = validMain;
    for (const auto& d : drifted) {
      offMin = std::min(offMin, d.second);
      offMax = std::max(offMax, d.second);
    }
    // §3.5 offset consistency (leading-only vs mid-roll/edit signal).
    r.offsetDrift = (validMain > 0 || !drifted.empty()) ? (offMax - offMin) : 1e9;
    const bool consistent = validMain > 0 && r.offsetDrift <= kAnchorOffsetTol;
    r.offsetDrifted = !drifted.empty() && r.offsetDrift > kAnchorOffsetTol;
    // §3.6 ending anchor (last 9 s @3 s): supporting evidence only.
    AnchorVerdict ev = checkAnchor(a, b, a.duration - 6.0, off);
    // Ending darkness check: dark-flat endings cannot separate dark main
    // content from credits, so confidence is lowered (uncertainty kept).
    double endMean = 0;
    int endN = 0;
    for (const auto& f : a.seq)
      if (f.timestamp >= a.duration - 9.0) {
        endMean += f.mean;
        ++endN;
      }
    if (endN > 0) endMean /= endN;
    bool endDarkFlat = (endMean < 25.0);
    if (ev.valid && !endDarkFlat) {
      r.endingMode = EndingMode::NoCredits;
    } else {
      r.endingMode = EndingMode::Uncertain;
    }
    r.anchors.push_back(ev);
    // §5 verdict: candidate search vs final DUP separated.
    // - DUP needs >=2 independent main anchors + consistent offset, with no
    //   covered-but-failed main anchor and no drift (mid-roll edit signal).
    // - Ending/credits alone never confirm; thin evidence stays UNCERTAIN.
    // - A covered-but-failed main anchor (or drift) means possible local
    //   edit -> UNCERTAIN with a piecewise/DTW branch left for follow-up;
    //   never forced to DUP, never dismissed as Diff.
    // - validMain==0 with >=1 covered main anchor on non-thin input -> Diff.
    const bool thinInput = (a.duration < 60 || b.duration < 60) || [&] {
      std::size_t flat = 0;
      for (const auto& f : a.seq)
        if (f.lowInfo) ++flat;
      return flat * 2 > a.seq.size();
    }();
    double score = 35.0 * std::min(validMain, 2) + (consistent ? 15.0 : 0.0) +
                   ((ev.valid && !endDarkFlat) ? 10.0 : 0.0);
    r.score = std::clamp(score, 0.0, 100.0);
    if (r.offsetDrifted) {
      r.verdict = AnchorVerdictKind::Uncertain;
    } else if (validMain >= 2 && consistent && coveredFailed == 0) {
      r.verdict = AnchorVerdictKind::Dup;
    } else if (coveredMain >= 2 && !thinInput) {
      r.verdict = (validMain == 0) ? AnchorVerdictKind::Diff : AnchorVerdictKind::Uncertain;
    } else {
      r.verdict = AnchorVerdictKind::Uncertain;
    }
    if (r.score > bestScore) {
      bestScore = r.score;
      best = r;
    }
  }
  out = best;
  // Cost split (§4).
  out.anchorFrames = a.seq.size() + b.seq.size();
  const auto t2 = std::chrono::steady_clock::now();
  out.alignmentMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
  out.compareMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
  return out.score;
}

// ---------------- TemporalAnchorV2 (bidirectional, 0.9.4.173) ----------------
// V1 entry points above are behavior-frozen; V2 shares only the index type,
// the plan builder, and the low-level helpers below.

namespace {
// Strict frame match for V2 evidence: low-info frames never match (§6.3).
bool frameMatchStrict(const AnchorFrame& a, const AnchorFrame& b) {
  if (a.lowInfo || b.lowInfo) return false;
  return frameScore(a, b) >= kAnchorFramePass;
}
// V2 anchor check: like V1's checkAnchor but matched frames must be
// non-flat; valid needs >=2 such frames (§2.3, §6.1-§6.3).
AnchorVerdict checkAnchorV2(const TemporalAnchorIndex& ref, const TemporalAnchorIndex& cand,
                            double tRef, double offset, int& hitValid) {
  AnchorVerdict v;
  v.tRef = tRef;
  v.tCand = tRef + offset;
  hitValid = 0;
  int hits = 0, n = 0, flat = 0;
  double offSum = 0, scSum = 0;
  for (double dt : {-3.0, 0.0, 3.0}) {
    const std::size_t ri = nearestAt(ref.seq, tRef + dt, 1.5);
    const std::size_t ci = nearestAt(cand.seq, tRef + offset + dt, 1.5);
    if (ri >= ref.seq.size() || ci >= cand.seq.size()) continue;
    ++n;
    const auto& rf = ref.seq[ri];
    const auto& cf = cand.seq[ci];
    if (rf.lowInfo || cf.lowInfo) {
      ++flat;
      continue;  // excluded from evidence, never counted as a match
    }
    if (framePass(rf, cf)) {
      ++hits;
      ++hitValid;
      scSum += frameScore(rf, cf);
      offSum += cf.timestamp - rf.timestamp;
    }
  }
  if (n == 0) return v;  // no coverage: not evidence either way
  v.covered = true;
  v.lowInfo = (flat * 2 >= n) || (n - flat == 0);
  v.score = (hits > 0) ? scSum / hits : 0;
  if (hitValid >= 2) {
    v.valid = true;
    v.offset = offSum / hitValid;
  }
  return v;
}
}  // namespace

std::vector<double> anchorCentersForDuration(double contentStart, double contentEnd) {
  std::vector<double> out;
  if (!(contentEnd > contentStart)) return out;
  const double span = contentEnd - contentStart;
  auto push = [&](double t) {
    if (t > contentStart + 1.0 && t < contentEnd - 1.0) {
      if (out.empty() || t - out.back() >= 2.0) out.push_back(t);
    }
  };
  if (span <= 13.0) {
    // §5.1 micro-anchors: start, middle, end — distinct frames.
    push(contentStart + 2.0);
    push(contentStart + span * 0.5);
    push(contentEnd - 2.0);
    return out;
  } else if (span <= 60.0) {
    // §5.2: scaled thirds (start, middle, end all used).
    push(contentStart + span * 0.25);
    push(contentStart + span * 0.5);
    push(contentStart + span * 0.75);
  } else if (span <= 1500.0) {
    // §5.3: start, center, late, end-region.
    push(contentStart + 5.0);
    push(contentStart + span * 0.5);
    push(contentEnd - span * 0.25);
    push(contentEnd - 5.0);
  } else {
    // §5.4 long-form: forward 5/10/15 min + reverse 5/10/15 min pre-end.
    for (double m : {300.0, 600.0, 900.0}) push(contentStart + m);
    for (double m : {900.0, 600.0, 300.0}) push(contentEnd - m);
  }
  return out;
}

EndingProbe findEndingProbe(const TemporalAnchorIndex& idx) {
  EndingProbe e;
  e.mainEndSec = idx.duration > 9.0 ? idx.duration - 9.0 : idx.duration;
  if (!idx.built || idx.seq.empty()) return e;
  // Main-region edge median (first 80%) as the texture baseline.
  std::vector<double> edges;
  const std::size_t head = idx.seq.size() * 4 / 5;
  for (std::size_t i = 0; i < head && i < idx.seq.size(); ++i)
    if (!idx.seq[i].lowInfo) edges.push_back(idx.seq[i].edge);
  std::sort(edges.begin(), edges.end());
  const double medEdge = edges.empty() ? 0.0 : edges[edges.size() / 2];
  // Trailing run scan (last 120 s): dark + still + edge-elevated credits.
  double runStart = -1;
  for (std::size_t i = 0; i < idx.seq.size(); ++i) {
    const auto& f = idx.seq[i];
    if (f.timestamp < idx.duration - 120.0) continue;
    const bool darkStill = f.mean < 45.0 && f.delta < 3.0;
    if (darkStill && !f.lowInfo && medEdge > 0.5 && f.edge > 1.5 * medEdge) {
      if (runStart < 0) runStart = f.timestamp;
    } else {
      // Run ends at the first frame that is not a credits candidate.
      if (runStart >= 0 && f.timestamp - runStart >= 10.0) {
        e.creditsStartSec = runStart;
        e.mainEndSec = std::max(0.0, runStart - 5.0);
        e.mode = EndingMode::PreCredits;
        e.confidence = std::clamp((f.timestamp - runStart) / 30.0, 0.5, 0.9);
        return e;
      }
      runStart = -1;
    }
  }
  if (runStart >= 0 && idx.duration - runStart >= 10.0) {
    e.creditsStartSec = runStart;
    e.mainEndSec = std::max(0.0, runStart - 5.0);
    e.mode = EndingMode::PreCredits;
    e.confidence = std::clamp((idx.duration - runStart) / 30.0, 0.5, 0.9);
    return e;
  }
  // No credits run: dark-flat tail = uncertain (never assume credits);
  // bright/moving ending = main content to the end.
  double endMean = 0;
  int endN = 0;
  for (const auto& f : idx.seq)
    if (f.timestamp >= idx.duration - 9.0) {
      endMean += f.mean;
      ++endN;
    }
  if (endN > 0) endMean /= endN;
  if (endMean < 25.0) {
    e.mode = EndingMode::Uncertain;
    e.confidence = 0.2;
  } else {
    e.mode = EndingMode::NoCredits;
    e.mainEndSec = idx.duration;
    e.confidence = 0.7;
  }
  return e;
}

double anchorVerdictScore(int validMain, bool consistent, bool endingSupport) {
  return std::clamp(40.0 * std::min(validMain, 2) + (consistent ? 10.0 : 0.0) +
                        (endingSupport ? 10.0 : 0.0),
                    0.0, 100.0);
}

AnchorVerdictKind anchorVerdictFor(int validMain, bool consistent, bool coveredFailed,
                                   bool drifted, bool thin, bool ratioFlag, double score) {
  if (drifted) return AnchorVerdictKind::Uncertain;
  if (ratioFlag) return AnchorVerdictKind::Uncertain;
  if (validMain >= 2 && consistent && !coveredFailed && score >= 87.5)
    return AnchorVerdictKind::Dup;
  // Partial match with failures = possible edit, never forced either way.
  if (validMain > 0 && coveredFailed) return AnchorVerdictKind::Uncertain;
  if (thin) return AnchorVerdictKind::Uncertain;
  if (validMain == 0) return AnchorVerdictKind::Diff;
  return AnchorVerdictKind::Uncertain;
}

double temporalAnchorSimilarityV2(const TemporalAnchorIndex& a, const TemporalAnchorIndex& b,
                                  TemporalAnchorV2Result& out) {
  const auto t0 = std::chrono::steady_clock::now();
  out = TemporalAnchorV2Result();
  out.strategyId = TemporalAnchorV2Result::kV2StrategyId;
  out.cacheIdentity = "anchor-v2";
  if (!a.built || !b.built || a.seq.empty() || b.seq.empty()) {
    out.verdict = AnchorVerdictKind::Uncertain;
    return 0;
  }
  const double kWindow = 180.0;
  out.leadingSearchWindow = kWindow;
  // Forward opening probe (textured run) both directions, V2 strict variant.
  std::vector<StartCandidate> cands, rev;
  for (double L : {3.0, 5.0, 10.0}) {
    auto c = findLeadingOffsetV2(a, b, L, kWindow);
    if (!c.empty() && (cands.empty() || c.front().confidence > cands.front().confidence)) {
      cands = std::move(c);
      if (cands.front().confidence >= 0.9) break;
    }
  }
  for (double L : {3.0, 5.0, 10.0}) {
    auto c = findLeadingOffsetV2(b, a, L, kWindow);
    if (!c.empty() && (rev.empty() || c.front().confidence > rev.front().confidence)) {
      rev = std::move(c);
      if (rev.front().confidence >= 0.9) break;
    }
  }
  out.startMatchConfidence = std::max(cands.empty() ? 0.0 : cands.front().confidence,
                                      rev.empty() ? 0.0 : rev.front().confidence);
  std::vector<std::pair<double, double>> starts = {{0.0, 0.0}};
  if (!cands.empty() && !rev.empty()) {
    starts.clear();
    for (const auto& cb : cands)
      for (const auto& ca : rev) starts.emplace_back(ca.startSec, cb.startSec);
    if (starts.size() > 4) starts.resize(4);
    starts.emplace_back(0.0, 0.0);
  } else if (!cands.empty()) {
    starts = {{0.0, cands.front().startSec}, {0.0, 0.0}};
  } else if (!rev.empty()) {
    starts = {{rev.front().startSec, 0.0}, {0.0, 0.0}};
  }
  out.estimatedContentA = std::max(0.0, a.duration - starts.front().first);
  out.estimatedContentB = std::max(0.0, b.duration - starts.front().second);
  out.contentRatio = (std::min(out.estimatedContentA, out.estimatedContentB) > 0)
                         ? std::max(out.estimatedContentA, out.estimatedContentB) /
                               std::min(out.estimatedContentA, out.estimatedContentB)
                         : 1.0;
  const auto t1 = std::chrono::steady_clock::now();
  double bestScore = -1;
  TemporalAnchorV2Result best;
  best.strategyId = TemporalAnchorV2Result::kV2StrategyId;
  best.cacheIdentity = "anchor-v2";
  for (const auto& st : starts) {
    TemporalAnchorV2Result r;
    r.strategyId = TemporalAnchorV2Result::kV2StrategyId;
    r.cacheIdentity = "anchor-v2";
    r.leadingSearchWindow = kWindow;
    r.startA = st.first;
    r.startB = st.second;
    r.alignmentOffset = st.second - st.first;
    r.startMatchConfidence = out.startMatchConfidence;
    r.estimatedContentA = std::max(0.0, a.duration - st.first);
    r.estimatedContentB = std::max(0.0, b.duration - st.second);
    r.contentRatio = (std::min(r.estimatedContentA, r.estimatedContentB) > 0)
                         ? std::max(r.estimatedContentA, r.estimatedContentB) /
                               std::min(r.estimatedContentA, r.estimatedContentB)
                         : 1.0;
    const double off = r.alignmentOffset;
    // Length-policy forward refs (§5) + reverse refs from the ending side.
    const EndingProbe ea = findEndingProbe(a);
    const EndingProbe eb = findEndingProbe(b);
    const double cEndA = (ea.mode == EndingMode::PreCredits) ? ea.mainEndSec : a.duration;
    const double cEndB = (eb.mode == EndingMode::PreCredits) ? eb.mainEndSec : b.duration;
    std::vector<double> refs = anchorCentersForDuration(st.first, cEndA);
    // Reverse refs mapped into A coordinates (§3.3-§3.4 cross-check).
    std::vector<double> revRefs;
    for (double rb : anchorCentersForDuration(0.0, cEndB)) {
      const double ra = rb - off;
      if (ra > st.first && ra < cEndA) revRefs.push_back(ra);
    }
    for (double t : revRefs) {
      bool dup = false;
      for (double e : refs)
        if (std::abs(e - t) < 5.0) {
          dup = true;
          break;
        }
      if (!dup) refs.push_back(t);  // independent evidence only (§6.4)
    }
    int validMain = 0, coveredMain = 0, coveredFailed = 0;
    double offMin = 1e9, offMax = -1e9;
    std::vector<std::pair<double, double>> drifted;
    for (double t : refs) {
      int hv = 0;
      AnchorVerdict v = checkAnchorV2(a, b, t, off, hv);
      v.tRef = t;
      r.anchors.push_back(v);
      if (v.valid) {
        ++validMain;
        ++coveredMain;
        offMin = std::min(offMin, v.offset);
        offMax = std::max(offMax, v.offset);
      } else if (v.covered && !v.lowInfo) {
        ++coveredMain;
        ++coveredFailed;
        int fh = 0;
        const double found = findAnchorOffsetNear(a, b, t, t + off, 120.0, fh);
        if (fh >= 2 && std::isfinite(found) && std::abs(found - off) > kAnchorOffsetTol)
          drifted.emplace_back(t, found);
      } else if (v.lowInfo) {
        ++r.lowInformationMatches;
      } else if (v.covered) {
        ++coveredMain;
      }
    }
    r.validAnchorCount = validMain;
    r.coveredFailedAnchors = coveredFailed;
    for (const auto& d : drifted) {
      offMin = std::min(offMin, d.second);
      offMax = std::max(offMax, d.second);
    }
    r.offsetDrift = (validMain > 0 || !drifted.empty()) ? (offMax - offMin) : 1e9;
    const bool consistent = validMain > 0 && r.offsetDrift <= kAnchorOffsetTol;
    r.offsetDrifted = !drifted.empty() && r.offsetDrift > kAnchorOffsetTol;
    // §3.6 ending anchor (pre-credits main scene or last content): support only.
    AnchorVerdict ev;
    {
      int hv = 0;
      const double et = std::min(cEndA, cEndB - off) - 6.0;
      if (et > st.first) {
        ev = checkAnchorV2(a, b, et, off, hv);
      } else {
        ev.covered = false;
      }
    }
    bool endDarkFlat = false;
    {
      double endMean = 0;
      int endN = 0;
      for (const auto& f : a.seq)
        if (f.timestamp >= a.duration - 9.0) {
          endMean += f.mean;
          ++endN;
        }
      if (endN > 0) endMean /= endN;
      endDarkFlat = (endMean < 25.0);
    }
    const bool endSupport = ev.valid && !endDarkFlat;
    if (ev.valid) {
      r.endingMode = endDarkFlat ? EndingMode::Uncertain : ea.mode;
      r.anchors.push_back(ev);
    } else {
      r.endingMode = EndingMode::Uncertain;
    }
    // Segments (§4): single piece by default; split at the largest drift gap.
    r.segments.push_back({st.first, cEndA, st.second, cEndB, off, validMain});
    if (r.offsetDrifted && !drifted.empty()) {
      double bt = drifted.front().first;
      r.segments.clear();
      int v0 = 0, v1 = 0;
      for (const auto& av : r.anchors)
        if (av.valid) (av.tRef < bt ? v0 : v1)++;
      r.segments.push_back({st.first, bt, st.second, bt + off, off, v0});
      r.segments.push_back({bt, cEndA, bt + drifted.front().second,
                            cEndB, drifted.front().second, v1});
    }
    // Reverse offset from the ending side (§3.4 cross-check).
    {
      int hv = 0;
      const double etA = cEndA - 6.0;
      const double etB = cEndB - 6.0;
      if (etA > st.first && etB > st.second) {
        AnchorVerdict rv = checkAnchorV2(a, b, etA, etB - etA, hv);
        if (rv.valid) {
          r.reverseOffset = rv.offset;
          r.reverseValidAnchors = 1;
          r.reverseConsistent = std::abs(rv.offset - off) <= kAnchorOffsetTol;
        }
      }
    }
    // §6.7-§6.8 unified verdict. Thin = short, flat/dark-majority, extreme
    // content ratio with thin evidence, or no coverage. HDR/SDR-level
    // darkness without counter-evidence stays UNCERTAIN (never DIFF).
    std::size_t flatN = 0, darkN = 0;
    for (const auto& f : a.seq) {
      if (f.lowInfo) ++flatN;
      if (f.mean < 25.0) ++darkN;
    }
    std::size_t flatB = 0, darkB = 0;
    for (const auto& f : b.seq) {
      if (f.lowInfo) ++flatB;
      if (f.mean < 25.0) ++darkB;
    }
    const bool flatMajor = (flatN * 2 > a.seq.size()) || (flatB * 2 > b.seq.size());
    const bool darkMajor = (darkN * 2 > a.seq.size()) || (darkB * 2 > b.seq.size());
    const bool thin = (validMain < 2) && (refs.size() < 2 || a.duration < 60 || b.duration < 60 ||
                                          flatMajor || darkMajor || r.contentRatio > 3.0);
    r.contentRatioFlag = (r.contentRatio > 3.0);
    const double score = anchorVerdictScore(validMain, consistent, endSupport);
    r.score = score;
    r.verdict = anchorVerdictFor(validMain, consistent, coveredFailed, r.offsetDrifted,
                                 thin, r.contentRatioFlag, score);
    if (r.score > bestScore) {
      bestScore = r.score;
      best = r;
    }
  }
  out = best;
  out.anchorFrames = a.seq.size() + b.seq.size();
  const auto t2 = std::chrono::steady_clock::now();
  out.alignmentMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
  out.compareMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
  return out.score;
}
}  // namespace msf
