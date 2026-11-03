// See video_structure.h.
#include "video_structure.h"

#include "video_decoder.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <vector>

namespace msf {
namespace {
double frameMean(const std::vector<std::uint8_t>& px) {
  if (px.empty()) return 0.0;
  double s = 0;
  for (unsigned char v : px) s += v;
  return s / (double)px.size();
}
double frameDarkRatio(const std::vector<std::uint8_t>& px) {
  if (px.empty()) return 0.0;
  std::size_t n = 0;
  for (unsigned char v : px) if (v < 16) ++n;
  return (double)n / (double)px.size();
}
// Mean absolute horizontal difference on a stride-4 subsample (cheap
// texture/edge proxy on 96x96 frames).
double frameEdge(const std::vector<std::uint8_t>& px, int w, int h) {
  if (px.size() < (std::size_t)w * h || w < 8) return 0.0;
  double s = 0;
  std::size_t n = 0;
  for (int y = 0; y < h; y += 4)
    for (int x = 0; x + 4 < w; x += 4) {
      const std::size_t i = (std::size_t)y * w + x;
      s += std::abs((int)px[i + 4] - (int)px[i]);
      ++n;
    }
  return n ? s / n : 0.0;
}
double frameDelta(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
  if (a.size() != b.size() || a.empty()) return 0.0;
  double s = 0;
  std::size_t n = 0;
  for (std::size_t i = 0; i < a.size(); i += 16) {
    s += std::abs((int)a[i] - (int)b[i]);
    ++n;
  }
  return n ? s / n : 0.0;
}
double medianOf(std::vector<double> v) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  const std::size_t n = v.size();
  return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}
}  // namespace

VideoStructure probeVideoStructure(const std::vector<VideoFrame>& frames, double durationSec) {
  (void)durationSec;  // Reserved for duration-normalized features (V-gate follow-up).
  VideoStructure out;
  if (frames.empty()) return out;
  const std::size_t n = frames.size();
  out.timestamps.reserve(n);
  out.brightness.reserve(n);
  out.darkRatio.reserve(n);
  out.edgeDensity.reserve(n);
  out.frameDelta.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    const auto& f = frames[i];
    out.timestamps.push_back(f.timestamp);
    out.brightness.push_back(frameMean(f.gray));
    out.darkRatio.push_back(frameDarkRatio(f.gray));
    out.edgeDensity.push_back(frameEdge(f.gray, f.width, f.height));
    out.frameDelta.push_back(i ? frameDelta(f.gray, frames[i - 1].gray) : 0.0);
  }
  // Boundary candidates: change above median + 3*MAD, min 1 s apart.
  // Floor keeps flat-but-noisy inputs boundary-free.
  std::vector<double> deltas(out.frameDelta.begin() + 1, out.frameDelta.end());
  const double med = medianOf(deltas);
  std::vector<double> dev;
  dev.reserve(deltas.size());
  for (double d : deltas) dev.push_back(std::abs(d - med));
  const double mad = medianOf(dev);
  const double thresh = std::max(8.0, med + 3.0 * mad);
  out.boundaries.push_back(0);
  for (std::size_t i = 1; i < n; ++i) {
    if (out.frameDelta[i] >= thresh &&
        out.timestamps[i] - out.timestamps[out.boundaries.back()] >= 1.0)
      out.boundaries.push_back(i);
  }
  // Segments between boundaries (last runs to the final sample).
  for (std::size_t s = 0; s < out.boundaries.size(); ++s) {
    const std::size_t b = out.boundaries[s];
    const std::size_t e = (s + 1 < out.boundaries.size()) ? out.boundaries[s + 1] : n;
    VideoSegment seg;
    seg.startSec = out.timestamps[b];
    seg.endSec = (e < n) ? out.timestamps[e] : out.timestamps.back();
    double mb = 0, md = 0, motion = 0;
    std::size_t cnt = 0;
    for (std::size_t i = b; i < e; ++i) {
      mb += out.brightness[i];
      md += out.darkRatio[i];
      if (i > b) motion += out.frameDelta[i];
      ++cnt;
    }
    if (cnt) {
      seg.meanBrightness = mb / cnt;
      seg.darkRatio = md / cnt;
      seg.lowMotion = (cnt < 2) || (motion / (cnt - 1) < 4.0);
    }
    out.segments.push_back(seg);
  }
  // Heuristic confidence: changes are concentrated at boundaries rather
  // than spread through every interval.
  double inSeg = 0, total = 0;
  for (std::size_t i = 1; i < n; ++i) {
    total += out.frameDelta[i];
    bool near = false;
    for (std::size_t b : out.boundaries)
      if (std::abs(out.timestamps[i] - out.timestamps[b]) <= 0.5) { near = true; break; }
    if (near) inSeg += out.frameDelta[i];
  }
  out.confidence = (total > 0) ? std::clamp(inSeg / total, 0.0, 1.0) : 1.0;
  out.probed = true;
  return out;
}

bool videoStructureProbeEnabled() {
  static const bool any = [] {
    const char* v = std::getenv("MSF_VIDEO_STRUCTURE");
    return v && (std::string(v) == "1" || std::string(v) == "full");
  }();
  return any;
}
bool videoStructuredFullEnabled() {
  static const bool on = [] {
    const char* v = std::getenv("MSF_VIDEO_STRUCTURE");
    return v && std::string(v) == "full";
  }();
  return on;
}
// 0.9.4.171: structure-windowed kept selection. One best-variance frame
// per structure segment first (time-axis coverage follows detected
// boundaries, not uniform quarters), then global-rank fill that never
// forces frames below the variance floor. Fewer than keptTarget is a
// valid outcome (soft target); the caller falls back to uniform
// segmented only when the structure itself is unusable.
std::vector<char> selectKeptStructured(const std::vector<VideoFrame>& frames32,
                                       const std::vector<double>& frameSd,
                                       std::size_t keptTarget,
                                       const VideoStructure& structure,
                                       bool* usedWindows) {
  std::vector<char> keep(frames32.size(), 0);
  if (usedWindows) *usedWindows = false;
  if (frames32.empty() || keptTarget == 0) return keep;
  if (!structure.probed || structure.confidence < kVideoStructureMinConfidence) return keep;
  if (structure.boundaries.empty() || structure.segments.empty()) return keep;
  constexpr double kMinVariance = 6.0;
  // Map each sample to its structure window (segment index).
  const std::size_t n = frames32.size();
  const std::vector<std::size_t>& b = structure.boundaries;
  auto winOf = [&](std::size_t i) -> std::size_t {
    std::size_t w = 0;
    for (std::size_t k = 0; k < b.size(); ++k) {
      if (b[k] <= i) w = k;
    }
    if (w >= structure.segments.size()) w = structure.segments.size() - 1;
    return w;
  };
  const std::size_t nWin = structure.segments.size();
  // One best-variance frame per window (windows compete, not samples).
  std::size_t kept = 0;
  const std::size_t winBudget = std::min(nWin, keptTarget);
  for (std::size_t w = 0; w < winBudget; ++w) {
    std::size_t best = n;
    double bestSd = -1.0;
    for (std::size_t i = 0; i < n; ++i) {
      if (keep[i] || winOf(i) != w) continue;
      if (frames32[i].gray.size() != 1024) continue;
      const double sd = (i < frameSd.size()) ? frameSd[i] : 0.0;
      if (sd < kMinVariance) continue;  // floor applies even to windows
      if (sd > bestSd) {
        bestSd = sd;
        best = i;
      }
    }
    if (best < n) {
      keep[best] = 1;
      ++kept;
    }
  }
  // Fill the remainder by global variance rank, still honoring the floor.
  if (kept < keptTarget) {
    std::vector<std::size_t> rest;
    for (std::size_t i = 0; i < n; ++i) {
      if (!keep[i] && frames32[i].gray.size() == 1024) {
        const double sd = (i < frameSd.size()) ? frameSd[i] : 0.0;
        if (sd >= kMinVariance) rest.push_back(i);
      }
    }
    std::sort(rest.begin(), rest.end(), [&](std::size_t a, std::size_t b) {
      const double sa = (a < frameSd.size()) ? frameSd[a] : 0.0;
      const double sb = (b < frameSd.size()) ? frameSd[b] : 0.0;
      if (sa != sb) return sa > sb;
      return a < b;
    });
    for (std::size_t i : rest) {
      if (kept >= keptTarget) break;
      keep[i] = 1;
      ++kept;
    }
  }
  if (usedWindows) *usedWindows = (kept > 0);
  return keep;
}
}  // namespace msf
