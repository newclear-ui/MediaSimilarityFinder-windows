// V2 structure probe (measurement-only, 0.9.4.167).
//
// Observes per-sample brightness, dark ratio, edge density and inter-frame
// change on already-decoded 96x96 gray frames (no extra decoding) and
// segments the timeline into intro / main / credits candidates.
// Output is a scratch record for future V-gate validation: it is never
// cached, never persisted, and never feeds judgement unless an explicit
// diagnostic switch enables the experimental consumers (V3/V4).
#pragma once
#include <cstddef>
#include <string>
#include <vector>
namespace msf {
struct VideoFrame;
// One segmented interval of the sampled timeline.
struct VideoSegment {
  double startSec = 0;     // inclusive video-time start
  double endSec = 0;       // exclusive video-time end
  double meanBrightness = 0;
  double darkRatio = 0;    // fraction of pixels below 16
  bool lowMotion = false;  // mean inter-frame change below the still threshold
};
// Scratch probe result. All vectors are 1:1 with the input sample order.
struct VideoStructure {
  std::vector<double> timestamps;   // video-time sample positions
  std::vector<double> brightness;   // per-sample mean (0-255)
  std::vector<double> darkRatio;    // per-sample dark fraction
  std::vector<double> edgeDensity;  // per-sample mean abs horizontal diff
  std::vector<double> frameDelta;   // mean abs diff vs previous sample (0 for first)
  std::vector<std::size_t> boundaries;  // sample indices starting a new segment (always has 0)
  std::vector<VideoSegment> segments;
  double confidence = 0;  // heuristic 0..1; NOT a calibrated probability
  bool probed = false;
};
// Heuristic v0 (measurement only): boundaries at inter-frame changes above
// median + 3*MAD with a 1 s minimum gap; intro = first segment when short,
// credits = trailing low-brightness low-motion run. No OCR, no color.
VideoStructure probeVideoStructure(const std::vector<VideoFrame>& frames96, double durationSec = 0);
// Diagnostic switch: MSF_VIDEO_STRUCTURE=1 runs the probe in build() and
// reports it through VideoBuildStats + telemetry. Default off.
bool videoStructureProbeEnabled();
}  // namespace msf
