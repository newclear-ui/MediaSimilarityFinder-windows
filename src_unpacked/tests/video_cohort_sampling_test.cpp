// 0.9.4.171 V cohort sampling pins (synthetic only, no fixtures/FFmpeg).
// - Default path byte-identical to the legacy sweep (no fingerprint change).
// - Structured short (<=60 s, defaults) collapses to the base set.
// - Structured long (130 min) is sorted ascending, deduped, carries the
//   reverse-from-T endpoint probes + opening grid, and honors custom
//   longStruct density — decoded in one sequential sweep (no seeks).
// - Structure-windowed kept selection honors the variance floor and the
//   soft target (fewer than target is valid); low confidence reports
//   fallback (all-zero + usedWindows=false).
// - Cache identity: default stays "v1+seg"; structured adds "+struct";
//   legacy rollback stays "+var".
#include "video_sampling.h"
#include "video_structure.h"
#include "video_fingerprint.h"
#include "video_policy.h"
#include "video_decoder.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
bool approx(const std::vector<double>& v, double t) {
  for (double x : v)
    if (std::abs(x - t) < 1e-3) return true;
  return false;
}
bool sortedUnique(const std::vector<double>& v) {
  for (std::size_t i = 1; i < v.size(); ++i) {
    if (v[i] < v[i - 1] - 1e-9) return false;
    if (std::abs(v[i] - v[i - 1]) < 1e-3) return false;
  }
  return true;
}
msf::VideoFrame frame32(double t, unsigned char v, bool checker) {
  msf::VideoFrame f;
  f.timestamp = t;
  f.width = 32;
  f.height = 32;
  f.gray.assign(1024, v);
  if (checker) {
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 32; ++x) f.gray[(std::size_t)y * 32 + x] = ((x + y) % 2) ? 255 : 0;
  }
  return f;
}
double sdOf(const std::vector<std::uint8_t>& px) {
  double s = 0;
  for (unsigned char v : px) s += v;
  const double m = s / px.size();
  double sq = 0;
  for (unsigned char v : px) {
    double d = (double)v - m;
    sq += d * d;
  }
  return std::sqrt(sq / px.size());
}
void setEnv(const char* k, const char* v) {
#if defined(_WIN32)
  if (v)
    _putenv_s(k, v);
  else
    _putenv_s(k, "");
#else
  if (v)
    setenv(k, v, 1);
  else
    unsetenv(k);
#endif
}
}  // namespace

int main() {
  msf::VideoPolicy def;  // all recommended defaults
  // 1. Default path == legacy sweep (byte-identical).
  {
    auto leg = msf::make_sample_plan(60, 5.0);
    auto coh = msf::make_cohort_plan(60, def, false);
    if (coh.timestamps != leg.timestamps) { std::cerr << "short default plan differs\n"; return 1; }
    if (std::abs(coh.intervalSec - 5.0) > 1e-9) { std::cerr << "short interval\n"; return 2; }
    auto leg300 = msf::make_sample_plan(300, 0.0);
    auto coh300 = msf::make_cohort_plan(300, def, false);
    if (coh300.timestamps != leg300.timestamps) { std::cerr << "long default plan differs\n"; return 3; }
    if (std::abs(coh300.intervalSec - 8.0) > 1e-9) { std::cerr << "tier interval\n"; return 4; }
  }
  // 2. Structured short collapses to base with defaults.
  {
    auto a = msf::make_cohort_plan(60, def, false);
    auto b = msf::make_cohort_plan(60, def, true);
    if (a.timestamps != b.timestamps) { std::cerr << "structured short not collapsed\n"; return 5; }
  }
  // 3. Structured long (130 min) properties.
  {
    const double T = 7800.0;
    auto base = msf::make_cohort_plan(T, def, false);
    auto full = msf::make_cohort_plan(T, def, true);
    if (!sortedUnique(full.timestamps)) { std::cerr << "long not sorted/unique\n"; return 6; }
    if (std::abs(full.timestamps.back() - T) > 1e-3) { std::cerr << "long missing T\n"; return 7; }
    if (full.timestamps.size() <= base.timestamps.size()) { std::cerr << "long adds nothing\n"; return 8; }
    if (full.timestamps.size() > 10000) { std::cerr << "long cap\n"; return 9; }
    for (double off : {15.0, 30.0, 60.0, 120.0, 240.0, 480.0, 720.0, 1200.0}) {
      if (!approx(full.timestamps, T - off)) { std::cerr << "missing T-" << off << "\n"; return 10; }
    }
    for (double t : {0.0, 5.0, 10.0, 15.0, 20.0, 25.0, 30.0, 60.0, 90.0, 120.0, 180.0}) {
      if (!approx(full.timestamps, t)) { std::cerr << "missing opening " << t << "\n"; return 11; }
    }
    if (std::abs(full.intervalSec - 32.0) > 1e-9) { std::cerr << "long tier\n"; return 12; }
    std::cout << "  [info] base=" << base.timestamps.size() << " structured=" << full.timestamps.size() << "\n";
  }
  // 4. Custom longStruct density honored in windows only.
  {
    msf::VideoPolicy custom = def;
    custom.longStructIntervalSec = 3.0;
    const double T = 7800.0;
    auto d5 = msf::make_cohort_plan(T, def, true);
    auto d3 = msf::make_cohort_plan(T, custom, true);
    if (!approx(d3.timestamps, 3.0) || !approx(d3.timestamps, 6.0) || !approx(d3.timestamps, 27.0)) {
      std::cerr << "custom opening grid missing\n";
      return 13;
    }
    if (d3.timestamps.size() <= d5.timestamps.size()) { std::cerr << "custom not denser\n"; return 14; }
    // Base tier elsewhere unchanged: spot-check a mid-timeline tier tick.
    if (!approx(d3.timestamps, 3200.0)) { std::cerr << "mid tier tick missing\n"; return 15; }
  }
  // 5. Structured kept: windows + variance floor + soft target.
  {
    std::vector<msf::VideoFrame> fr;
    std::vector<double> sd;
    for (int i = 0; i < 12; ++i) {
      bool tex = (i % 2 == 0);  // even = textured, odd = flat
      fr.push_back(frame32((double)i, 128, tex));
      sd.push_back(sdOf(fr.back().gray));
    }
    msf::VideoStructure st;
    st.probed = true;
    st.confidence = 0.9;
    st.boundaries = {0, 4, 8};
    st.timestamps = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    st.segments.resize(3);
    bool used = false;
    auto keep = msf::selectKeptStructured(fr, sd, 3, st, &used);
    if (!used) { std::cerr << "structured unused on good input\n"; return 16; }
    std::size_t n = 0;
    for (std::size_t i = 0; i < keep.size(); ++i) {
      if (keep[i]) {
        ++n;
        if (sd[i] < 6.0) { std::cerr << "kept flat frame\n"; return 17; }
      }
    }
    if (n != 3) { std::cerr << "kept=" << n << "\n"; return 18; }
    // Soft target: all flat -> nothing forced.
    std::vector<msf::VideoFrame> flat;
    std::vector<double> flatSd;
    for (int i = 0; i < 8; ++i) {
      flat.push_back(frame32((double)i, 128, false));
      flatSd.push_back(sdOf(flat.back().gray));
    }
    msf::VideoStructure stf;
    stf.probed = true;
    stf.confidence = 0.9;
    stf.boundaries = {0};
    stf.segments.resize(1);
    bool usedF = true;
    auto keepF = msf::selectKeptStructured(flat, flatSd, 4, stf, &usedF);
    std::size_t nf = 0;
    for (char k : keepF) nf += (k != 0);
    if (usedF || nf != 0) { std::cerr << "flat forced fill\n"; return 19; }
    // Fallback: low confidence -> all-zero + false.
    msf::VideoStructure low = st;
    low.confidence = 0.1;
    bool usedL = true;
    auto keepL = msf::selectKeptStructured(fr, sd, 3, low, &usedL);
    std::size_t nl = 0;
    for (char k : keepL) nl += (k != 0);
    if (usedL || nl != 0) { std::cerr << "low-conf not fallback\n"; return 20; }
  }
  // 6. Cache identity.
  {
    setEnv("MSF_VIDEO_KEPT", nullptr);
    setEnv("MSF_VIDEO_STRUCTURE", nullptr);
    std::string id = msf::videoKeptPolicyId(def);
    if (id != "v1+seg") { std::cerr << "default identity=" << id << "\n"; return 21; }
    setEnv("MSF_VIDEO_STRUCTURE", "full");
    std::string sid = msf::videoKeptPolicyId(def);
    if (sid == id || sid.find("+struct:") == std::string::npos) {
      std::cerr << "structured identity=" << sid << "\n";
      return 22;
    }
    setEnv("MSF_VIDEO_STRUCTURE", nullptr);
    setEnv("MSF_VIDEO_KEPT", "legacy");
    std::string lid = msf::videoKeptPolicyId(def);
    if (lid.size() < 4 || lid.substr(lid.size() - 4) != "+var") {
      std::cerr << "legacy identity=" << lid << "\n";
      return 23;
    }
    setEnv("MSF_VIDEO_KEPT", nullptr);
    // Custom longs without env also enable structured identity.
    msf::VideoPolicy custom = def;
    custom.longStructIntervalSec = 3.0;
    std::string cid = msf::videoKeptPolicyId(custom);
    if (cid == id || cid.find("+struct:") == std::string::npos) {
      std::cerr << "custom-long identity=" << cid << "\n";
      return 24;
    }
  }
  std::cout << "video_cohort_sampling=ok\n";
  return 0;
}
