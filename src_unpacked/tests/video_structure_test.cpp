// 0.9.4.167 V2: structure-probe unit pins (no fixtures, no FFmpeg).
// Hand-built 96x96 frames: black block / white block / black block must
// segment at the two cuts; a flat run must stay boundary-free.
#include "video_structure.h"
#include "video_decoder.h"

#include <cmath>
#include <iostream>
#include <vector>

namespace {
msf::VideoFrame solid(double t, unsigned char v) {
  msf::VideoFrame f;
  f.timestamp = t;
  f.width = 96;
  f.height = 96;
  f.gray.assign((std::size_t)96 * 96, v);
  return f;
}
}  // namespace

int main() {
  // BWB: 4 black, 4 white, 4 black at 1 fps.
  std::vector<msf::VideoFrame> clip;
  for (int i = 0; i < 4; ++i) clip.push_back(solid((double)i, 0));
  for (int i = 4; i < 8; ++i) clip.push_back(solid((double)i, 255));
  for (int i = 8; i < 12; ++i) clip.push_back(solid((double)i, 0));
  const msf::VideoStructure s = msf::probeVideoStructure(clip);
  if (!s.probed) { std::cerr << "not probed\n"; return 1; }
  if (s.boundaries.size() != 3) { std::cerr << "boundaries=" << s.boundaries.size() << "\n"; return 2; }
  if (s.boundaries[1] < 3 || s.boundaries[1] > 5) { std::cerr << "cut1 off\n"; return 3; }
  if (s.boundaries[2] < 7 || s.boundaries[2] > 9) { std::cerr << "cut2 off\n"; return 4; }
  if (s.segments.size() != 3) { std::cerr << "segments=" << s.segments.size() << "\n"; return 5; }
  if (!(s.confidence > 0.0)) { std::cerr << "no confidence\n"; return 6; }
  // Flat gray: no boundaries beyond the start.
  std::vector<msf::VideoFrame> flat;
  for (int i = 0; i < 12; ++i) flat.push_back(solid((double)i, 128));
  const msf::VideoStructure f = msf::probeVideoStructure(flat);
  if (f.boundaries.size() != 1 || f.boundaries[0] != 0) {
    std::cerr << "flat split, boundaries=" << f.boundaries.size() << "\n";
    return 7;
  }
  std::cout << "video_structure=ok bounds=" << s.boundaries[1] << "," << s.boundaries[2]
            << " conf=" << s.confidence << "\n";
  return 0;
}
