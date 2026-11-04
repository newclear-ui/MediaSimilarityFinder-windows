// 0.9.4.173 TemporalAnchorV2 pins (synthetic only, no fixtures/FFmpeg).
// Covers: bidirectional search, reverse ending search + PreCredits,
// length-policy placement, per-segment offsets, unified score mapping,
// dark-content UNCERTAIN, mid-roll UNCERTAIN (not Diff), HDR/SDR-level
// darkness, shared-ending non-confirmation, ≥2 non-low-frame rule,
// no double-counting, content-ratio flag. V1 semantics untouched.
#include "temporal_anchor.h"
#include "video_decoder.h"

#include <iostream>
#include <vector>

namespace {
std::uint64_t splitmix64(std::uint64_t& s) {
  std::uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 27);
}
int hamdist(std::uint64_t a, std::uint64_t b) {
  std::uint64_t x = a ^ b;
  int n = 0;
  while (x) {
    x &= x - 1;
    ++n;
  }
  return n;
}
msf::AnchorFrame mkA(double t, std::uint64_t h, bool low = false, double mean = 128.0) {
  msf::AnchorFrame a;
  a.timestamp = t;
  a.phash = h;
  a.mirror = ~h;
  a.mean = low ? 8.0 : mean;
  a.sd = low ? 0.0 : 20.0;
  a.delta = 10.0;
  a.edge = low ? 0.5 : 6.0;
  a.lowInfo = low;
  return a;
}
msf::AnchorFrame flatA(double t) {
  msf::AnchorFrame a;
  a.timestamp = t;
  a.phash = 0xAAAAAAAAAAAAAAAAULL;
  a.mirror = ~a.phash;
  a.mean = 8.0;
  a.sd = 0.0;
  a.delta = 0.0;
  a.edge = 0.2;
  a.lowInfo = true;
  return a;
}
std::vector<std::uint64_t> hashes(std::uint64_t seed, int n) {
  for (int attempt = 0;; ++attempt) {
    std::vector<std::uint64_t> v;
    std::uint64_t s = seed + (std::uint64_t)attempt * 1000003ULL;
    for (int i = 0; i < n; ++i) v.push_back(splitmix64(s));
    bool ok = true;
    for (auto h : v) {
      const int d = hamdist(h, 0xAAAAAAAAAAAAAAAAULL);
      if (d < 14 || d > 50) {
        ok = false;
        break;
      }
    }
    for (std::size_t i = 0; ok && i < v.size(); ++i)
      for (std::size_t j = i + 1; ok && j < v.size(); ++j)
        if (hamdist(v[i], v[j]) < 14) ok = false;
    if (ok) return v;
  }
}
msf::TemporalAnchorIndex seqIdx(const std::vector<std::uint64_t>& hs, double t0, double step,
                                double dur) {
  msf::TemporalAnchorIndex idx;
  idx.duration = dur;
  for (std::size_t i = 0; i < hs.size(); ++i) idx.seq.push_back(mkA(t0 + i * step, hs[i]));
  idx.built = !idx.seq.empty();
  return idx;
}
int fails = 0;
void check(bool ok, const char* msg, int& code) {
  if (!ok) {
    std::cerr << "FAIL[" << code << "] " << msg << "\n";
    ++fails;
  }
  ++code;
}
}  // namespace

int main() {
  int code = 100;
  // V2-1. Score/verdict mapping table (§6.8, single definition).
  {
    check(msf::anchorVerdictScore(2, true, true) == 100.0, "score 2+cons+end", code);
    check(msf::anchorVerdictScore(2, true, false) == 90.0, "score 2+cons", code);
    check(msf::anchorVerdictScore(1, true, true) == 60.0, "score 1+cons+end", code);
    check(msf::anchorVerdictFor(2, true, false, false, false, false, 95.0) == msf::AnchorVerdictKind::Dup, "map DUP", code);
    check(msf::anchorVerdictFor(2, true, false, false, false, false, 85.0) != msf::AnchorVerdictKind::Dup, "map sub-87.5 no DUP", code);
    check(msf::anchorVerdictFor(2, false, false, false, false, false, 80.0) != msf::AnchorVerdictKind::Dup, "map inconsistent no DUP", code);
    check(msf::anchorVerdictFor(2, true, true, false, false, false, 95.0) == msf::AnchorVerdictKind::Uncertain, "map covered-failed Uncertain", code);
    check(msf::anchorVerdictFor(1, true, false, true, false, false, 95.0) == msf::AnchorVerdictKind::Uncertain, "map drifted Uncertain", code);
    check(msf::anchorVerdictFor(2, true, false, false, false, true, 95.0) == msf::AnchorVerdictKind::Uncertain, "map ratio Uncertain", code);
    check(msf::anchorVerdictFor(0, false, false, false, false, false, 0.0) == msf::AnchorVerdictKind::Diff, "map empty Diff", code);
    check(msf::anchorVerdictFor(0, false, false, false, true, false, 0.0) == msf::AnchorVerdictKind::Uncertain, "map thin Uncertain", code);
  }
  // V2-2. ≥2 non-low frames required: single-matching-frame anchors are not evidence.
  {
    auto ha = hashes(11, 700), hb = hashes(12, 700);
    // identical except anchor windows share exactly ONE textured frame
    auto h = hashes(13, 700);
    msf::TemporalAnchorIndex a = seqIdx(h, 0.0, 1.0, 700.0);
    msf::TemporalAnchorIndex b = seqIdx(h, 0.0, 1.0, 700.0);
    // flatten everything except single frames at 5,305,605 to low-info resembling (but mismatched) content
    for (std::size_t i = 0; i < a.seq.size(); ++i) {
      const double t = a.seq[i].timestamp;
      bool keep = (std::abs(t - 5.0) < 0.1) || (std::abs(t - 305.0) < 0.1);
      if (!keep) {
        a.seq[i] = flatA(t);
      }
    }
    for (std::size_t i = 0; i < b.seq.size(); ++i) {
      const double t = b.seq[i].timestamp;
      bool keep = (std::abs(t - 5.0) < 0.1) || (std::abs(t - 305.0) < 0.1);
      if (!keep) {
        b.seq[i] = flatA(t);
      }
    }
    a.seq[(std::size_t)305] = mkA(305.0, ha[0]);
    b.seq[(std::size_t)305] = mkA(305.0, hb[0]);  // different content, single textured frame each
    msf::TemporalAnchorV2Result r;
    msf::temporalAnchorSimilarityV2(a, b, r);
    check(r.verdict != msf::AnchorVerdictKind::Dup, "single-frame anchor no DUP", code);
  }
  // V2-3. Low-info frames never count as match evidence.
  {
    auto h = hashes(21, 60);
    msf::TemporalAnchorIndex a = seqIdx(h, 0.0, 1.0, 60.0);
    msf::TemporalAnchorIndex b;
    b.duration = 60.0;
    for (int i = 0; i < 60; ++i) b.seq.push_back(flatA((double)i));
    b.built = true;
    msf::TemporalAnchorV2Result r;
    msf::temporalAnchorSimilarityV2(a, b, r);
    check(r.validAnchorCount == 0, "flat8808 no valid anchors", code);
    check(r.verdict == msf::AnchorVerdictKind::Uncertain, "flat vs textured Uncertain", code);
  }
  // V2-4. Length policy placement (§5).
  {
    auto micro = msf::anchorCentersForDuration(0.0, 10.0);
    check(micro.size() == 3, "micro 3 anchors", code);
    auto mid = msf::anchorCentersForDuration(0.0, 1500.0);
    check(mid.size() == 4, "mid 4 anchors", code);
    auto lng = msf::anchorCentersForDuration(0.0, 7800.0);
    check(lng.size() >= 6, "long fwd+rev anchors", code);
    bool hasRev = false, hasFwd = false;
    for (double t : lng) {
      if (t < 1000.0) hasFwd = true;
      if (t > 6800.0) hasRev = true;
    }
    check(hasFwd && hasRev, "long both ends", code);
    auto empty = msf::anchorCentersForDuration(10.0, 5.0);
    check(empty.empty(), "empty span", code);
  }
  // V2-5. Reverse ending search: dark still + edgy run -> PreCredits.
  {
    msf::TemporalAnchorIndex idx;
    idx.duration = 900.0;
    auto h = hashes(31, 900);
    for (int i = 0; i < 860; ++i) idx.seq.push_back(mkA((double)i, h[(std::size_t)i], false, 120.0));
    // 40 s of dark credits-like frames with strong edges (scrolling text proxy).
    // Non-lowInfo (textured) so the detector may use them as evidence.
    auto hc = hashes(32, 40);
    for (int i = 0; i < 40; ++i) {
      auto f = mkA(860.0 + i, hc[(std::size_t)i], false, 20.0);
      f.edge = 12.0;
      f.delta = 1.0;
      idx.seq.push_back(f);
    }
    idx.built = true;
    msf::EndingProbe e = msf::findEndingProbe(idx);
    check(e.mode == msf::EndingMode::PreCredits, "credits detected", code);
    check(e.creditsStartSec >= 855.0 && e.creditsStartSec <= 865.0, "credits start", code);
    check(e.mainEndSec < e.creditsStartSec, "main end before credits", code);
  }
  // V2-6. Dark-flat tail without edge -> Uncertain, never PreCredits.
  {
    msf::TemporalAnchorIndex idx;
    idx.duration = 900.0;
    auto h = hashes(41, 860);
    for (int i = 0; i < 860; ++i) idx.seq.push_back(mkA((double)i, h[(std::size_t)i], false, 120.0));
    for (int i = 0; i < 40; ++i) idx.seq.push_back(flatA(860.0 + i));
    idx.built = true;
    msf::EndingProbe e = msf::findEndingProbe(idx);
    check(e.mode != msf::EndingMode::PreCredits, "dark flat not credits", code);
    check(e.mode == msf::EndingMode::Uncertain, "dark flat uncertain", code);
  }
  // V2-7. Bidirectional DUP: same content + 45 s leader, 700 s main.
  {
    auto h = hashes(51, 700);
    msf::TemporalAnchorIndex ref = seqIdx(h, 0.0, 1.0, 700.0);
    msf::TemporalAnchorIndex cand;
    cand.duration = 745.0;
    for (int i = 0; i < 45; ++i) cand.seq.push_back(flatA((double)i));
    for (std::size_t i = 0; i < h.size(); ++i) cand.seq.push_back(mkA(45.0 + i, h[i]));
    cand.built = true;
    msf::TemporalAnchorV2Result r;
    double s = msf::temporalAnchorSimilarityV2(ref, cand, r);
    check(r.verdict == msf::AnchorVerdictKind::Dup, "V2 leader DUP", code);
    check(std::abs(r.alignmentOffset - 45.0) < 2.5, "V2 leader offset", code);
    check(s >= 87.5, "V2 leader score", code);
    check(r.validAnchorCount >= 2, "V2 leader anchors", code);
  }
  // V2-8. Mid-roll 30 s insertion -> segments + UNCERTAIN, never forced DUP.
  {
    auto h = hashes(61, 700);
    msf::TemporalAnchorIndex ref = seqIdx(h, 0.0, 1.0, 700.0);
    msf::TemporalAnchorIndex cand;
    cand.duration = 730.0;
    auto filler = hashes(62, 30);
    for (int i = 0; i < 400; ++i) cand.seq.push_back(mkA((double)i, h[(std::size_t)i]));
    for (int i = 0; i < 30; ++i) cand.seq.push_back(mkA(400.0 + i, filler[(std::size_t)i]));
    for (int i = 400; i < 700; ++i) cand.seq.push_back(mkA(30.0 + i, h[(std::size_t)i]));
    cand.built = true;
    msf::TemporalAnchorV2Result r;
    msf::temporalAnchorSimilarityV2(ref, cand, r);
    check(r.verdict == msf::AnchorVerdictKind::Uncertain, "midroll Uncertain", code);
    check(r.offsetDrifted || r.offsetDrift > 2.0, "midroll drift recorded", code);
    check(r.segments.size() >= 2, "midroll segments", code);
  }
  // V2-9. Dark content pair with no counter-evidence -> UNCERTAIN, not DIFF.
  {
    auto ha = hashes(71, 300), hb = hashes(72, 300);
    msf::TemporalAnchorIndex a, b;
    a.duration = b.duration = 300.0;
    for (int i = 0; i < 300; ++i) {
      a.seq.push_back(mkA((double)i, ha[(std::size_t)i], true, 12.0));
      b.seq.push_back(mkA((double)i, hb[(std::size_t)i], true, 14.0));
    }
    a.built = b.built = true;
    msf::TemporalAnchorV2Result r;
    msf::temporalAnchorSimilarityV2(a, b, r);
    check(r.verdict == msf::AnchorVerdictKind::Uncertain, "dark pair Uncertain", code);
  }
  // V2-10. Shared ending only -> not DUP.
  {
    auto ha = hashes(81, 691), hb = hashes(82, 691), ht = hashes(83, 9);
    msf::TemporalAnchorIndex a, b;
    a.duration = b.duration = 700.0;
    for (int i = 0; i < 691; ++i) {
      a.seq.push_back(mkA((double)i, ha[(std::size_t)i]));
      b.seq.push_back(mkA((double)i, hb[(std::size_t)i]));
    }
    for (int i = 0; i < 9; ++i) {
      a.seq.push_back(mkA(691.0 + i, ht[(std::size_t)i]));
      b.seq.push_back(mkA(691.0 + i, ht[(std::size_t)i]));
    }
    a.built = b.built = true;
    msf::TemporalAnchorV2Result r;
    msf::temporalAnchorSimilarityV2(a, b, r);
    check(r.verdict != msf::AnchorVerdictKind::Dup, "shared ending not DUP", code);
  }
  // V2-11. Content-ratio guard: tiny-vs-huge with thin evidence -> Uncertain.
  {
    auto h = hashes(91, 60);
    msf::TemporalAnchorIndex a = seqIdx(h, 0.0, 1.0, 60.0);
    auto hb = hashes(92, 1800);
    msf::TemporalAnchorIndex b = seqIdx(hb, 0.0, 1.0, 1800.0);
    // force one overlapping window
    for (int i = 0; i < 60; ++i) b.seq[(std::size_t)i] = mkA((double)i, h[(std::size_t)i]);
    msf::TemporalAnchorV2Result r;
    msf::temporalAnchorSimilarityV2(a, b, r);
    check(r.contentRatio > 3.0, "ratio computed", code);
    check(r.verdict == msf::AnchorVerdictKind::Uncertain, "ratio mismatch Uncertain", code);
  }
  // V2-12. No double counting: overlapping fwd/rev anchor counted once.
  {
    auto h = hashes(101, 700);
    msf::TemporalAnchorIndex ref = seqIdx(h, 0.0, 1.0, 700.0);
    msf::TemporalAnchorIndex cand = seqIdx(h, 0.0, 1.0, 700.0);
    msf::TemporalAnchorV2Result r;
    msf::temporalAnchorSimilarityV2(ref, cand, r);
    check(r.verdict == msf::AnchorVerdictKind::Dup, "identical DUP", code);
    // fwd anchors at 5,305,605 + ending; reverse may add near-duplicates:
    // count must stay bounded (no explosion from dup windows).
    check(r.validAnchorCount <= 8, "no anchor explosion", code);
  }
  if (fails == 0) std::cout << "video_temporal_anchor_v2=ok\n";
  return fails == 0 ? 0 : 1;
}
