#include "image_verify.h"
#include "image_decoder.h"
#include "video_fingerprint.h" // frame_ssim
#include <algorithm>
namespace msf {
namespace {
// Center region of a WxH gray buffer (mirrors cropFingerprints geometry:
// center 4:3 / 1:1 / 9:16 windows of the decoded frame).
struct Region { int x0, y0, w, h; };
static Region centerRegion(int W, int H, double aspect) {
  double srcA = (double)W / H; int cw = W, ch = H;
  if (srcA > aspect) cw = std::max(1, (int)std::lround(H * aspect));
  else if (srcA < aspect) ch = std::max(1, (int)std::lround(W / aspect));
  return {(W - cw) / 2, (H - ch) / 2, cw, ch};
}
static void flipH(const std::vector<std::uint8_t>& src, std::vector<std::uint8_t>& dst, int W, int H) {
  dst.resize((std::size_t)W * H);
  for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x)
    dst[(std::size_t)y * W + x] = src[(std::size_t)y * W + (W - 1 - x)];
}
static double regionSsim(const std::vector<std::uint8_t>& A, const std::vector<std::uint8_t>& B,
                         int W, int H, const Region& r) {
  // Copy the region into contiguous rows (frame_ssim needs row stride == w).
  std::vector<std::uint8_t> a((std::size_t)r.w * r.h), b((std::size_t)r.w * r.h);
  for (int y = 0; y < r.h; ++y)
    for (int x = 0; x < r.w; ++x) {
      a[(std::size_t)y * r.w + x] = A[(std::size_t)(r.y0 + y) * W + (r.x0 + x)];
      b[(std::size_t)y * r.w + x] = B[(std::size_t)(r.y0 + y) * W + (r.x0 + x)];
    }
  return frame_ssim(a.data(), b.data(), r.w, r.h);
}
} // namespace
double verifyImagePair(const std::string& pathA, const std::string& pathB,
                       bool isImage, double hammingSim, double threshold) {
  if (!isImage) return hammingSim;
  constexpr double kFast = 97.0;
  if (hammingSim >= kFast) return hammingSim; // near-identical: no decode
  if (pathA.empty() || pathB.empty()) return hammingSim;
  constexpr int kDim = 64;
  ImageDecoder dec; GrayImage A, B;
  if (!dec.decode(pathA, kDim, kDim, A) || !dec.decode(pathB, kDim, kDim, B)) return hammingSim;
  if (A.width != kDim || A.height != kDim || B.width != kDim || B.height != kDim) return hammingSim;
  if (A.pixels.size() != (std::size_t)kDim * kDim || B.pixels.size() != (std::size_t)kDim * kDim) return hammingSim;
  std::vector<std::uint8_t> fB; flipH(B.pixels, fB, kDim, kDim);
  double s = std::max(frame_ssim(A.pixels.data(), B.pixels.data(), kDim, kDim),
                      frame_ssim(A.pixels.data(), fB.data(), kDim, kDim));
  const double aspects[3] = {4.0 / 3.0, 1.0, 9.0 / 16.0};
  for (double asp : aspects) {
    const Region r = centerRegion(kDim, kDim, asp);
    s = std::max(s, regionSsim(A.pixels, B.pixels, kDim, kDim, r));
    s = std::max(s, regionSsim(A.pixels, fB, kDim, kDim, r));
  }
  return 0.5 * hammingSim + 0.5 * (100.0 * s);
}
}
