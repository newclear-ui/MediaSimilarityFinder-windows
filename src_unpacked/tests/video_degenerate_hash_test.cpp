// Regression: degenerate pHashes (almost no bits set, e.g. flat/letterbox
// crop fingerprints) carry no identity and must never produce a match.
// Real-world case: unrelated videos whose sparse crop hashes (popcount 2-4)
// scored 87-96% similarity and were grouped as identical (L1 short-circuit,
// temporal score 0 never consulted).
#include "scan_pipeline.h"
#include "similarity.h"
#include <iostream>

static msf::MediaFile video(const char* path, std::uint64_t fp,
                            std::uint64_t crop4x3) {
  msf::MediaFile f;
  f.path = path;
  f.kind = msf::MediaKind::Video;
  f.size = 10;
  f.modified = 1;
  f.fingerprint = fp;
  f.mirrorFingerprint = fp ^ 0x0F0F0F0F0F0F0F0FULL;
  f.crop4x3 = crop4x3;
  f.crop1x1 = 0;
  f.crop9x16 = 0;
  f.mirrorCrop4x3 = 0;
  f.mirrorCrop1x1 = 0;
  f.mirrorCrop9x16 = 0;
  f.duration = 10.0;
  return f;
}

int main() {
  // hash_usable contract.
  if (msf::hash_usable(0)) return 1;
  if (msf::hash_usable(0x500000000000ULL)) return 2;  // popcount 2
  if (msf::hash_usable(0x80401020000000ULL)) return 3;  // popcount 4
  if (!msf::hash_usable(0xFFFF)) return 4;
  if (!msf::hash_usable(0x0101010101010101ULL)) return 5;
  if (!msf::hash_usable(~0ULL)) return 6;

  // Two unrelated videos sharing an identical DEGENERATE crop hash must not
  // match. Full fingerprints are 20 bits apart (outside D<=8 candidacy), so
  // the pair is only enumerated via the crop index; the verdict must reject.
  {
    msf::ScanPipeline p;
    p.add(video("dg_a.mp4", ~0ULL, 0x500000000000ULL));
    p.add(video("dg_b.mp4", ~0ULL ^ 0xFFFFFULL, 0x500000000000ULL));
    auto s = p.analyze(8);
    if (s.groups != 0) {
      std::cerr << "degenerate crop pair matched groups=" << s.groups << "\n";
      return 7;
    }
  }
  // Positive control: identical informative hashes still match.
  {
    msf::ScanPipeline p;
    p.add(video("ok_a.mp4", 0x0101010101010101ULL, 0xAAAAAAAAAAAAAAAAULL));
    p.add(video("ok_b.mp4", 0x0101010101010101ULL, 0xAAAAAAAAAAAAAAAAULL));
    auto s = p.analyze(8);
    if (s.groups != 1) {
      std::cerr << "identical pair missed groups=" << s.groups << "\n";
      return 8;
    }
  }
  std::cout << "video_degenerate_hash=ok\n";
  return 0;
}
