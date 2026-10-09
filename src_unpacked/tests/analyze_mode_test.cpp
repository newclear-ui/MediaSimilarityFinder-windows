// Analyze-mode filter parity (0.9.4.81).
//
// ScanPipeline::analyze gained two optional parameters that restrict which
// candidate pairs the final pass re-verifies:
//   changedFiles            - per-file "changed this scan" flags (null = all).
//   skipBothChangedImages   - also skip image pairs where BOTH sides changed
//                             (the live streaming pass already emitted them).
//
// These must NOT change any verdict: they only choose which pairs are
// re-verified. This test pins the three observable properties that make the
// Live (A) and Hybrid (A+B) modes safe:
//   1. A full dirty set (cold scan: every file changed) verifies exactly the
//      same pair set as the unfiltered Sequential (B) pass.
//   2. A partial dirty set verifies every pair touching a changed file and
//      skips pairs where both sides are unchanged.
//   3. skipBothChangedImages drops image pairs where both sides changed, on
//      the caller's guarantee that the live pass already produced them.
#include "scan_pipeline.h"
#include <functional>
#include <iostream>
#include <utility>
#include <vector>

namespace {
int checks = 0, failures = 0;
void expect(bool ok, const char* what) {
  ++checks;
  if (!ok) { ++failures; std::cerr << "FAIL: " << what << "\n"; }
}
bool hasPair(const std::vector<msf::MediaMatch>& m, std::size_t a, std::size_t b) {
  if (a > b) std::swap(a, b);
  for (const auto& x : m) {
    std::size_t l = x.left, r = x.right;
    if (l > r) std::swap(l, r);
    if (l == a && r == b) return true;
  }
  return false;
}
}  // namespace

int main() {
  // Five images: X,X,Y,Y,X. X forms three pairs (0-1, 0-4, 1-4); Y forms one
  // (2-3). X and Y are far apart, so no cross pairs exist.
  const std::uint64_t X = 0xFFFF0000FFFF0000ULL;
  const std::uint64_t Y = 0x0F0F0F0F0F0F0F0FULL;
  msf::ScanPipeline p;
  p.add({"a.jpg", msf::MediaKind::Image, 10, 1, X});
  p.add({"b.jpg", msf::MediaKind::Image, 10, 1, X});
  p.add({"c.jpg", msf::MediaKind::Image, 10, 1, Y});
  p.add({"d.jpg", msf::MediaKind::Image, 10, 1, Y});
  p.add({"e.jpg", msf::MediaKind::Image, 10, 1, X});

  // B: unfiltered sequential pass. Reference set.
  const auto full = p.analyze(8);
  expect(full.matches.size() == 4, "B: full pass finds the 4 duplicate pairs");
  expect(hasPair(full.matches, 0, 1) && hasPair(full.matches, 0, 4) &&
             hasPair(full.matches, 1, 4) && hasPair(full.matches, 2, 3),
         "B: the 4 pairs are exactly the X and Y duplicate pairs");

  // (1) A with every file changed == B (cold-scan parity).
  std::vector<char> all(5, 1);
  const auto aAll = p.analyze(8, {}, {}, &all, false);
  expect(aAll.matches.size() == full.matches.size(),
         "A cold (all changed): same pair count as B");
  bool sameSet = true;
  for (const auto& m : full.matches)
    if (!hasPair(aAll.matches, m.left, m.right)) sameSet = false;
  expect(sameSet, "A cold (all changed): same pair set as B");

  // (2) A with only files 0,1 changed: every pair touching a changed file is
  // verified; the both-unchanged pair (2,3) is skipped.
  std::vector<char> dirty(5, 0);
  dirty[0] = dirty[1] = 1;
  const auto aSub = p.analyze(8, {}, {}, &dirty, false);
  expect(aSub.matches.size() == 3, "A partial: verifies 3 pairs touching a changed file");
  expect(hasPair(aSub.matches, 0, 1) && hasPair(aSub.matches, 0, 4) &&
             hasPair(aSub.matches, 1, 4),
         "A partial: keeps changed-vs-changed and changed-vs-unchanged pairs");
  expect(!hasPair(aSub.matches, 2, 3),
         "A partial: skips the both-unchanged pair");

  // (3) Hybrid (skipBothChangedImages) with files 0,1 changed: the both-changed
  // pair (0,1) is dropped (live already emitted it), leaving the two
  // changed-vs-unchanged pairs.
  const auto abSub = p.analyze(8, {}, {}, &dirty, true);
  expect(abSub.matches.size() == 2, "AB partial: drops both-changed, keeps changed-vs-unchanged");
  expect(!hasPair(abSub.matches, 0, 1), "AB partial: both-changed image pair (0,1) skipped");
  expect(hasPair(abSub.matches, 0, 4) && hasPair(abSub.matches, 1, 4),
         "AB partial: changed-vs-unchanged pairs kept");

  // Hybrid on a cold scan (all changed) drops every image pair in the final
  // pass -- correct only because the caller also runs the live streaming pass
  // that emitted them. Pinned here so the dependency is explicit.
  const auto abAll = p.analyze(8, {}, {}, &all, true);
  expect(abAll.matches.empty(),
         "AB cold: final pass emits no image pairs (live pass owns them)");

  // No filter still equals the reference (guards the default/B regression).
  const auto none = p.analyze(8, {}, {}, nullptr, false);
  expect(none.matches.size() == full.matches.size(),
         "no filter (B/default): identical to the unfiltered pass");

  // (4) B-slice windowing: sliceGroups=2 must call onSlice once per window and
  // produce the same pair set as the unsliced pass.
  {
    std::vector<std::pair<std::size_t, std::size_t>> calls;
    std::function<void(int, std::size_t, std::size_t)> slice =
        [&](int phase, std::size_t d, std::size_t t) { if (phase == 0) calls.push_back({d, t}); };
    const auto s = p.analyze(8, {}, {}, nullptr, false, 2, &slice, 0);
    expect(s.matches.size() == full.matches.size(), "B-slice: same pair count as unsliced");
    bool sameSet = true;
    for (const auto& m : full.matches) if (!hasPair(s.matches, m.left, m.right)) sameSet = false;
    expect(sameSet, "B-slice: same pair set as unsliced");
    expect(calls.size() == 3, "B-slice: 5 groups / window 2 => 3 onSlice calls");
    if (calls.size() == 3) {
      expect(calls[0].first == 2 && calls[1].first == 4 && calls[2].first == 5,
             "B-slice: done advances 2,4,5");
      expect(calls[2].second == 5, "B-slice: total reported as the group count");
    }
  }
  // (5) resume offset: skipping the first two image groups drops every pair
  // whose left group is below the start.
  {
    const auto s = p.analyze(8, {}, {}, nullptr, false, 0, nullptr, 2);
    expect(s.matches.size() == 1 && hasPair(s.matches, 2, 3),
           "B-slice resume: only pairs with left group >= start remain");
  }

  if (failures) { std::cout << "analyze_mode=failed " << failures << "/" << checks << "\n"; return 1; }
  std::cout << "analyze_mode=ok checks=" << checks << "\n";
  return 0;
}
