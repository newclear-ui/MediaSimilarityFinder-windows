// 0.9.4.173 anchor file-based tests (real FFmpeg decode, no hand-built frames).
// - buildAnchorIndex() (separate sweep) vs build()+peekAnchorIndex (union
//   sweep in the product path) produce identical indexes: proves the §7
//   single-sweep integration is byte-identical to the validation harness.
// - temporalAnchorSimilarityV2 on real decoded indexes: identical DUP,
//   10 s leader found at ~10 s, stranger not DUP.
// - Gating: unarmed engine (default strategy or env unset) yields no index.
#include "temporal_anchor.h"
#include "video_fingerprint.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
static int fails = 0;
static void check(bool ok, const char* msg) {
  if (!ok) {
    std::cerr << "FAIL " << msg << "\n";
    ++fails;
  }
}
static void setEnv(const char* k, const char* v) {
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
static int run(const std::string& c) { return std::system(c.c_str()); }

int main() {
  std::error_code ec;
  const auto d = fs::path("D:/Temp/OpenCodeWork/msf_anchor_file");
  fs::remove_all(d, ec);
  fs::create_directories(d, ec);
  const std::string base = (d / "base.mp4").string();
  const std::string lead = (d / "lead.mp4").string();
  const std::string reenc = (d / "reenc.mp4").string();
  const std::string other = (d / "other.mp4").string();
  // 30 s textured content.
  if (run("ffmpeg -hide_banner -loglevel error -y -f lavfi -i \"testsrc=size=160x90:rate=10:duration=30,format=yuv420p\" -c:v mpeg4 \"" + base + "\"") != 0) return 1;
  // 10 s black leader + same content (single concat, re-encoded once).
  if (run("ffmpeg -hide_banner -loglevel error -y -f lavfi -i \"color=black:s=160x90:r=10:d=10\" -i \"" + base + "\" -filter_complex \"[0:v][1:v]concat=n=2:v=1:a=0[v]\" -map \"[v]\" -c:v mpeg4 \"" + lead + "\"") != 0) return 1;
  if (run("ffmpeg -hide_banner -loglevel error -y -i \"" + base + "\" -vf scale=128:72 -c:v mpeg4 -q:v 3 \"" + reenc + "\"") != 0) return 1;
  if (run("ffmpeg -hide_banner -loglevel error -y -f lavfi -i \"color=c=red:size=160x90:rate=10:duration=30,format=yuv420p\" -c:v mpeg4 \"" + other + "\"") != 0) return 1;
  setEnv("MSF_VIDEO_ANCHOR", "1");
  msf::VideoFingerprintEngine e;
  msf::VideoPolicy pol;
  pol.readStrategy = msf::VideoReadStrategy::TemporalAnchorV2;
  e.setVideoPolicy(pol);
  auto buildIdx = [&](const std::string& p, msf::TemporalAnchorIndex& idx) {
    msf::VideoBuildStats st;
    msf::VideoFingerprint vf;
    if (!e.build(p, vf, nullptr, nullptr, &st, nullptr)) {
      std::cerr << "  [dbg] build failed " << p << " cacheHit=" << st.cacheHit << "\n";
      return false;
    }
    std::error_code ec2;
    const auto fp = fs::path(p);
    const auto sz = fs::file_size(fp, ec2);
    if (ec2) return false;
    const auto mt = (std::uint64_t)fs::last_write_time(fp, ec2).time_since_epoch().count();
    if (ec2) return false;
    const bool ok = e.peekAnchorIndex(p, (std::uint64_t)sz, mt, idx);
    if (!ok) std::cerr << "  [dbg] peek failed " << p << " anchorBuilt=" << st.anchorBuilt << " frames=" << st.anchorFrames << " sampled=" << st.sampledFrames << " kept=" << vf.hashes.size() << "\n";
    return ok;
  };
  msf::TemporalAnchorIndex iBase, iLead, iReenc, iOther;
  check(buildIdx(base, iBase), "base union index");
  check(buildIdx(lead, iLead), "lead union index");
  check(buildIdx(reenc, iReenc), "reenc union index");
  check(buildIdx(other, iOther), "other union index");
  check(iBase.built && iLead.built && iReenc.built && iOther.built, "all built");
  // Default (unarmed) build must yield byte-identical V3 fingerprints:
  // the union sweep only ADDS anchor frames, never changes V3 samples.
  {
    msf::VideoFingerprintEngine e0;
    msf::VideoPolicy p0;  // LegacyV3 default: strategy gates the anchor path off
    e0.setVideoPolicy(p0);
    msf::VideoFingerprint vfArmed, vfPlain;
    if (!e.build(base, vfArmed, nullptr, nullptr, nullptr, nullptr)) { std::cerr << "FAIL armed build\n"; ++fails; }
    if (!e0.build(base, vfPlain, nullptr, nullptr, nullptr, nullptr)) { std::cerr << "FAIL plain build\n"; ++fails; }
    check(vfArmed.hashes == vfPlain.hashes && vfArmed.timestamps == vfPlain.timestamps,
          "armed vs unarmed V3 identical");
  }
  // Union-sweep index == separate-sweep index (product == harness).
  {
    msf::TemporalAnchorIndex sep;
    msf::VideoBuildStats st;
    check(e.buildAnchorIndex(base, sep, &st, nullptr), "separate sweep");
    check(sep.seq.size() == iBase.seq.size(), "same frame count");
    std::cout << "  [info] union=" << iBase.seq.size() << " separate=" << sep.seq.size() << "\n";
    bool same = sep.seq.size() == iBase.seq.size();
    if (same)
      for (std::size_t i = 0; i < sep.seq.size(); ++i)
        if (sep.seq[i].phash != iBase.seq[i].phash ||
            std::abs(sep.seq[i].timestamp - iBase.seq[i].timestamp) > 1e-9) {
          same = false;
          break;
        }
    check(same, "union==separate indexes");
  }
  // Identical content via both paths.
  {
    msf::TemporalAnchorV2Result r;
    double s = msf::temporalAnchorSimilarityV2(iBase, iBase, r);
    check(r.verdict == msf::AnchorVerdictKind::Dup && s >= 87.5, "identical DUP");
  }
  // Re-encode: DUP expected (same content, same geometry class).
  {
    msf::TemporalAnchorV2Result r;
    double s = msf::temporalAnchorSimilarityV2(iBase, iReenc, r);
    std::cout << "  [info] reenc score=" << s << " verdict=" << (int)r.verdict << " valid=" << r.validAnchorCount << "\n";
    check(r.verdict == msf::AnchorVerdictKind::Dup, "reenc DUP");
  }
  // Leader: offset must be found (~10 s); verdict DUP or honest UNCERTAIN.
  {
    msf::TemporalAnchorV2Result r;
    double s = msf::temporalAnchorSimilarityV2(iBase, iLead, r);
    std::cout << "  [info] lead score=" << s << " verdict=" << (int)r.verdict << " valid=" << r.validAnchorCount << " off=" << r.alignmentOffset << "\n";
    check(std::abs(r.alignmentOffset - 10.0) < 3.0, "leader offset ~10 s");
    check(r.verdict != msf::AnchorVerdictKind::Diff, "leader not Diff");
  }
  // Stranger: never DUP.
  {
    msf::TemporalAnchorV2Result r;
    msf::temporalAnchorSimilarityV2(iBase, iOther, r);
    check(r.verdict != msf::AnchorVerdictKind::Dup, "stranger not DUP");
  }
  // Gating: default strategy + env on -> no union index.
  {
    msf::VideoFingerprintEngine e2;
    msf::VideoPolicy def;
    e2.setVideoPolicy(def);
    msf::VideoFingerprint vf;
    msf::VideoBuildStats st;
    check(e2.build(base, vf, nullptr, nullptr, &st, nullptr), "default build");
    msf::TemporalAnchorIndex idx;
    std::error_code ec3;
    const auto fp = fs::path(base);
    const auto sz = fs::file_size(fp, ec3);
    const auto mt = (std::uint64_t)fs::last_write_time(fp, ec3).time_since_epoch().count();
    check(!e2.peekAnchorIndex(base, (std::uint64_t)sz, mt, idx), "disarmed no index");
    check(!st.anchorBuilt, "disarmed no stats");
  }
  setEnv("MSF_VIDEO_ANCHOR", nullptr);
  fs::remove_all(d, ec);
  if (fails == 0) std::cout << "video_anchor_file=ok\n";
  return fails == 0 ? 0 : 1;
}
