#pragma once
// V workstream video comparison policy (GUI Video tab + engine sampling).
// Decided 2026-10-31 (directive): Auto applies the recommended per-cohort
// policy; Custom exposes intervals/targets. Long-form local-window refinement,
// selective re-extraction (uncertain boost) and the partial-clip route have no
// engine implementation yet: their settings travel end-to-end (snapshot,
// telemetry) but the engine logs the pending scope instead of pretending.
#include <array>
#include <cstdint>
#include <string>
namespace msf {
// Duration cohorts G1..G8 (boundaries match the design doc).
inline int videoCohortForDuration(double seconds) {
  if (!(seconds > 0)) return 0;
  if (seconds <= 15.0) return 0;    // G1
  if (seconds <= 60.0) return 1;    // G2
  if (seconds <= 180.0) return 2;   // G3
  if (seconds <= 600.0) return 3;   // G4
  if (seconds <= 1500.0) return 4;  // G5
  if (seconds <= 3600.0) return 5;  // G6
  if (seconds <= 9000.0) return 6;  // G7
  return 7;                         // G8
}
enum class VideoMode { Auto = 0, Custom = 1 };
// 0.9.4.156: single source of truth for recommended defaults and UI ranges.
// Short/long-form probe intervals (seconds); kept-frame soft targets and
// their per-cohort UI ranges (directive table: G1 4-6, G2 6-10, G3 8-10,
// G4 10-14, G5 9-13, G6 9-12, G7 10-14, G8 12-16; centers 5/8/9/12/11/10/12/14).
constexpr double kVideoShortIntervalDefault = 5.0;
constexpr double kVideoLongStructIntervalDefault = 5.0;
constexpr double kVideoLongPreciseIntervalDefault = 5.0;
constexpr int kVideoKeptMin[8] = {4, 6, 8, 10, 9, 9, 10, 12};
constexpr int kVideoKeptMax[8] = {6, 10, 10, 14, 13, 12, 14, 16};
constexpr int kVideoKeptDefault[8] = {5, 8, 9, 12, 11, 10, 12, 14};
struct VideoPolicy {
  VideoMode mode = VideoMode::Auto;
  // Short-form (<=60 s) whole-timeline probe interval, seconds. Applied to the
  // sweep. UI range 1..60, recommended default 5.
  double shortIntervalSec = kVideoShortIntervalDefault;
  // Long-form (>25 min) intervals: stored and propagated, but the current
  // engine has no local-window refinement, so they are NOT applied to the
  // uniform sweep (pending V2/V3 structure detection). Telemetry records them.
  double longStructIntervalSec = kVideoLongStructIntervalDefault;   // boundary-window detail interval
  double longPreciseIntervalSec = kVideoLongPreciseIntervalDefault;  // uncertain-section precise interval
  // Kept-frame soft targets per cohort (not quotas; fewer when low-info).
  // Recommended centers: G1 5, G2 8, G3 9, G4 12, G5 11, G6 10, G7 12, G8 14.
  std::array<int, 8> keptTarget = {kVideoKeptDefault[0], kVideoKeptDefault[1], kVideoKeptDefault[2], kVideoKeptDefault[3], kVideoKeptDefault[4], kVideoKeptDefault[5], kVideoKeptDefault[6], kVideoKeptDefault[7]};
  // Uncertain-candidate boost (default ON): stored + propagated, but selective
  // re-extraction is not implemented (V4 scope) so the engine treats it as an
  // explicit no-op and logs it once per scan.
  bool uncertainBoost = true;
  int boostFrames = 1;  // 1..2 per uncertain interval (pending with the boost)
  // Partial-clip search: separate opt-in route, not implemented (V6 scope).
  // Stored; the engine keeps using the whole-video path and logs it.
  bool partialClip = false;
  // Stable cache/generation identity. Covers ONLY the settings that change the
  // fingerprint (short-form interval + kept targets). Pending/no-op settings
  // (long-form intervals, uncertain boost, partial clip) are deliberately
  // excluded: toggling them must not invalidate caches. If a future engine
  // starts consuming them, they must join this identity. "v1" = all
  // recommended defaults; anything else => "custom:<hash>".
  std::string policyId() const {
    bool defKept = true;
    for (int i = 0; i < 8; ++i)
      if (keptTarget[(std::size_t)i] != kVideoKeptDefault[i]) defKept = false;
    if (shortIntervalSec == kVideoShortIntervalDefault && defKept)
      return "v1";
    std::string s;
    s += "|" + std::to_string(shortIntervalSec) + "|";
    for (int v : keptTarget) {
      s += std::to_string(v);
      s += ",";
    }
    // NOTE: long-form intervals, uncertain boost and partial clip are pending
    // no-ops for fingerprinting (see above) and stay out of the hash.
    // FNV-1a 64-bit, matching quickIdentity's spirit (identity, not security).
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) {
      h ^= c;
      h *= 1099511628211ULL;
    }
    char buf[32];
    const char* hex = "0123456789abcdef";
    buf[0] = 'c';
    buf[1] = 'u';
    buf[2] = 's';
    buf[3] = 't';
    buf[4] = 'o';
    buf[5] = 'm';
    buf[6] = ':';
    for (int i = 0; i < 8; ++i)
      buf[7 + i] = hex[(h >> (60 - i * 4)) & 0xF];
    buf[15] = '\0';
    return std::string(buf);
  }
};
}  // namespace msf
