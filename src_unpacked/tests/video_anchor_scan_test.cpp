// 0.9.4.173 anchor scan-path test (directive §8): the SAME product scan path
// runs with LegacyV3 and with TemporalAnchorV2 armed, and must produce the
// IDENTICAL match set (anchor runs report-only until its gates pass).
// Uses small generated files + ScanWorker (like video_scan_e2e_test).
#include "mainwindow.h"
#include "scan_worker.h"

#include <QCoreApplication>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <utility>

namespace fs = std::filesystem;
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
static std::set<std::pair<std::string, std::string>> runScan(const std::string& root,
                                                             const std::string& ad,
                                                             msf::VideoReadStrategy strat) {
  ScanWorker w(QString::fromStdString(root), QString::fromStdString(ad), 8, 50, 50, false, false,
               true);
  msf::VideoPolicy p;
  p.readStrategy = strat;
  w.setVideoPolicy(p);
  w.run();
  std::set<std::pair<std::string, std::string>> out;
  for (const auto& m : w.takePending()) {
    // Compare by file NAME: the two scans run in different roots (m1 vs m2)
    // with identical content, so full paths can never match.
    auto base = [](const std::string& p) {
      const std::size_t i = p.find_last_of("/\\");
      return (i == std::string::npos) ? p : p.substr(i + 1);
    };
    std::string a = base(m.left.toStdString()), b = base(m.right.toStdString());
    if (a > b) std::swap(a, b);
    out.emplace(a, b);
  }
  return out;
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  auto d = fs::path("D:/Temp/OpenCodeWork/msf_anchor_scan");
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d / "m1", ec);
  fs::create_directories(d / "a1", ec);
  fs::create_directories(d / "m2", ec);
  fs::create_directories(d / "a2", ec);
  const std::string o1 = (d / "m1" / "o.mp4").string();
  const std::string c1 = (d / "m1" / "c.mp4").string();
  const std::string r1 = (d / "m1" / "r.mp4").string();
  const std::string t1 = (d / "m1" / "t.mp4").string();
  auto sh = [](const std::string& c) { return std::system(c.c_str()); };
  if (sh("ffmpeg -hide_banner -loglevel error -y -f lavfi -i \"testsrc=size=160x90:rate=10:duration=12,format=yuv420p\" -c:v mpeg4 \"" + o1 + "\"") != 0) return 1;
  // Exact duplicate via stream copy (identical content, distinct file).
  if (sh("ffmpeg -hide_banner -loglevel error -y -i \"" + o1 + "\" -c copy \"" + c1 + "\"") != 0) return 1;
  if (sh("ffmpeg -hide_banner -loglevel error -y -i \"" + o1 + "\" -vf scale=128:72 -c:v mpeg4 -q:v 3 \"" + r1 + "\"") != 0) return 1;
  if (sh("ffmpeg -hide_banner -loglevel error -y -f lavfi -i \"color=c=green:size=160x90:rate=10:duration=12,format=yuv420p\" -c:v mpeg4 \"" + t1 + "\"") != 0) return 1;
  // LegacyV3 default scan (no anchor env): baseline match set.
  setEnv("MSF_VIDEO_ANCHOR", nullptr);
  // NOTE: separate media/app dirs on purpose: the anchor build must not reuse
  // another scan's index rows; the content is identical by construction.
  auto cp = [&](const std::string& a, const std::string& b) {
    return sh("ffmpeg -hide_banner -loglevel error -y -i \"" + a + "\" -c copy \"" + b + "\"");
  };
  if (cp(o1, (d / "m2" / "o.mp4").string()) != 0) return 1;
  if (cp(c1, (d / "m2" / "c.mp4").string()) != 0) return 1;
  if (cp(r1, (d / "m2" / "r.mp4").string()) != 0) return 1;
  if (cp(t1, (d / "m2" / "t.mp4").string()) != 0) return 1;
  auto mLegacy = runScan((d / "m1").string(), (d / "a1").string(), msf::VideoReadStrategy::LegacyV3);
  // Armed TemporalAnchorV2 scan: must yield the identical match set.
  setEnv("MSF_VIDEO_ANCHOR", "1");
  auto mAnchor = runScan((d / "m2").string(), (d / "a2").string(), msf::VideoReadStrategy::TemporalAnchorV2);
  setEnv("MSF_VIDEO_ANCHOR", nullptr);
  std::cout << "  [info] legacy=" << mLegacy.size() << " anchor=" << mAnchor.size() << "\n";
  for (const auto& m : mLegacy) std::cout << "  [legacy] " << m.first << " <> " << m.second << "\n";
  for (const auto& m : mAnchor) std::cout << "  [anchor] " << m.first << " <> " << m.second << "\n";
  if (mAnchor != mLegacy) {
    std::cerr << "anchor scan changed the match set\n";
    return 2;
  }
  // The duplicate pair must be found on the real product path in both modes.
  // (Normalized pair order is alphabetical: "c.mp4" < "o.mp4".)
  if (mAnchor.count({"c.mp4", "o.mp4"}) == 0) {
    std::cerr << "duplicate pair missing on anchor path\n";
    return 3;
  }
  fs::remove_all(d, ec);
  std::cout << "video_anchor_scan=ok\n";
  return 0;
}
