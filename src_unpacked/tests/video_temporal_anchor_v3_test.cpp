// TemporalAnchorV3 pins (synthetic only, no fixtures/FFmpeg).
// Covers: duration-band gate boundaries, tolerant 3-of-4 ordered run
// (1 skip allowed, 2 misses rejected), offset refinement to the true
// offset, ending-first DUP on synthetic same-content + leader, dark-pair
// UNCERTAIN (never DIFF), cross non-DUP. V1/V2 semantics untouched.
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
  int code = 300;
  // V3-1. Duration-band gate.
  {
    check(msf::anchorBandPass(60.0, 90.0), "shorts abs in", code);
    check(!msf::anchorBandPass(60.0, 200.0), "shorts abs out", code);
    check(msf::anchorBandPass(1800.0, 1980.0), "long ratio in", code);
    check(!msf::anchorBandPass(3600.0, 5400.0), "long ratio out", code);
    check(msf::anchorBandPass(1800.0, 2160.0), "ratio boundary 1.2", code);
    check(!msf::anchorBandPass(1800.0, 2161.0), "ratio 1.2+ out", code);
    check(!msf::anchorBandPass(0.0, 100.0), "zero duration out", code);
  }
  // V3-2. Tolerant 3-of-4 ordered run: 1 miss ok, 2 misses rejected.
  {
    auto h = hashes(501, 8);
    msf::TemporalAnchorIndex ref = seqIdx(h, 0.0, 1.0, 60.0);
    msf::TemporalAnchorIndex cand;
    cand.duration = 90.0;
    for (int i = 0; i < 30; ++i) cand.seq.push_back(flatA((double)i));
    for (std::size_t i = 0; i < h.size(); ++i) {
      auto f = mkA(30.0 + i, h[i]);
      if (i == 1) f.phash = hashes(999, 1)[0], f.mirror = ~f.phash;  // 1 corrupted
      cand.seq.push_back(f);
    }
    cand.built = true;
    std::vector<std::size_t> probe = {0, 1, 2, 3};
    msf::TolRun r = msf::orderedSearchTol(ref, cand, probe, 300.0, 4.5, 3, 1);
    check(r.found, "tol 3-of-4 found", code);
    check(r.found && std::abs(r.offset - 30.0) < 2.0, "tol offset ~30", code);
    auto h2 = hashes(502, 8);
    msf::TemporalAnchorIndex ref2 = seqIdx(h2, 0.0, 1.0, 60.0);
    msf::TemporalAnchorIndex cand2;
    cand2.duration = 90.0;
    for (int i = 0; i < 30; ++i) cand2.seq.push_back(flatA((double)i));
    for (std::size_t i = 0; i < h2.size(); ++i) {
      auto f = mkA(30.0 + i, h2[i]);
      if (i == 1 || i == 2) f.phash = hashes(1000 + i, 1)[0], f.mirror = ~f.phash;  // 2 corrupted
      cand2.seq.push_back(f);
    }
    cand2.built = true;
    msf::TolRun r2 = msf::orderedSearchTol(ref2, cand2, probe, 300.0, 4.5, 3, 1);
    check(!r2.found, "tol 2-miss rejected", code);
  }
  // V3-3. Offset refinement converges to the true offset.
  {
    auto h = hashes(503, 400);
    msf::TemporalAnchorIndex a = seqIdx(h, 0.0, 1.0, 400.0);
    msf::TemporalAnchorIndex b;
    b.duration = 445.0;
    for (int i = 0; i < 45; ++i) b.seq.push_back(flatA((double)i));
    for (std::size_t i = 0; i < h.size(); ++i) b.seq.push_back(mkA(45.0 + i, h[i]));
    b.built = true;
    double r = msf::refineOffsetBySupport(a, b, 47.0, -10.0, 10.0);
    check(std::abs(r - 45.0) < 0.6, "refine to true offset", code);
  }
  // V3-4. Ending-first DUP on synthetic same content + 45 s leader.
  {
    auto h = hashes(504, 700);
    msf::TemporalAnchorIndex ref = seqIdx(h, 0.0, 1.0, 700.0);
    msf::TemporalAnchorIndex cand;
    cand.duration = 745.0;
    for (int i = 0; i < 45; ++i) cand.seq.push_back(flatA((double)i));
    for (std::size_t i = 0; i < h.size(); ++i) cand.seq.push_back(mkA(45.0 + i, h[i]));
    cand.built = true;
    msf::TemporalAnchorV3Result r;
    double s = msf::temporalAnchorSimilarityV3(ref, cand, r);
    check(r.verdict == msf::AnchorVerdictKind::Dup, "V3 leader DUP", code);
    check(std::abs(r.endOffset - 45.0) < 3.0, "V3 offset ~45", code);
    check(s >= 87.5, "V3 leader score", code);
    check(r.bandPass, "V3 band pass", code);
  }
  // V3-5. Dark pair: UNCERTAIN, never DIFF.
  {
    auto ha = hashes(505, 120), hb = hashes(506, 120);
    msf::TemporalAnchorIndex a, b;
    a.duration = b.duration = 120.0;
    for (int i = 0; i < 120; ++i) {
      a.seq.push_back(mkA((double)i, ha[(std::size_t)i], true, 12.0));
      b.seq.push_back(mkA((double)i, hb[(std::size_t)i], true, 14.0));
    }
    a.built = b.built = true;
    msf::TemporalAnchorV3Result r;
    msf::temporalAnchorSimilarityV3(a, b, r);
    check(r.verdict == msf::AnchorVerdictKind::Uncertain, "dark pair Uncertain", code);
  }
  // V3-6. Different content: never DUP.
  {
    auto ha = hashes(507, 700), hb = hashes(508, 700);
    msf::TemporalAnchorIndex a = seqIdx(ha, 0.0, 1.0, 700.0);
    msf::TemporalAnchorIndex b = seqIdx(hb, 0.0, 1.0, 700.0);
    msf::TemporalAnchorV3Result r;
    msf::temporalAnchorSimilarityV3(a, b, r);
    check(r.verdict != msf::AnchorVerdictKind::Dup, "stranger not DUP", code);
  }
  if (fails == 0) std::cout << "video_temporal_anchor_v3=ok\n";
  return fails == 0 ? 0 : 1;
}
