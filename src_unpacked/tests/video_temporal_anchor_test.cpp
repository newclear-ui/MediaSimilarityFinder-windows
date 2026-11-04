// 0.9.4.172 TemporalAnchorV1 pins (synthetic only, no fixtures/FFmpeg).
// AnchorFrame hashes are directly controlled (splitmix64 sequences with a
// verified minimum pairwise distance), so texture luck cannot influence
// the verdict logic under test. One index-build case still exercises the
// real VideoFrame -> index path.
#include "temporal_anchor.h"
#include "video_decoder.h"

#include <iostream>
#include <vector>

namespace {
// Deterministic 64-bit stream; pairwise distances verified at runtime.
std::uint64_t splitmix64(std::uint64_t& s) {
  std::uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
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
msf::AnchorFrame mkA(double t, std::uint64_t h, bool low = false) {
  msf::AnchorFrame a;
  a.timestamp = t;
  a.phash = h;
  a.mirror = ~h;
  a.mean = low ? 8.0 : 128.0;
  a.sd = low ? 0.0 : 20.0;
  a.delta = 10.0;
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
  a.lowInfo = true;
  return a;
}
msf::TemporalAnchorIndex seqIdx(const std::vector<std::uint64_t>& hs, double t0, double step,
                                double dur) {
  msf::TemporalAnchorIndex idx;
  idx.duration = dur;
  for (std::size_t i = 0; i < hs.size(); ++i) {
    auto a = mkA(t0 + i * step, hs[i]);
    if (i) a.delta = 12.0;
    idx.seq.push_back(a);
  }
  idx.built = !idx.seq.empty();
  return idx;
}
std::vector<std::uint64_t> hashes(std::uint64_t seed, int n) {
  // Rejection-sampled: pairwise distance >= 14 bits and vs the flat hash
  // in [14, 50] (mirrors stay matchable only to their own counterpart).
  // Deterministic for a given seed.
  const std::uint64_t FLAT = 0xAAAAAAAAAAAAAAAAULL;
  for (int attempt = 0;; ++attempt) {
    std::vector<std::uint64_t> v;
    std::uint64_t s = seed + (std::uint64_t)attempt * 1000003ULL;
    for (int i = 0; i < n; ++i) v.push_back(splitmix64(s));
    bool ok = true;
    for (auto h : v) {
      const int d = hamdist(h, FLAT);
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
  int code = 1;
  // 0. Hash hygiene: distinct content hashes stay far apart.
  {
    auto v = hashes(12345, 40);
    int mind = 64;
    for (std::size_t i = 0; i < v.size(); ++i)
      for (std::size_t j = i + 1; j < v.size(); ++j) mind = std::min(mind, hamdist(v[i], v[j]));
    for (auto h : v) mind = std::min(mind, hamdist(h, 0xAAAAAAAAAAAAAAAAULL));
    check(mind >= 14, "hash hygiene", code);
  }
  // 1. Plan shape: dense 1 fps substrate (searchable at any offset).
  {
    auto p = msf::buildTemporalAnchorPlan(7800.0);
    bool sorted = true;
    for (std::size_t i = 1; i < p.size(); ++i)
      if (p[i] < p[i - 1] - 1e-9) sorted = false;
    check(sorted, "plan sorted", code);
    check(p.size() == 7801, "plan density", code);
    auto has = [&](double t) {
      for (double x : p)
        if (std::abs(x - t) < 1e-3) return true;
      return false;
    };
    check(has(0.0) && has(10.0), "plan opening", code);
    check(has(297.0) && has(300.0) && has(303.0), "plan 5min anchor", code);
    check(has(597.0) && has(900.0) && has(903.0), "plan 10/15min anchors", code);
    check(has(1200.0) && has(7797.0), "plan dense mid/end", code);
    auto p60 = msf::buildTemporalAnchorPlan(60.0);
    check(p60.size() == 61 && p60.front() == 0.0, "short plan", code);
  }
  // 2. Leading search finds a 30 s black leader (bidirectional, earliest wins ties).
  {
    auto h = hashes(7, 12);
    msf::TemporalAnchorIndex ref = seqIdx(h, 0.0, 1.0, 12.0);
    msf::TemporalAnchorIndex cand;
    cand.duration = 42.0;
    for (int i = 0; i < 30; ++i) cand.seq.push_back(flatA((double)i));
    for (std::size_t i = 0; i < h.size(); ++i) {
      auto a = mkA(30.0 + i, h[i]);
      cand.seq.push_back(a);
    }
    cand.built = true;
    auto fw = msf::findLeadingOffset(ref, cand, 5.0, 180.0);
    check(!fw.empty(), "leader found", code);
    if (!fw.empty()) check(std::abs(fw.front().startSec - 30.0) < 1.6, "leader offset", code);
    auto rev = msf::findLeadingOffset(cand, ref, 5.0, 180.0);
    // Symmetric: the candidate's textured run (from 30 s) is found in the
    // reference at 0. The candidate's own black opening is skipped by design.
    check(!rev.empty(), "reverse found", code);
    if (!rev.empty()) check(std::abs(rev.front().startSec - 0.0) < 1.6, "reverse offset", code);
  }
  // 3. Single-frame coincidence rejected.
  {
    auto h = hashes(21, 12);
    msf::TemporalAnchorIndex ref = seqIdx(h, 0.0, 1.0, 12.0);
    msf::TemporalAnchorIndex cand;
    cand.duration = 12.0;
    cand.seq.push_back(mkA(0.0, h[0]));
    auto g = hashes(99, 11);
    for (int i = 1; i < 12; ++i) cand.seq.push_back(mkA((double)i, g[(std::size_t)i - 1]));
    cand.built = true;
    check(msf::findLeadingOffset(ref, cand, 5.0, 180.0).empty(), "single frame rejected", code);
  }
  // 4. Reversed order rejected.
  {
    auto h = hashes(33, 12);
    msf::TemporalAnchorIndex ref = seqIdx(h, 0.0, 1.0, 12.0);
    msf::TemporalAnchorIndex cand;
    cand.duration = 12.0;
    for (int i = 11; i >= 0; --i) cand.seq.push_back(mkA(11.0 - i, h[(std::size_t)i]));
    cand.built = true;
    check(msf::findLeadingOffset(ref, cand, 5.0, 180.0).empty(), "reversed rejected", code);
  }
  // 5. Mirror tolerated at full DUP (400 s: post-start + 5-min anchors).
  {
    auto h = hashes(55, 400);
    msf::TemporalAnchorIndex ref = seqIdx(h, 0.0, 1.0, 400.0);
    msf::TemporalAnchorIndex mir = ref;
    for (auto& a : mir.seq) std::swap(a.phash, a.mirror);
    msf::TemporalAnchorResult r;
    double s = msf::temporalAnchorSimilarity(ref, mir, r);
    check(s >= 87.5, "mirror score", code);
    check(r.verdict == msf::AnchorVerdictKind::Dup, "mirror verdict", code);
  }
  // 6. Same content + 30 s leader on 700 s -> DUP at offset ~30.
  {
    auto h = hashes(77, 700);
    msf::TemporalAnchorIndex ref = seqIdx(h, 0.0, 1.0, 700.0);
    msf::TemporalAnchorIndex cand;
    cand.duration = 730.0;
    for (int i = 0; i < 30; ++i) cand.seq.push_back(flatA((double)i));
    for (std::size_t i = 0; i < h.size(); ++i) cand.seq.push_back(mkA(30.0 + i, h[i]));
    cand.built = true;
    msf::TemporalAnchorResult r;
    double s = msf::temporalAnchorSimilarity(ref, cand, r);
    check(r.verdict == msf::AnchorVerdictKind::Dup, "leader DUP verdict", code);
    check(std::abs(r.alignmentOffset - 30.0) < 2.5, "leader offset", code);
    check(s >= 87.5, "leader score", code);
    check(r.validAnchorCount >= 2, "leader anchors", code);
  }
  // 7. Different content -> Diff.
  {
    auto a = seqIdx(hashes(101, 700), 0.0, 1.0, 700.0);
    auto b = seqIdx(hashes(202, 700), 0.0, 1.0, 700.0);
    msf::TemporalAnchorResult r;
    msf::temporalAnchorSimilarity(a, b, r);
    check(r.verdict == msf::AnchorVerdictKind::Diff, "different Diff", code);
  }
  // 8. All-flat -> UNCERTAIN (never forced).
  {
    msf::TemporalAnchorIndex r1, r2;
    r1.duration = r2.duration = 20.0;
    for (int i = 0; i < 20; ++i) {
      r1.seq.push_back(flatA((double)i));
      r2.seq.push_back(flatA((double)i));
    }
    r1.built = r2.built = true;
    msf::TemporalAnchorResult r;
    msf::temporalAnchorSimilarity(r1, r2, r);
    check(r.verdict == msf::AnchorVerdictKind::Uncertain, "flat UNCERTAIN", code);
  }
  // 9. Ending-only match never confirms DUP.
  {
    auto ha = hashes(303, 691), hb = hashes(404, 691), ht = hashes(505, 9);
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
    msf::TemporalAnchorResult r;
    msf::temporalAnchorSimilarity(a, b, r);
    check(r.verdict != msf::AnchorVerdictKind::Dup, "ending-only not DUP", code);
  }
  // 10. Shared black leader, different mains -> not DUP.
  {
    auto ha = hashes(601, 300), hb = hashes(602, 300);
    msf::TemporalAnchorIndex a, b;
    a.duration = b.duration = 330.0;
    for (int i = 0; i < 30; ++i) {
      a.seq.push_back(flatA((double)i));
      b.seq.push_back(flatA((double)i));
    }
    for (int i = 0; i < 300; ++i) {
      a.seq.push_back(mkA(30.0 + i, ha[(std::size_t)i]));
      b.seq.push_back(mkA(30.0 + i, hb[(std::size_t)i]));
    }
    a.built = b.built = true;
    msf::TemporalAnchorResult r;
    msf::temporalAnchorSimilarity(a, b, r);
    check(r.verdict != msf::AnchorVerdictKind::Dup, "shared leader not DUP", code);
  }
  // 11. Offset drift (mid-content insertion) reported, not forced.
  {
    auto h = hashes(707, 700);
    msf::TemporalAnchorIndex ref = seqIdx(h, 0.0, 1.0, 700.0);
    msf::TemporalAnchorIndex cand;
    cand.duration = 760.0;
    for (int i = 0; i < 30; ++i) cand.seq.push_back(flatA((double)i));
    for (int i = 0; i < 400; ++i) cand.seq.push_back(mkA(30.0 + i, h[(std::size_t)i]));
    for (int i = 400; i < 700; ++i) cand.seq.push_back(mkA(60.0 + i, h[(std::size_t)i]));
    cand.built = true;
    msf::TemporalAnchorResult r;
    msf::temporalAnchorSimilarity(ref, cand, r);
    check(r.offsetDrift > msf::kAnchorOffsetTol, "drift detected", code);
    check(r.verdict != msf::AnchorVerdictKind::Dup, "drift not forced DUP", code);
  }
  // 12. Shorts rule: thin short content -> UNCERTAIN.
  {
    msf::TemporalAnchorIndex a, b;
    a.duration = b.duration = 35.0;
    for (int i = 0; i < 30; ++i) {
      a.seq.push_back(flatA((double)i));
      b.seq.push_back(flatA((double)i));
    }
    auto ha = hashes(801, 5), hb = hashes(802, 5);
    for (int i = 0; i < 5; ++i) {
      a.seq.push_back(mkA(30.0 + i, ha[(std::size_t)i]));
      b.seq.push_back(mkA(30.0 + i, hb[(std::size_t)i]));
    }
    a.built = b.built = true;
    msf::TemporalAnchorResult r;
    msf::temporalAnchorSimilarity(a, b, r);
    check(r.verdict == msf::AnchorVerdictKind::Uncertain, "short thin UNCERTAIN", code);
  }
  // 13. Real index-build path (VideoFrame -> index, low-info flags).
  {
    msf::VideoFrame flat, tex;
    flat.timestamp = 0;
    flat.width = tex.width = 32;
    flat.height = tex.height = 32;
    flat.gray.assign(1024, 128);
    tex.timestamp = 1.0;
    tex.gray.assign(1024, 0);
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 32; ++x)
        tex.gray[(std::size_t)y * 32 + x] = (std::uint8_t)(((x + y) % 2) ? 230 : 25);
    std::vector<msf::VideoFrame> fr{flat, tex};
    auto idx = msf::makeTemporalAnchorIndex(fr, 2.0);
    check(idx.built && idx.seq.size() == 2, "index built", code);
    if (idx.seq.size() == 2) {
      check(idx.seq[0].lowInfo && !idx.seq[1].lowInfo, "index lowInfo flags", code);
    }
  }
  if (fails == 0) std::cout << "video_temporal_anchor=ok\n";
  return fails == 0 ? 0 : 1;
}
