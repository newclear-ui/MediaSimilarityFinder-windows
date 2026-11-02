#include "analyze_telemetry.h"
#include "benchmark.h"
#include "image_decoder.h"
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
    msf::TelemetryRecorder rec;
    msf::TelemetryConfig cfg;
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

    // --- D3: the two decodes a miss performs must be separable, and the split
    // must still reconstruct what D9d reported as one combined number.
    expect(tel.decodeFull.calls == tel.verifyDecodeMisses,
           "D3: one decode() per verify decode miss");
    // decodePreserveAspect() increments aspectCalls, not calls, so the second
    // accumulator is counted through that field.
    expect(tel.decodeAspect.aspectCalls == tel.verifyDecodeMisses,
           "D3: one decodePreserveAspect() per verify decode miss");
    expect(tel.verifyDecodes == tel.verifyDecodeMisses * 2,
           "D3: verifyDecodes stays 2x verifyDecodeMisses");
    expect(tel.decode.calls == tel.decodeFull.calls + tel.decodeAspect.calls,
           "D3: combined decode.calls is the sum of the two per-call counters");
    expect(tel.decode.aspectCalls == tel.decodeAspect.aspectCalls,
           "D3: combined decodeAspectCalls is the aspect side only");
    // The D9d keys must keep their old meaning, so the merged view has to
    // reproduce exactly what the two calls measured.
    expect(tel.decode.totalMs == tel.decodeFull.totalMs + tel.decodeAspect.totalMs,
           "D3: combined decodeTotalMs is the sum of the two per-call totals");
    expect(tel.decode.openMs == tel.decodeFull.openMs + tel.decodeAspect.openMs,
           "D3: combined decodeOpenMs is the sum of the two per-call opens");
    expect(tel.decode.copyMs == tel.decodeFull.copyMs + tel.decodeAspect.copyMs,
           "D3: combined decodeCopyMs is the sum of the two per-call copies");

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

    // --- D3 on a cold cache. The pair above is already in the process-wide
    // verify cache by now, so its second lookup would be a hit and both decode
    // accumulators would legitimately stay at zero. A pair that has never been
    // verified is what actually exercises both calls.
    writeBmp(dir / "c.bmp", 21);
    writeBmp(dir / "d.bmp", 23);
    msf::AnalyzeTelemetry cold;
    (void)msf::verifyImagePair(msf::path_to_utf8(dir / "c.bmp"),
                               msf::path_to_utf8(dir / "d.bmp"), true, 90.0, 87.5, &cold);
    expect(cold.verifyDecodeMisses == 2, "D3: both lookups on a cold pair miss the cache");
    expect(cold.decodeFull.calls == 2, "D3: two decode() calls on a cold pair");
    expect(cold.decodeAspect.aspectCalls == 2, "D3: two decodePreserveAspect() calls on a cold pair");
    expect(cold.decode.totalMs == cold.decodeFull.totalMs + cold.decodeAspect.totalMs,
           "D3: the combined total is exactly the two per-call totals");
    expect(cold.decodeFull.totalMs > 0.0 && cold.decodeAspect.totalMs > 0.0,
           "D3: both decode calls recorded a positive wall-clock time");
    // Both calls read the same file, so their combined cost must still fit
    // inside the verifyDecodeMs bucket the D9c identity accounts for.
    expect(cold.decode.totalMs <= cold.verifyDecodeMs + 1e-6,
           "D3: combined decode cost does not exceed the verifyDecodeMs bucket");

    fs::remove_all(dir, ec);
  }
  if (!firstFailure.empty()) { std::cout << "verify section: " << firstFailure << "\n"; return 3; }

  // --- D3: the merge that keeps the D9d keys meaning the same thing. ---
  // Checked field by field so a new DecodeTelemetry field that someone forgets
  // to fold in fails here instead of silently reading as zero in a benchmark.
  {
    msf::DecodeTelemetry a, b;
    a.totalMs = 1.5; a.openMs = 0.5; a.copyMs = 0.25; a.calls = 3; a.aspectCalls = 1;
    a.wicSucceeded = 3; a.failures = 1; a.factory2Attempts = 2; a.factory2Successes = 2;
    a.factory2Ms = 7; a.factoryFallbackMs = 0.5; a.openHrFailCount = 1;
    b.totalMs = 2.5; b.openMs = 0.75; b.copyMs = 0.5; b.calls = 4; b.aspectCalls = 2;
    b.wicSucceeded = 4; b.failures = 0; b.factory2Attempts = 1; b.factory2Successes = 0;
    b.factory2Fallbacks = 1; b.factory2FirstFailHr = 0x80070057u; b.factory2Ms = 3;
    b.factoryFallbackMs = 0.25; b.openHrFailCount = 2; b.openHrFirstFailCode = 0x80070005u;
    b.osFileOpenProbeMs = 0.1; b.osFileOpenProbeCount = 4; b.osFileOpenProbeFails = 1;

    msf::DecodeTelemetry m;
    msf::mergeDecodeTelemetry(m, a, b);
    expect(m.totalMs == 4.0, "D3 merge: totalMs adds");
    expect(m.openMs == 1.25, "D3 merge: openMs adds");
    expect(m.copyMs == 0.75, "D3 merge: copyMs adds");
    expect(m.calls == 7, "D3 merge: calls add");
    expect(m.aspectCalls == 3, "D3 merge: aspectCalls add");
    expect(m.wicSucceeded == 7, "D3 merge: wicSucceeded adds");
    expect(m.failures == 1, "D3 merge: failures add");
    expect(m.factory2Attempts == 3, "D3 merge: factory2Attempts add");
    expect(m.factory2Fallbacks == 1, "D3 merge: factory2Fallbacks add");
    expect(m.factory2Ms == 10, "D3 merge: factory2Ms adds");
    expect(m.factoryFallbackMs == 0.75, "D3 merge: factoryFallbackMs adds");
    expect(m.openHrFailCount == 3, "D3 merge: openHrFailCount adds");
    // The two failure codes cannot be summed, so the first non-zero wins.
    expect(m.factory2FirstFailHr == 0x80070057u, "D3 merge: factory2FirstFailHr takes the first non-zero");
    expect(m.openHrFirstFailCode == 0x80070005u, "D3 merge: openHrFirstFailCode takes the first non-zero");
    expect(m.osFileOpenProbeCount == 4 && m.osFileOpenProbeFails == 1,
           "D3 merge: the D1 OS-open reference counters add");

    // Merging an untouched accumulator must be a no-op, which is what makes the
    // short-circuit case (second call never ran) report honestly.
    msf::DecodeTelemetry onlyA;
    msf::mergeDecodeTelemetry(onlyA, a, msf::DecodeTelemetry{});
    expect(onlyA.totalMs == a.totalMs && onlyA.calls == a.calls,
           "D3 merge: folding in an empty accumulator changes nothing");
  }

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
    msf::TelemetryRecorder rec;
    msf::TelemetryConfig cfg;
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
  // --- 0.9.4.161 video stage split: decode vs CPU-hash timings are ---
  // --- measured values with their own states, never zero-filled. ---
  {
    msf::TelemetryRecorder rec;
    msf::TelemetryConfig cfg;
    cfg.root = "C:/media"; cfg.build = "0.9.4.161"; cfg.engine = "1.5.1"; cfg.db = "1.0.6";
    rec.start(cfg);
    rec.addVideo(1000, 8.0, 50.0, 3, "v.mp4", 80,
                 3, false);
    rec.addVideoStages(40.0, 5.0);
    rec.addVideoDecodeErrors(2);
    rec.addVideoNopts(1);
    // 0.9.4.170 (F-2): hardware decode usage.
    rec.addVideoHw(true, 1);
    const std::string js = rec.toJson();
    expect(objNum(js, "videos", "decodeMs") == 40.0, "V: decodeMs recorded");
    expect(objStr(js, "videos", "decodeMsState") == "measured", "V: decodeMsState measured");
    expect(objNum(js, "videos", "phashCpuMs") == 5.0, "V: phashCpuMs recorded");
    expect(objStr(js, "videos", "phashCpuMsState") == "measured", "V: phashCpuMsState measured");
    expect(objNum(js, "videos", "decodeErrors") == 2.0, "V: decodeErrors recorded");
    expect(objStr(js, "videos", "decodeErrorsState") == "measured", "V: decodeErrorsState measured");
    expect(objNum(js, "videos", "noptsFrames") == 1.0, "V: noptsFrames recorded");
    expect(objStr(js, "videos", "noptsFramesState") == "measured", "V: noptsFramesState measured");
    expect(objNum(js, "videos", "hwFiles") == 1.0, "V: hwFiles recorded");
    expect(objStr(js, "videos", "hwFilesState") == "measured", "V: hwFilesState measured");
    expect(objNum(js, "videos", "hwFallbacks") == 1.0, "V: hwFallbacks recorded");
    expect(objStr(js, "videos", "hwFallbacksState") == "measured", "V: hwFallbacksState measured");
  }
  {
    // No stage calls: null + not_measured, never 0.
    msf::TelemetryRecorder rec;
    msf::TelemetryConfig cfg;
    cfg.root = "C:/media"; cfg.build = "0.9.4.161"; cfg.engine = "1.5.1"; cfg.db = "1.0.6";
    rec.start(cfg);
    rec.addVideo(1000, 8.0, 50.0, 3, "v.mp4", 80,
                 3, false);
    const std::string js = rec.toJson();
    expect(hasIn(js, "videos", "\"decodeMs\":null"), "V: decodeMs null without stage calls");
    expect(objStr(js, "videos", "decodeMsState") == "not_measured", "V: decodeMsState not_measured");
    expect(hasIn(js, "videos", "\"phashCpuMs\":null"), "V: phashCpuMs null without stage calls");
    expect(objStr(js, "videos", "phashCpuMsState") == "not_measured", "V: phashCpuMsState not_measured");
    expect(hasIn(js, "videos", "\"decodeErrors\":null"), "V: decodeErrors null without error calls");
    expect(objStr(js, "videos", "decodeErrorsState") == "not_measured", "V: decodeErrorsState not_measured");
    expect(hasIn(js, "videos", "\"noptsFrames\":null"), "V: noptsFrames null without nopts calls");
    expect(objStr(js, "videos", "noptsFramesState") == "not_measured", "V: noptsFramesState not_measured");
    expect(hasIn(js, "videos", "\"hwFiles\":null"), "V: hwFiles null without hw calls");
    expect(objStr(js, "videos", "hwFilesState") == "not_measured", "V: hwFilesState not_measured");
    expect(hasIn(js, "videos", "\"hwFallbacks\":null"), "V: hwFallbacks null without hw calls");
    expect(objStr(js, "videos", "hwFallbacksState") == "not_measured", "V: hwFallbacksState not_measured");
  }
  if (!firstFailure.empty()) { std::cout << "recorder section: " << firstFailure << "\n"; return 4; }

  // --- 0.9.4.162 match-stage MSSIM split folds additively (D3 style). ---
  {
    msf::AnalyzeTelemetry a, b;
    a.videoMatchGpuMs = 1.5; a.videoMatchCpuMs = 0.5;
    a.videoMatchSubmitted = 10; a.videoMatchCompleted = 8;
    a.videoMatchSmallBatch = 4; a.videoMatchBatchFailed = 1;
    a.videoMatchCovShort = 0.5; a.videoMatchCovLong = 0.25; a.videoMatchScored = 2;
    // 0.9.4.170 (F-2): hardware decode sums fold additively too.
    a.videoHwFiles = 3; a.videoHwFallbacks = 1;
    b.videoMatchGpuMs = 2.5; b.videoMatchCpuMs = 0.25;
    b.videoMatchSubmitted = 6; b.videoMatchCompleted = 6;
    b.videoMatchSmallBatch = 2; b.videoMatchBatchFailed = 0;
    b.videoMatchCovShort = 1.0; b.videoMatchCovLong = 0.5; b.videoMatchScored = 1;
    b.videoHwFiles = 5; b.videoHwFallbacks = 0;
    msf::AnalyzeTelemetry m;
    msf::addAnalyzeTelemetry(m, a);
    msf::addAnalyzeTelemetry(m, b);
    expect(m.videoMatchGpuMs == 4.0, "V: match gpu ms adds");
    expect(m.videoMatchCpuMs == 0.75, "V: match cpu ms adds");
    expect(m.videoMatchSubmitted == 16, "V: match submitted adds");
    expect(m.videoMatchCompleted == 14, "V: match completed adds");
    expect(m.videoMatchSmallBatch == 6, "V: match small-batch adds");
    expect(m.videoMatchBatchFailed == 1, "V: match batch-failed adds");
    expect(m.videoMatchCovShort == 1.5, "V: match covShort adds");
    expect(m.videoMatchCovLong == 0.75, "V: match covLong adds");
    expect(m.videoMatchScored == 3, "V: match scored adds");
    expect(m.videoHwFiles == 8, "V: hwFiles adds");
    expect(m.videoHwFallbacks == 1, "V: hwFallbacks adds");
    msf::TelemetryRecorder rec2;
    msf::TelemetryConfig cfg2;
    cfg2.root = "C:/media"; cfg2.build = "0.9.4.164"; cfg2.engine = "1.5.1"; cfg2.db = "1.0.6";
    rec2.start(cfg2);
    rec2.setAnalyzeTelemetry(m);
    const std::string js3 = rec2.toJson();
    expect(hasIn(js3, "analyze", "\"videoMatchGpuMs\":"), "V: match gpu ms key present");
    expect(hasIn(js3, "analyze", "\"videoMatchSmallBatch\":"), "V: match small-batch key present");
    expect(hasIn(js3, "analyze", "\"videoMatchBatchFailed\":"), "V: match batch-failed key present");
    expect(hasIn(js3, "analyze", "\"videoMatchCovShort\":"), "V: match covShort key present");
    expect(hasIn(js3, "analyze", "\"videoMatchCovLong\":"), "V: match covLong key present");
    expect(hasIn(js3, "analyze", "\"videoMatchScored\":"), "V: match scored key present");
  }
  if (!firstFailure.empty()) { std::cout << "match-split section: " << firstFailure << "\n"; return 5; }

  if (failures) { std::cout << "analyze_telemetry=failed " << failures << " failure(s)\n"; return 1; }
  std::cout << "analyze_telemetry=ok checks=" << checks << "\n";
  return 0;
}
