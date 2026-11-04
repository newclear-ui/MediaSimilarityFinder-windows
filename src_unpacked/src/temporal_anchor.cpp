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
}  // namespace msf
