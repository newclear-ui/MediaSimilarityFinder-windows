#pragma once
// Node C1: Profile Foundation (data model + INI store, no calibration).
//
// PerformanceProfile is the long-term hardware baseline the scheduler uses
// as its initial estimate. Live runtime measurement always overrides it;
// the store never invents numbers (unmeasured stays NotMeasured, never 0).
// C1 builds the machinery with zero live behavior change: nothing writes
// profiles yet (C2 runs the first calibration), so the engine path below
// always falls back to hardware baselines until then.
//
// C2/C3 own what C1 deliberately leaves open: calibration durations and
// probes, confidence adjustment values, stale default age, recalibration
// thresholds. C1 stores the fields and classifies; it does not tune.
#include <cstdint>
#include <string>
#include "benchmark.h" // MeasureState reuse (Node A rule: no duplicate states)
namespace msf {
std::string detectCpuModel();

struct ProfileIdentity {
  // cpuModel may be empty in C1 (no portable CPU-model source in msf_core
  // yet); the classifier treats empty-vs-set as Soft, never as a match.
  // C2/C4 fills it from a local CPU identity source when available; an empty value remains unknown.
  std::string cpuModel;
  int cpuThreads = 0;
  std::string gpuName; // empty when no concrete backend resolved
  std::string gpuBackend = "CPU";
  // driver string is empty only when the backend cannot report it. Empty-vs-set counts as Soft.
  std::string driver;
  std::string appVersion;
  std::string engineVersion;
};
struct ProfileMetric {
  double value = 0;
  MeasureState state = MeasureState::NotMeasured;
};
// C3: last profile update record (brief §5 [lastUpdate]). Written on every
// recalibration decision (consistent update or inconsistent keep); absent
// means "never updated since creation". Old/new ids are usually equal
// (identity-based ids); they are kept to show continuity, not change.
struct ProfileUpdateRecord {
  std::string reason; // recalibration_consistent | recalibration_inconsistent
  std::string oldProfileId;
  double oldConfidence = 0;
  double newConfidence = 0;
  bool present = false;
};
struct PerformanceProfile {
  static constexpr int kProfileVersion = 1;
  // Normal development default for staleness checks (classify/initialEstimate
  // maxAgeDays); negative age stays reserved for explicit diagnostics.
  static constexpr long long kDefaultMaxAgeDays = 30;
  // Opaque stable id: FNV-1a hex over the canonical identity. Same hardware
  // unit -> same id across restarts; identity VERDICTS use fields, not id.
  std::string id;
  ProfileIdentity identity;
  // Stored as-is in C1; adjustment policy is C2/C3 territory.
  double confidence = 0;
  long long createdAt = 0; // epoch seconds, set on first save
  long long updatedAt = 0; // epoch seconds, refreshed on every save
  ProfileMetric cpuThroughput;    // images/sec (C2 measures)
  ProfileMetric gpuThroughput;    // images/sec (C2 measures)
  ProfileMetric gpuBatchThroughput; // batches/sec (C2 measures; brief §5)
  ProfileMetric resizeThroughput; // px/sec (C2 measures)
  ProfileMetric decodeThroughput; // frames/sec (C2 measures)
  // Transfer is stored as measured BANDWIDTH (brief §5 concept). The
  // scheduler's cost term converts with bytes-per-unit at use time.
  ProfileMetric transferBandwidthMBps; // MB/s (C2 measures)
  ProfileMetric queueLatencyMs;   // ms (D1 instruments; C1: not_measured)
  ProfileUpdateRecord lastUpdate; // C3: last recalibration decision
};
enum class ProfileMatch { Missing, Exact, Soft, Hard, Stale };
struct InitialEstimate {
  bool cpuKnown = false, gpuKnown = false;
  double cpu = 0, gpu = 0; // images/sec; usable only as a pair (see below)
};
class ProfileStore {
public:
  bool load(const std::string& path);
  // Atomic replacement (temp file in the same directory + rename with a
  // remove+rename fallback). Failure never throws; the caller keeps
  // running without a profile (a profile must never fail a search).
  // Refreshes updatedAt (and createdAt when unset) on success.
  bool save(const std::string& path);
  void setProfile(PerformanceProfile p) { profile_ = std::move(p); has_ = true; }
  const PerformanceProfile& profile() const { return profile_; }
  bool hasProfile() const { return has_; }
  void clear() { profile_ = PerformanceProfile{}; has_ = false; }
  static std::string deriveId(const ProfileIdentity& id);
  static const char* matchName(ProfileMatch m);
  // Order: Missing -> Hard -> Stale -> Soft -> Exact (see classify).
  ProfileMatch classify(const ProfileIdentity& current, long long maxAgeDays, long long nowSec) const;
  // Usable only when the verdict is Exact/Soft AND both throughputs are
  // measured. Pair-or-nothing: overriding one baseline while keeping the
  // other would corrupt the scheduler ratio. CPU-only machines therefore
  // never yield an estimate (their GPU path is fallback by construction).
  InitialEstimate initialEstimate(const ProfileIdentity& current, long long maxAgeDays, long long nowSec) const;
private:
  PerformanceProfile profile_;
  bool has_ = false;
};

bool profileNeedsCalibration(const PerformanceProfile& profile, bool gpuEnabled, bool gpuAvailable);
}
