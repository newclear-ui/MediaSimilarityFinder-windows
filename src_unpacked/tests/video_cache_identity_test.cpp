// 0.9.4.160 P2: quickIdentity cache-identity reliability pins.
//
// Cache key = path + size + mtime + quick_hash(first 64KB FNV-1a) +
// version + policy. Each case uses the SAME file path with a FRESH
// engine + FRESH persistent DB (primed first), so only the intended
// key component varies:
//  (C) control: identical file hits (cacheHit==true on rebuild).
//  (A) front-byte change with same size+mtime: MUST miss (cacheHit==false).
//      A stale true here would mean the quick-hash gate is bypassed.
//  (B) mtime bump with identical content: MUST miss.
//  (D) tail-only change (past 64KB) with same size+mtime: documents the
//      residual risk — quick_hash is unchanged so the stale row HITS.
//      Full-content hashing was deliberately rejected (I/O cost on
//      multi-GB videos); this pins the tradeoff so any future change
//      is deliberate. Revisit if field evidence shows tail-only
//      mutations matter.
#include "video_fingerprint.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int gen(const std::string& out) {
  const std::string c = std::string("ffmpeg -hide_banner -loglevel error -y -f lavfi"
      " -i testsrc=size=160x90:rate=10:duration=8"
      " -c:v mpeg4 -pix_fmt yuv420p -movflags +faststart \"") + out + "\"";
  return std::system(c.c_str());
}

static std::vector<char> readAll(const std::string& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static bool writeAll(const std::string& p, const std::vector<char>& b) {
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(b.data(), (std::streamsize)b.size());
  return (bool)out;
}

static int failures = 0;
static void check(bool cond, const char* msg) {
  if (cond) { std::cout << "  [ok] " << msg << "\n"; }
  else { std::cerr << "FAILED: " << msg << "\n"; ++failures; }
}

int main() {
  std::error_code ec;
  const auto d = fs::path("D:/Temp/OpenCodeWork/msf_video_cacheid");
  fs::remove_all(d, ec);
  fs::create_directories(d, ec);
  const std::string clip = (d / "clip.mp4").string();
  if (gen(clip) != 0 || !fs::exists(clip, ec)) {
    std::cerr << "fixture generation failed (environment)\n";
    return 2;
  }
  const auto origBytes = readAll(clip);
  if (origBytes.size() < 95000) {
    std::cerr << "fixture too small for tail-region test\n";
    return 2;
  }
  const auto origMt = fs::last_write_time(clip, ec); if (ec) return 2;
  auto restore = [&]() {
    if (!writeAll(clip, origBytes)) { std::cerr << "restore failed\n"; std::exit(2); }
    fs::last_write_time(clip, origMt, ec);
    if (ec) { std::cerr << "restore mtime failed\n"; std::exit(2); }
  };

  // (C) control.
  {
    msf::VideoFingerprintEngine e;
    if (!e.openPersistentCache((d / "dbC.sqlite").string())) return 3;
    msf::VideoFingerprint vf; msf::VideoBuildStats st;
    bool ok = e.build(clip, vf, nullptr, nullptr, &st);
    check(ok && !st.cacheHit, "control prime analyzes");
    msf::VideoFingerprint vf2; msf::VideoBuildStats st2;
    ok = e.build(clip, vf2, nullptr, nullptr, &st2);
    check(ok && st2.cacheHit, "control rebuild hits");
    check(vf2.hashes == vf.hashes, "control hash stable");
    e.closePersistentCache();
  }

  // (A) front change, same size+mtime -> MUST miss.
  {
    msf::VideoFingerprintEngine e;
    if (!e.openPersistentCache((d / "dbA.sqlite").string())) return 3;
    msf::VideoFingerprint vf; msf::VideoBuildStats st;
    if (!e.build(clip, vf, nullptr, nullptr, &st)) return 4;
    auto b = origBytes;
    b[100] ^= (char)0xFF;
    if (!writeAll(clip, b)) return 4;
    fs::last_write_time(clip, origMt, ec); if (ec) return 4;
    if (fs::file_size(clip, ec) != origBytes.size()) return 4;
    msf::VideoFingerprint vf2; msf::VideoBuildStats st2;
    (void)e.build(clip, vf2, nullptr, nullptr, &st2);
    check(!st2.cacheHit, "front change misses");
    e.closePersistentCache();
    restore();
  }

  // (B) mtime bump, identical content -> MUST miss.
  {
    msf::VideoFingerprintEngine e;
    if (!e.openPersistentCache((d / "dbB.sqlite").string())) return 3;
    msf::VideoFingerprint vf; msf::VideoBuildStats st;
    if (!e.build(clip, vf, nullptr, nullptr, &st)) return 5;
    fs::last_write_time(clip, origMt + std::chrono::seconds(10), ec); if (ec) return 5;
    msf::VideoFingerprint vf2; msf::VideoBuildStats st2;
    bool ok = e.build(clip, vf2, nullptr, nullptr, &st2);
    check(ok && !st2.cacheHit, "mtime bump misses");
    e.closePersistentCache();
    restore();
  }

  // (D) tail-only change past 64KB, same size+mtime -> stale HIT
  // (documented residual risk). Probe for a flipped offset that still
  // decodes, using a cacheless engine, so the pin does not depend on
  // decoder error tolerance.
  {
    const std::vector<std::size_t> cands = {70000, 75000, 80000, 85000, 90000};
    std::size_t chosen = 0;
    for (std::size_t off : cands) {
      if (off >= origBytes.size()) break;
      auto b = origBytes;
      b[off] ^= (char)0xFF;
      const std::string probe = (d / "probe.mp4").string();
      if (!writeAll(probe, b)) return 6;
      msf::VideoFingerprintEngine clean;
      msf::VideoFingerprint vf;
      if (clean.build(probe, vf)) { chosen = off; break; }
    }
    if (!chosen) { std::cerr << "no decodable tail offset\n"; return 7; }
    fs::remove(d / "probe.mp4", ec);
    msf::VideoFingerprintEngine e;
    if (!e.openPersistentCache((d / "dbD.sqlite").string())) return 3;
    msf::VideoFingerprint vf; msf::VideoBuildStats st;
    if (!e.build(clip, vf, nullptr, nullptr, &st)) return 6;
    auto b = origBytes;
    b[chosen] ^= (char)0xFF;
    if (!writeAll(clip, b)) return 6;
    fs::last_write_time(clip, origMt, ec); if (ec) return 6;
    msf::VideoFingerprint vf2; msf::VideoBuildStats st2;
    bool ok = e.build(clip, vf2, nullptr, nullptr, &st2);
    check(ok && st2.cacheHit, "tail-only change hits (residual risk)");
    std::cout << "  [info] tail offset=" << chosen << "\n";
    e.closePersistentCache();
    restore();
  }

  fs::remove_all(d, ec);
  if (failures) { std::cerr << "video_cache_identity FAILED\n"; return 1; }
  std::cout << "video_cache_identity=ok\n";
  return 0;
}
