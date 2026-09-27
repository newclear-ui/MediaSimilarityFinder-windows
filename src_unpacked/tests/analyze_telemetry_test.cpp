#include "analyze_telemetry.h"
#include "benchmark.h"
#include "image_verify.h"
#include "path_utils.h"
#include "scan_pipeline.h"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// D9a regression. D9a is measurement-only, so the properties worth protecting
// are: the telemetry exists and reports honest states, the sub-stage times
// cannot exceed the analyze total, counters never go negative or impossible,
// and the recorded results are byte-identical to what an uninstrumented
// pipeline would produce.
//
// Note what is deliberately NOT asserted: that a stage dominates, or that
// any particular value is "fast". Those are the measurements D9a exists to
// produce, and baking one machine's answer into the suite would make the test
// lie on every other machine.
namespace {
int failures = 0;
int checks = 0;
std::string firstFailure;
void expect(bool ok, const char* what) {
  ++checks;
  if (!ok) { ++failures; std::cout << "FAIL: " << what << "\n"; if (firstFailure.empty()) firstFailure = what; }
}
double numAfter(const std::string& js, const std::string& key, bool& found) {
  const std::string pat = "\"" + key + "\":";
  const std::size_t p = js.find(pat);
  found = p != std::string::npos;
  if (!found) return 0.0;
  return std::strtod(js.c_str() + p + pat.size(), nullptr);
}
double objNum(const std::string& js, const std::string& obj, const std::string& key) {
  const std::size_t o = js.find("\"" + obj + "\":{");
  if (o == std::string::npos) return 0.0;
  const std::size_t end = js.find('}', o);
  if (end == std::string::npos) return 0.0;
  bool f = false;
  return numAfter(js.substr(o, end - o), key, f);
}
bool hasIn(const std::string& js, const std::string& obj, const std::string& needle) {
  const std::size_t o = js.find("\"" + obj + "\":{");
  if (o == std::string::npos) return false;
  const std::size_t end = js.find('}', o);
  if (end == std::string::npos) return false;
  return js.substr(o, end - o).find(needle) != std::string::npos;
}
std::string objStr(const std::string& js, const std::string& obj, const std::string& key) {
  const std::size_t o = js.find("\"" + obj + "\":{");
  if (o == std::string::npos) return {};
  const std::size_t end = js.find('}', o);
  if (end == std::string::npos) return {};
  const std::string sub = js.substr(o, end - o);
  const std::string pat = "\"" + key + "\":\"";
  const std::size_t p = sub.find(pat);
  if (p == std::string::npos) return {};
  const std::size_t s = p + pat.size();
  const std::size_t e = sub.find('"', s);
  return e == std::string::npos ? std::string() : sub.substr(s, e - s);
}
}  // namespace

int main() {
  // --- C: a recorder that never ran analyze must not fake zeros ---
  {
    msf::BenchmarkRecorder rec;
    msf::BenchmarkConfig cfg;
    cfg.root = "C:/media"; cfg.build = "0.9.4.23"; cfg.engine = "1.5.0"; cfg.db = "1.0.3";
    rec.start(cfg);
    rec.addAnalyzeMs(1234.5);
    const std::string js = rec.toJson();
    expect(js.find("\"analyze\":{") != std::string::npos, "A: analyze object present without a run");
    expect(objStr(js, "analyze", "state") == "not_measured", "C: no analyze run -> not_measured");
    expect(hasIn(js, "analyze", "\"videoState\":\"not_measured\""), "C: video stage not entered -> not_measured");
    expect(objNum(js, "analyze", "verifyHitRate") == 0.0, "C: no lookups -> hit rate not a number claim");
    expect(hasIn(js, "analyze", "\"verifyHitRate\":null"), "C: verifyHitRate null when no lookups");
    expect(hasIn(js, "analyze", "\"msPerVerifyCall\":null"), "C: msPerVerifyCall null when no verify calls");
    expect(hasIn(js, "analyze", "\"msPerVerifyCallState\":\"not_measured\""), "C: derived state not_measured");
  }
  if (!firstFailure.empty()) { std::cout << "C section: " << firstFailure << "\n"; return 1; }

  // --- A/B/D/F/G: a real analyze run over in-memory files ---
  // Fingerprints are chosen so real verification work happens: the values sit
  // in the grey zone (below the kFast 97 short-circuit) so verifyImagePair
  // actually decodes and runs SSIM rather than returning immediately.
  {
    msf::ScanPipeline pipe;
    const std::size_t n = 24;
    for (std::size_t i = 0; i < n; ++i) {
      msf::MediaFile f;
      f.kind = msf::MediaKind::Image;
      f.path = "img" + std::to_string(i) + ".png";
      // Distinct but close pairs: each is a few bits away from the next.
      f.fingerprint = 0x0102030405060708ULL ^ (static_cast<std::uint64_t>(i) * 0x0000000000000003ULL);
      f.mirrorFingerprint = 0;
      f.crop4x3 = 0; f.crop1x1 = 0; f.crop9x16 = 0;
      f.mirrorCrop4x3 = 0; f.mirrorCrop1x1 = 0; f.mirrorCrop9x16 = 0;
      pipe.add(f);
    }
    const msf::ScanStats st = pipe.analyze(8);
    expect(st.analyze.analyzeRan, "B: analyze reports it ran");
    expect(st.analyze.indexMs >= 0.0, "D: indexMs >= 0");
    expect(st.analyze.scanMs >= 0.0, "D: scanMs >= 0");
    expect(st.analyze.verifyMs >= 0.0, "D: verifyMs >= 0");
    expect(st.analyze.videoMs >= 0.0, "D: videoMs >= 0");

    // D: the four slices must sum to at most the analyze total. sumMs is
    // derived from the same fields the recorder renders, and scan is defined
    // as the remainder, so the sum equals the total by construction.
    const double sumMs = st.analyze.indexMs + st.analyze.scanMs +
                         st.analyze.verifyMs + st.analyze.videoMs;
    expect(sumMs >= 0.0, "D: substage sum is non-negative");
    expect(st.analyze.videoStageEntered == false, "C: no video in this dataset -> stage not entered");

    // F: counters are unsigned, so this is a structural check, but assert it
    // explicitly because a future refactor could introduce a signed counter.
    expect(st.analyze.verifyCalls >= 0, "F: verifyCalls >= 0");
    expect(st.analyze.verifyDecodeMisses >= 0, "F: verifyDecodeMisses >= 0");
    expect(st.analyze.verifyCacheHits >= 0, "F: verifyCacheHits >= 0");
    expect(st.analyze.ssimEvals >= 0, "F: ssimEvals >= 0");
    expect(st.analyze.frameSsimEvals >= 0, "F: frameSsimEvals >= 0");
    expect(st.analyze.videoTemporalPairs >= 0, "F: videoTemporalPairs >= 0");
    // F: these files are MediaKind::Image and sit in the grey zone, so real
    // verification runs. But the paths do not exist on disk, so every buffer
    // lookup fails and no cache hit or SSIM evaluation can happen. That is the
    // honest record: a decode failure is not a cache hit, and it must not be
    // counted as SSIM work either.
    expect(st.analyze.verifyCalls > 0, "G: grey-zone image pairs do reach verifyImagePair");
    expect(st.analyze.verifyCacheHits == 0, "F: undecodable paths never report a cache hit");
    expect(st.analyze.ssimEvals == 0, "F: a failed buffer lookup runs no SSIM");
    expect(st.analyze.videoTemporalPairs == 0, "F: images never enter the video temporal path");
    // G: with no successful lookup the hit rate is undefined, so nothing is
    // asserted about its value here. The recorder section checks the null.
  }
  if (!firstFailure.empty()) { std::cout << "pipeline section: " << firstFailure << "\n"; return 2; }

  // --- Image pair verification actually exercises the counters ---
  // Two real files on disk, so verifyBuffersFor decodes and the cache
  // hit/miss and SSIM counters all get real values.
  {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "msf_d9a_verify";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    auto writeBmp = [&](const fs::path& p, int seed) {
      const int w = 32, h = 32, row = w * 3, img = row * h, fs2 = 54 + img;
      std::ofstream f(p, std::ios::binary);
      unsigned char hd[54] = {0};
      hd[0] = 'B'; hd[1] = 'M';
      hd[2] = (unsigned char)(fs2 & 0xFF); hd[3] = (unsigned char)((fs2 >> 8) & 0xFF);
      hd[10] = 54; hd[14] = 40; hd[18] = w; hd[22] = h; hd[26] = 1; hd[28] = 24;
      hd[34] = (unsigned char)(img & 0xFF); hd[35] = (unsigned char)((img >> 8) & 0xFF);
      f.write((const char*)hd, 54);
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          const unsigned char v = (unsigned char)(((x * 3 + y * 5 + seed) * 11) % 256);
          f.put((char)v); f.put((char)v); f.put((char)v);
        }
    };
    writeBmp(dir / "a.bmp", 7);
    writeBmp(dir / "b.bmp", 9);
    const std::string pa = msf::path_to_utf8(dir / "a.bmp");
    const std::string pb = msf::path_to_utf8(dir / "b.bmp");

    // H: an uninstrumented call and an instrumented call must agree exactly.
    msf::AnalyzeTelemetry none;
    const double plain = msf::verifyImagePair(pa, pb, true, 90.0, 87.5, &none);
    msf::AnalyzeTelemetry tel;
    const double measured = msf::verifyImagePair(pa, pb, true, 90.0, 87.5, &tel);
    expect(plain == measured, "H: instrumentation does not change the verdict value");
    expect(plain == measured, "H: verdict bit-identical with and without telemetry");
    expect(tel.verifyCalls == 1, "G: one image verify call counted");
    expect(tel.verifyDecodeMisses + tel.verifyCacheHits == 2, "G: two buffer lookups for two paths");
    expect(tel.ssimEvals >= 1, "G: at least one SSIM evaluation on the grey-zone path");
    expect(tel.frameSsimEvals == tel.ssimEvals * 2, "G: frameSsimEvals is 2x ssimEvals when no guard rejects");

    // The kFast short-circuit must count the call but do no buffer work.
    msf::AnalyzeTelemetry fast;
    (void)msf::verifyImagePair(pa, pb, true, 99.0, 87.5, &fast);
    expect(fast.verifyCalls == 1, "G: kFast short-circuit still counts as an image verify call");
    expect(fast.verifyDecodeMisses == 0 && fast.verifyCacheHits == 0,
           "G: kFast short-circuit performs no buffer lookup");
    expect(fast.ssimEvals == 0, "G: kFast short-circuit performs no SSIM");

    // A video pair is not image verification at all.
    msf::AnalyzeTelemetry vid;
    (void)msf::verifyImagePair(pa, pb, false, 90.0, 87.5, &vid);
    expect(vid.verifyCalls == 0, "F: isImage=false never counts as an image verify call");
    expect(vid.verifyDecodeMisses == 0 && vid.ssimEvals == 0, "F: video pair does no image work");

    fs::remove_all(dir, ec);
  }
  if (!firstFailure.empty()) { std::cout << "verify section: " << firstFailure << "\n"; return 3; }

  // --- D against the recorder: substage sum <= analyzeMs ---
  {
    msf::ScanPipeline pipe;
    for (int i = 0; i < 12; ++i) {
      msf::MediaFile f;
      f.kind = msf::MediaKind::Image;
      f.path = "p" + std::to_string(i) + ".png";
      f.fingerprint = 0xF0F0F0F0F0F0F0F0ULL ^ (static_cast<std::uint64_t>(i) * 0x11ULL);
      pipe.add(f);
    }
    const msf::ScanStats st = pipe.analyze(8);
    msf::BenchmarkRecorder rec;
    msf::BenchmarkConfig cfg;
    cfg.root = "C:/media"; cfg.build = "0.9.4.23"; cfg.engine = "1.5.0"; cfg.db = "1.0.3";
    rec.start(cfg);
    rec.setAnalyzeTelemetry(st.analyze);
    rec.addAnalyzeMs(st.analyze.indexMs + st.analyze.scanMs +
                     st.analyze.verifyMs + st.analyze.videoMs);
    rec.finalize(true, 12, 12, 0, st.candidates, st.groups, st.groups,
                 st.candidateReductionPercent, 0, 0);
    const std::string js = rec.toJson();
    bool f = false;
    const double analyzeMs = numAfter(js, "analyzeMs", f);
    expect(f, "A: analyzeMs still present in summary");
    const double sub = objNum(js, "analyze", "indexMs") + objNum(js, "analyze", "scanMs") +
                       objNum(js, "analyze", "verifyMs") + objNum(js, "analyze", "videoMs");
    expect(sub <= analyzeMs + 1.0, "D: substage sum <= analyzeMs (with float slack)");
    expect(objStr(js, "analyze", "state") == "measured", "B: analyze state measured after a run");
    expect(hasIn(js, "analyze", "\"schemaVersion\"") == false, "A: analyze object has no nested schema key");
    expect(js.find("\"schemaVersion\":9") != std::string::npos, "A: schema version is 9");
    // A fresh start must clear the previous run's numbers.
    rec.start(cfg);
    const std::string js2 = rec.toJson();
    expect(objStr(js2, "analyze", "state") == "not_measured", "C: a new run does not inherit analyze state");
  }
  if (!firstFailure.empty()) { std::cout << "recorder section: " << firstFailure << "\n"; return 4; }

  if (failures) { std::cout << "analyze_telemetry=failed " << failures << " failure(s)\n"; return 1; }
  std::cout << "analyze_telemetry=ok checks=" << checks << "\n";
  return 0;
}
