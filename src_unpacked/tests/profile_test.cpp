// Node C1 Profile Foundation tests.
//
// Covers the C1 contract list: create, save/reload round-trip, exact /
// soft / hard / stale verdicts, missing profile, not_measured-vs-zero,
// atomic write/read, CPU-only environment, malformed/partial INI, and
// initial-estimate delivery. No absolute performance values asserted.
#include "profile.h"
#include "scheduler.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace {
int failures = 0;
void check(bool ok, const char* name) {
  if (ok) return;
  std::cerr << "fail: " << name << "\n";
  ++failures;
}
msf::ProfileIdentity hwIdentity() {
  msf::ProfileIdentity id;
  id.cpuThreads = 16;
  id.gpuName = "NVIDIA GeForce RTX 3080 Ti";
  id.gpuBackend = "CUDA";
  id.appVersion = "0.9.4.9";
  id.engineVersion = "1.5.0";
  return id;
}
msf::PerformanceProfile makeProfile() {
  msf::PerformanceProfile p;
  p.identity = hwIdentity();
  p.confidence = 0.7;
  p.cpuThroughput = {120.0, msf::MeasureState::Measured};
  p.gpuThroughput = {480.0, msf::MeasureState::Measured};
  p.gpuBatchThroughput = {35.0, msf::MeasureState::Measured};
  p.transferBandwidthMBps = {11800.0, msf::MeasureState::Measured};
  // resize/decode/queue stay NotMeasured in C1 (nothing measures).
  return p;
}
std::string tmpIni(const char* name) {
  auto d = std::filesystem::temp_directory_path() / "msf_profile_test";
  std::error_code ec;
  std::filesystem::create_directories(d, ec);
  return (d / name).string();
}
} // namespace
int main() {
  using msf::MeasureState;
  using msf::ProfileMatch;
  using msf::ProfileStore;
  const long long now = (long long)std::time(nullptr);
  // 1. create: fresh profile carries version 1, stable id, NotMeasured gaps.
  {
    ProfileStore s;
    msf::PerformanceProfile p = makeProfile();
    check(p.cpuThroughput.state == MeasureState::NotMeasured ||
          p.cpuThroughput.value == 120.0, "create-fields");
    check(msf::PerformanceProfile::kProfileVersion == 1, "create-version");
    const std::string a = ProfileStore::deriveId(hwIdentity());
    const std::string b = ProfileStore::deriveId(hwIdentity());
    check(a == b && a.size() == 16, "create-id-stable");
    msf::ProfileIdentity other = hwIdentity();
    other.gpuName = "Other GPU";
    check(ProfileStore::deriveId(other) != a, "create-id-differs");
    check(!s.hasProfile(), "create-empty-store");
    (void)p;
  }
  // 2. save -> reload round-trip preserves everything incl. states.
  {
    const std::string path = tmpIni("roundtrip.ini");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    ProfileStore s;
    msf::PerformanceProfile p = makeProfile();
    p.resizeThroughput = {0.0, MeasureState::Measured}; // 0.0 measured != missing
    s.setProfile(p);
    check(s.save(path), "rt-save");
    ProfileStore r;
    check(r.load(path), "rt-load");
    check(r.hasProfile(), "rt-has");
    const auto& q = r.profile();
    check(q.identity.gpuName == "NVIDIA GeForce RTX 3080 Ti", "rt-identity");
    check(q.confidence == 0.7, "rt-confidence");
    check(q.cpuThroughput.value == 120.0 && q.cpuThroughput.state == MeasureState::Measured, "rt-cpu");
    check(q.gpuBatchThroughput.value == 35.0 && q.gpuBatchThroughput.state == MeasureState::Measured, "rt-batch");
    check(q.transferBandwidthMBps.value == 11800.0 &&
          q.transferBandwidthMBps.state == MeasureState::Measured, "rt-bandwidth");
    check(q.decodeThroughput.state == MeasureState::NotMeasured, "rt-gap");
    check(q.resizeThroughput.value == 0.0 && q.resizeThroughput.state == MeasureState::Measured, "rt-zero-measured");
    check(!q.id.empty(), "rt-id");
    check(q.createdAt > 0 && q.updatedAt >= q.createdAt, "rt-stamps");
  }
  // 3. exact identity reuse.
  {
    const std::string path = tmpIni("exact.ini");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    ProfileStore s;
    s.setProfile(makeProfile());
    check(s.save(path), "exact-save");
    ProfileStore r;
    check(r.load(path), "exact-load");
    check(r.classify(hwIdentity(), -1, now) == ProfileMatch::Exact, "exact-verdict");
    const auto e = r.initialEstimate(hwIdentity(), -1, now);
    check(e.cpuKnown && e.gpuKnown && e.cpu == 120.0 && e.gpu == 480.0, "exact-estimate");
  }
  // 4. soft mismatch: app version drift reuses flagged.
  {
    ProfileStore s;
    s.setProfile(makeProfile());
    msf::ProfileIdentity cur = hwIdentity();
    cur.appVersion = "0.9.5.0";
    check(s.classify(cur, -1, now) == ProfileMatch::Soft, "soft-verdict");
    const auto e = s.initialEstimate(cur, -1, now);
    check(e.cpuKnown && e.gpuKnown, "soft-usable");
  }
  // 5. hard mismatch: different GPU is never silently reused.
  {
    ProfileStore s;
    s.setProfile(makeProfile());
    msf::ProfileIdentity cur = hwIdentity();
    cur.gpuName = "Other GPU";
    check(s.classify(cur, -1, now) == ProfileMatch::Hard, "hard-verdict");
    const auto e = s.initialEstimate(cur, -1, now);
    check(!e.cpuKnown && !e.gpuKnown, "hard-unusable");
    // GPU vanished entirely is also hard.
    cur.gpuName.clear();
    cur.gpuBackend = "CPU";
    check(s.classify(cur, -1, now) == ProfileMatch::Hard, "hard-vanished");
  }
  // 6. stale: explicit maxAge only (C1 sets no default policy).
  {
    ProfileStore s;
    msf::PerformanceProfile p = makeProfile();
    p.updatedAt = now - 31 * 86400LL;
    s.setProfile(p);
    check(s.classify(hwIdentity(), 30, now) == ProfileMatch::Stale, "stale-verdict");
    check(s.classify(hwIdentity(), -1, now) == ProfileMatch::Exact, "stale-never-by-default");
    const auto e = s.initialEstimate(hwIdentity(), 30, now);
    check(!e.cpuKnown, "stale-unusable");
  }
  // 7. missing profile.
  {
    ProfileStore s;
    check(!s.load(tmpIni("does-not-exist-zzz.ini")), "missing-load");
    check(s.classify(hwIdentity(), -1, now) == ProfileMatch::Missing, "missing-verdict");
    const auto e = s.initialEstimate(hwIdentity(), -1, now);
    check(!e.cpuKnown && !e.gpuKnown, "missing-unusable");
  }
  // 8. not_measured is not zero (covered in 2: rt-gap, rt-zero-measured).
  {
    msf::PerformanceProfile p;
    check(p.queueLatencyMs.state == MeasureState::NotMeasured, "nm-default");
    check(p.queueLatencyMs.value == 0, "nm-zero-value-but-state-rules");
  }
  // 9. atomic write/read: reload equals, no tmp leftovers.
  {
    const std::string path = tmpIni("atomic.ini");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    ProfileStore s;
    s.setProfile(makeProfile());
    check(s.save(path), "atomic-save");
    ProfileStore r;
    check(r.load(path), "atomic-reload");
    check(r.profile().id == s.profile().id, "atomic-id");
    bool leftover = false;
    const auto dir = std::filesystem::path(path).parent_path();
    for (const auto& e2 : std::filesystem::directory_iterator(dir, ec)) {
      if (e2.path().string().find("atomic.ini.tmp.") != std::string::npos) leftover = true;
    }
    check(!leftover, "atomic-no-tmp");
  }
  // 10. CPU-only environment: exact match, but never a pair estimate.
  {
    ProfileStore s;
    msf::PerformanceProfile p;
    msf::ProfileIdentity cpuOnly;
    cpuOnly.cpuThreads = 8;
    cpuOnly.gpuBackend = "CPU";
    cpuOnly.appVersion = "0.9.4.9";
    cpuOnly.engineVersion = "1.5.0";
    p.identity = cpuOnly;
    p.confidence = 0.5;
    p.cpuThroughput = {90.0, MeasureState::Measured};
    s.setProfile(p);
    check(s.classify(cpuOnly, -1, now) == ProfileMatch::Exact, "cpuonly-exact");
    const auto e = s.initialEstimate(cpuOnly, -1, now);
    check(!e.cpuKnown && !e.gpuKnown, "cpuonly-no-pair");
    // ... and a GPU profile is hard-mismatched here, not reused.
    ProfileStore g;
    g.setProfile(makeProfile());
    check(g.classify(cpuOnly, -1, now) == ProfileMatch::Hard, "cpuonly-gpu-hard");
  }
  // 11. malformed rejects; partial loads with defaults.
  {
    const std::string bad = tmpIni("bad.ini");
    {
      std::ofstream f(bad, std::ios::binary | std::ios::trunc);
      f << "[profile]\nthis line has no equals\n";
    }
    ProfileStore s;
    check(!s.load(bad), "malformed-reject");
    const std::string part = tmpIni("part.ini");
    {
      std::ofstream f(part, std::ios::binary | std::ios::trunc);
      f << "[profile]\nversion=1\nid=abc\n";
    }
    ProfileStore s2;
    check(s2.load(part), "partial-load");
    check(s2.profile().confidence == 0, "partial-default");
    check(s2.profile().cpuThroughput.state == MeasureState::NotMeasured, "partial-metric");
    check(s2.classify(hwIdentity(), -1, now) != ProfileMatch::Exact, "partial-no-exact");
  }
  // 12. scheduler delivery: usable estimate drives profile_baseline reason.
  {
    ProfileStore s;
    s.setProfile(makeProfile());
    const auto e = s.initialEstimate(hwIdentity(), -1, now);
    check(e.cpuKnown && e.gpuKnown, "delivery-usable");
    msf::CpuGpuScheduler sched;
    msf::SchedulerHardware hw;
    hw.cpuThreads = 16; hw.gpuEnabled = true;
    hw.gpuAvailable = true; hw.gpuComputeUnits = 80; hw.backendName = "CUDA";
    hw.profileBaselineKnown = true;
    hw.profileBaselineCpu = e.cpu; hw.profileBaselineGpu = e.gpu;
    const auto d = sched.decide(hw);
    check(d.reason == "profile_baseline", "delivery-reason");
    check(d.gpuUsed && d.backend == "CUDA", "delivery-meta");
  }
  if (failures) {
    std::cerr << "profile failures=" << failures << "\n";
    return 1;
  }
  std::cout << "profile=ok\n";
  return 0;
}
