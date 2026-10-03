// E-3B end-to-end validation.
//
// This drives the REAL production path: msf::MediaSearchEngine::scan(). The
// earlier E-2A/E-2B probes reimplemented decode loops inside the test, which is
// fine for isolating one behaviour but cannot answer the question that actually
// matters here: does enabling the planner change what the product fingerprints?
//
// So this compares, per file, every hash the product keeps. Three modes:
//
//   A  MSF_VIDEO_SAMPLING=off            sequential, the shipped default
//   B  MSF_VIDEO_SAMPLING=adaptive       the planner decides
//   C  MSF_VIDEO_SAMPLING=forced-sparse  diagnostic: skip the cost decision
//
// A vs B is the release question. A vs C is calibration: C forces files the
// planner would have refused, so C mismatches are the cost of a wrong decision
// made deliberately, and they measure the exactness risk directly.
//
// Usage: msf_video_sampling_e2e_test <datasetRoot> <scratchAppDir> [repeats]

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <filesystem>

#include "../src/media_search_engine.h"
#include "../src/path_utils.h"

namespace {

struct FileHashes {
    std::uint64_t fp=0, mirror=0, c43=0, c11=0, c916=0, m43=0, m11=0, m916=0;
    bool operator==(const FileHashes& o) const {
        return fp==o.fp && mirror==o.mirror && c43==o.c43 && c11==o.c11 && c916==o.c916
            && m43==o.m43 && m11==o.m11 && m916==o.m916;
    }
    bool nonZero() const { return fp!=0; }
};

struct RunResult {
    std::map<std::string, FileHashes> byPath;
    std::vector<std::string> videoPaths;
    double wallMs=0.0;
    std::string json;
    std::size_t analyzed=0;
};

void setMode(const char* m) { _putenv_s("MSF_VIDEO_SAMPLING", m ? m : "off"); }

RunResult runScan(const std::string& root, const std::string& app) {
    RunResult r;
    // A distinct app dir keeps every index cold. Reusing one would let run 2+
    // skip analysis entirely and turn the measurement into a no-op.
    std::error_code ec;
    std::filesystem::remove_all(msf::path_from_utf8(app), ec);
    std::filesystem::create_directories(msf::path_from_utf8(app), ec);

    msf::MediaSearchEngine engine;
    if (!engine.openIndexForRoot(root, app)) {
        std::fprintf(stderr, "openIndexForRoot failed\n");
        return r;
    }
    msf::ScanControl control;
    control.buildVersion = "0.9.4.42";
    const auto t0 = std::chrono::steady_clock::now();
    const msf::SearchReport rep = engine.scan(root, 8, &control);
    r.wallMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    r.json = engine.telemetryJson();
    r.analyzed = rep.analyzed;
    for (const auto& f : engine.files()) {
        if (f.kind != msf::MediaKind::Video) continue;
        const std::string key = msf::path_to_utf8(f.path);
        r.byPath[key] = FileHashes{f.fingerprint, f.mirrorFingerprint, f.crop4x3, f.crop1x1,
                                   f.crop9x16, f.mirrorCrop4x3, f.mirrorCrop1x1, f.mirrorCrop9x16};
        r.videoPaths.push_back(key);
    }
    std::filesystem::remove_all(msf::path_from_utf8(app), ec);
    return r;
}

// Per-file comparison. Returns the number of files whose hashes differ.
struct Compare { std::size_t compared=0, mismatched=0, missing=0; std::vector<std::string> bad; };

Compare compare(const RunResult& base, const RunResult& other) {
    Compare c;
    for (const auto& kv : base.byPath) {
        auto it = other.byPath.find(kv.first);
        if (it == other.byPath.end()) { ++c.missing; c.bad.push_back("MISSING  " + kv.first); continue; }
        ++c.compared;
        if (!(it->second == kv.second)) { ++c.mismatched; c.bad.push_back("MISMATCH " + kv.first); }
    }
    return c;
}

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <datasetRoot> <scratchAppDir> [repeats]\n", argv[0]);
        return 2;
    }
    const std::string root = argv[1];
    const std::string app  = argv[2];
    const int repeats = argc > 3 ? std::atoi(argv[3]) : 3;

    std::printf("E-3B end-to-end sampling validation (production MediaSearchEngine::scan)\n");
    std::printf("root=%s repeats=%d\n\n", root.c_str(), repeats);

    std::vector<double> wallOff, wallAdapt, wallForce;
    Compare adaptVsOff, forceVsOff, controlVsOff;
    std::size_t videoCount = 0;

    for (int i = 0; i < repeats; ++i) {
        // Interleaved per repeat so a machine that drifts warmer or busier over
        // the run cannot systematically favour whichever mode ran first.
        setMode("off");            RunResult a  = runScan(root, app + "_A");
        setMode("adaptive");       RunResult b  = runScan(root, app + "_B");
        setMode("forced-sparse");  RunResult c  = runScan(root, app + "_C");
        // Control: a SECOND identical sequential run. Without it, any mismatch
        // below would be attributed to the planner even if the file is simply
        // non-deterministic under repeated decode. A mismatch here means the
        // file itself is unstable and the other comparisons prove nothing.
        setMode("off");            RunResult a2 = runScan(root, app + "_A2");

        if (i == 0) {
            videoCount = a.videoPaths.size();
            controlVsOff = compare(a, a2);
            adaptVsOff   = compare(a, b);
            forceVsOff   = compare(a, c);
            std::printf("videos=%zu analyzed(A)=%zu analyzed(B)=%zu analyzed(C)=%zu\n\n",
                        videoCount, a.analyzed, b.analyzed, c.analyzed);
        }
        wallOff.push_back(a.wallMs);
        wallAdapt.push_back(b.wallMs);
        wallForce.push_back(c.wallMs);
        std::printf("run %d: off=%.0f ms  off-again=%.0f ms  adaptive=%.0f ms  forced-sparse=%.0f ms\n",
                    i + 1, a.wallMs, a2.wallMs, b.wallMs, c.wallMs);
    }
    setMode("off");

    std::printf("\n--- exactness (all 8 product hashes per file) ---\n");
    std::printf("A vs A (control)   : compared=%zu mismatched=%zu missing=%zu\n",
                controlVsOff.compared, controlVsOff.mismatched, controlVsOff.missing);
    for (const auto& s : controlVsOff.bad) std::printf("    %s\n", s.c_str());
    std::printf("A vs B (adaptive)   : compared=%zu mismatched=%zu missing=%zu\n",
                adaptVsOff.compared, adaptVsOff.mismatched, adaptVsOff.missing);
    for (const auto& s : adaptVsOff.bad) std::printf("    %s\n", s.c_str());
    std::printf("A vs C (forced)     : compared=%zu mismatched=%zu missing=%zu\n",
                forceVsOff.compared, forceVsOff.mismatched, forceVsOff.missing);
    for (const auto& s : forceVsOff.bad) std::printf("    %s\n", s.c_str());

    const double mo = median(wallOff), ma = median(wallAdapt), mf = median(wallForce);
    std::printf("\n--- end-to-end wall clock (median of %d) ---\n", repeats);
    std::printf("off            = %.0f ms\n", mo);
    std::printf("adaptive       = %.0f ms  (%+.2f%% vs off)\n", ma, mo > 0 ? (ma - mo) / mo * 100.0 : 0.0);
    std::printf("forced-sparse  = %.0f ms  (%+.2f%% vs off)\n", mf, mo > 0 ? (mf - mo) / mo * 100.0 : 0.0);

    // The planner must not change results. A mismatch here is a release blocker,
    // not a tuning opportunity, so it is the only hard failure this test reports.
    if (controlVsOff.mismatched) {
        std::printf("\nRESULT: INCONCLUSIVE - sequential mode is not reproducible against itself.\n");
        std::printf("       The listed files change fingerprint between two identical runs, so\n");
        std::printf("       the A-vs-B and A-vs-C numbers cannot be attributed to the planner.\n");
        return 3;
    }
    if (adaptVsOff.mismatched || adaptVsOff.missing) {
        std::printf("\nRESULT: FAIL - adaptive mode changed fingerprints\n");
        return 1;
    }
    std::printf("\nRESULT: PASS - adaptive mode is bit-identical to the shipped sequential path\n");
    std::printf("NOTE:  forced-sparse mismatches=%zu are the calibration signal for the cost model\n",
                forceVsOff.mismatched);

    // The planner telemetry is a machine-readable contract other tools read, so
    // it is worth being able to look at it after a run rather than only
    // counting files. A malformed object here would silently break consumers.
    RunResult last = runScan(root, app + "_JSON");
    if (const char* path = std::getenv("MSF_DUMP_JSON")) {
        std::FILE* fh = std::fopen(path, "wb");
        if (fh) { std::fwrite(last.json.data(), 1, last.json.size(), fh); std::fclose(fh); }
    }
    const std::size_t s = last.json.find("\"sampling\":{");
    if (s == std::string::npos) {
        std::printf("FAIL: benchmark JSON has no sampling object\n");
        return 1;
    }
    std::printf("\n--- benchmark JSON sampling object ---\n");
    std::size_t e = s;
    int depth = 0;
    for (; e < last.json.size(); ++e) {
        if (last.json[e] == '{') ++depth;
        else if (last.json[e] == '}') { --depth; if (depth == 0) { ++e; break; } }
    }
    std::printf("%.*s\n\n", (int)(e - s), last.json.c_str() + s);
    return 0;
}
