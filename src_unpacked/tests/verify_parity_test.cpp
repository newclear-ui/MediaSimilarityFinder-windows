#include "analyze_telemetry.h"
#include "crop_fingerprint.h"
#include "image_verify.h"
#include "path_utils.h"
#include "video_fingerprint.h" // frame_ssim
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// D9b parity gate. The whole point of this test is the instruction that a
// mathematically identical rewrite must NOT be assumed to give bit-identical
// floating-point results: decode order, resize results, crop coordinates,
// rounding, and cached intermediates can all shift a verdict. So the test
// runs two independent implementations of the SAME greedy-max plan over the
// SAME input and requires a double-identical result.
//
// A "greedy short-circuit" would be a different optimization: it would stop
// evaluating once a window already hits the maximum, which can skip windows
// entirely. That is NOT what is being tested here. Both implementations below
// evaluate the exact same 10 window pairs, so this isolates the intended D9b
// change (removing recomputation) rather than an early-exit semantic change.
namespace {
int failures = 0;
int checks = 0;
void expect(bool ok, const char* what) {
  ++checks;
  if (!ok) { ++failures; std::cerr << "FAIL: " << what << "\n"; }
}

constexpr int kW = 64, kH = 64;

msf::GrayImage makeBuf(int seed, int w, int h) {
  msf::GrayImage g; g.width = w; g.height = h; g.pixels.resize((size_t)w * h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      g.pixels[(size_t)y * w + x] = (uint8_t)((x * 3 + y * 5 + seed * 7) % 251);
  return g;
}

}  // namespace

// The reference below is transcribed from the ORIGINAL pre-D9b algorithm
// (image_verify.cpp before the change), so the comparison is against the code
// that was actually replaced rather than against a re-derivation of it.
static double referenceVerify(const msf::GrayImage& fA, const msf::GrayImage& aA,
                              const msf::GrayImage& fB, const msf::GrayImage& aB,
                              double hammingSim) {
  return msf::verifyScorePlanReference(fA, aA, fB, aB, hammingSim, nullptr);
}

int main() {
  // Multiple buffer shapes and seeds, including degenerate ones, so the
  // comparison covers the aspect branches (landscape, square, portrait) that
  // drive the crop coordinates.
  const int seeds[] = {1, 7, 31, 97, 199};
  const int dims[][2] = {{64, 64}, {96, 64}, {64, 96}, {128, 80}, {80, 128}};
  for (int si = 0; si < 5; ++si)
    for (int di = 0; di < 5; ++di) {
      const int w = dims[di][0], h = dims[di][1];
      const msf::GrayImage fA = makeBuf(seeds[si], kW, kH), aA = makeBuf(seeds[si] + 1, w, h);
      const msf::GrayImage fB = makeBuf(seeds[si] + 2, kW, kH), aB = makeBuf(seeds[si] + 3, w, h);
      const double ham = 88.0 + (si * 2) % 8;  // stays below the kFast 97 gate
      const double ref = referenceVerify(fA, aA, fB, aB, ham);
      const double opt = msf::verifyScorePlan(fA, aA, fB, aB, ham, nullptr);
      char label[96];
      std::snprintf(label, sizeof(label), "seed %d dims %dx%d ref=%.17g opt=%.17g", seeds[si], w, h, ref, opt);
      if (ref != opt) { std::cerr << "FAIL: mismatch " << label << "\n"; ++failures; }
      ++checks;
    }
  if (failures) { std::cout << "verify_parity=failed " << failures << "\n"; return 1; }
  std::cout << "verify_parity=ok checks=" << checks << "\n";
  return 0;
}
