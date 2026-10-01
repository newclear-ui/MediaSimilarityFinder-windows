// S6-4 benchmark aggregation.
//
// S6-3 produced deterministic cohorts. This stage turns them into statistics, and
// the checks concentrate on the ways an aggregate can quietly lie:
//
//   * a Skipped or Failed mode must not enter a mean as a 0 ms;
//   * a 0 ms run wall duration is a measurement, not a missing value, and its one
//     second resolution must travel with it;
//   * "all" must not become images+videos, and must not donate elapsed time to them;
//   * observed must equal eligible plus excluded, with the reason for each exclusion;
//   * the same file described by duplicated journal records must be counted once;
//   * case, mode and run metrics must never be added together or presented as one
//     comparable number.

#include <cstdio>
#include <string>
#include <vector>

#include "benchmark_core.h"
#include "benchmark_data_aggregation.h"
#include "benchmark_data_grouping.h"
#include "benchmark_data_mining.h"

namespace {

int gChecks = 0;
int gFails = 0;

void chk(bool ok, const std::string& what) {
    ++gChecks;
    if (ok) std::printf("  [ok] %s\n", what.c_str());
    else { ++gFails; std::printf("  [F] %s\n", what.c_str()); }
}

// --- fixture builders -------------------------------------------------------

msf::IngestModeResult makeMode(msf::GpuBackendKind req, msf::GpuBackendKind eff,
                               msf::BenchmarkStatus st, std::optional<double> elapsed) {
    msf::IngestModeResult m;
    m.requestedMode = req;
    m.effectiveMode = eff;
    m.status = st;
    m.started = (st != msf::BenchmarkStatus::Skipped);
    m.completed = m.started;
    m.elapsedMs = elapsed;
    return m;
}

msf::IngestCase makeCase(const std::string& id, const std::string& path,
                         msf::BenchmarkStatus st, double elapsed,
                         const std::vector<msf::IngestModeResult>& modes) {
    msf::IngestCase c;
    c.caseId = id;
    c.path = path;
    c.media = msf::MediaKind::Image;
    c.status = st;
    // case_complete.elapsedMs is S2's contract: the sum of this case's mode elapsed
    // times, excluding discovery. The aggregation layer must not redefine it.
    c.elapsedMs = elapsed;
    c.modes = modes;
    return c;
}

// Shorthand for the case-only fixtures, which do not care about the path.
msf::IngestCase makeCase(const std::string& id, msf::BenchmarkStatus st, double elapsed,
                         const std::vector<msf::IngestModeResult>& modes) {
    return makeCase(id, "/d/" + id, st, elapsed, modes);
}

msf::IngestRun makeRun(const std::string& runId, const std::string& journal,
                       const std::string& fp, const std::string& scope,
                       const std::string& build, msf::GitCommitState gs,
                       const std::string& git, std::optional<msf::BenchmarkStatus> status,
                       std::optional<double> wallMs,
                       const std::vector<msf::IngestCase>& cases) {
    msf::IngestRun r;
    r.sourceJournalPath = journal;
    r.runId = runId;
    r.suiteId = journal;
    if (!fp.empty()) {
        r.datasetFingerprint = fp;
        r.datasetIdentity = msf::DatasetIdentityState::Valid;
    } else {
        r.datasetIdentity = msf::DatasetIdentityState::Missing;
    }
    r.buildVersion = build;
    r.gitCommitState = gs;
    if (gs == msf::GitCommitState::Known) r.gitCommit = git;
    r.mediaScope = scope;
    r.runStatus = status;
    r.wallDurationMs = wallMs;
    r.runClass = status.has_value() ? msf::IngestRunClass::Complete : msf::IngestRunClass::Incomplete;
    r.cases = cases;
    return r;
}

const msf::AggregatedScope* findScope(const msf::BenchmarkAggregation& a, const std::string& fp,
                                      const std::string& scope) {
    for (const auto& d : a.datasets) {
        if (d.key.datasetFingerprint != fp) continue;
        for (const auto& s : d.scopes)
            if (s.key.mediaScope == scope) return &s;
    }
    return nullptr;
}

const msf::AggregatedBuild* findBuild(const msf::AggregatedScope* s,
                                      const std::string& build, msf::GitCommitState gs) {
    if (s == nullptr) return nullptr;
    for (const auto& b : s->builds)
        if (b.key.buildVersion == build && b.key.gitCommitState == gs) return &b;
    return nullptr;
}

const msf::AggregatedMode* findMode(const msf::AggregatedBuild* b, msf::GpuBackendKind req,
                                    msf::GpuBackendKind eff) {
    if (b == nullptr) return nullptr;
    for (const auto& m : b->modes)
        if (m.key.requestedMode == req && m.key.effectiveMode == eff) return &m;
    return nullptr;
}

const msf::AggregatedCase* findCase(const msf::BenchmarkAggregation& a, const std::string& id) {
    for (const auto& c : a.cases)
        if (c.key.caseId == id) return &c;
    return nullptr;
}

std::size_t statusCount(const std::vector<msf::StatusTally>& t, msf::BenchmarkStatus s) {
    for (const auto& e : t)
        if (e.status == s) return e.count;
    return 0;
}

std::size_t exclusionCount(const msf::SampleAccounting& a, msf::SampleExclusion r) {
    for (const auto& e : a.exclusions)
        if (e.reason == r) return e.count;
    return 0;
}

bool hasEligible(const msf::DurationStatistics& d) {
    return d.meanMs.has_value() && d.medianMs.has_value() && d.minMs.has_value() &&
           d.maxMs.has_value() && d.p95Ms.has_value();
}

const double kEps = 1e-6;

// --- fixtures ---------------------------------------------------------------

// A: statistics basics over three cases.
msf::NormalizedBenchmarkData basicDataset() {
    msf::NormalizedBenchmarkData d;
    const auto ok = [](double v) { return std::optional<double>{v}; };
    auto r = makeRun("run-1", "j1", "fp-1", "images", "1.0.0", msf::GitCommitState::Known,
                     "commitA", msf::BenchmarkStatus::Success, ok(7000.0),
                     {makeCase("c1", "/d/c1.jpg", msf::BenchmarkStatus::Success, 100.0,
                               {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                         msf::BenchmarkStatus::Success, ok(100.0))}),
                      makeCase("c2", "/d/c2.jpg", msf::BenchmarkStatus::Success, 200.0,
                               {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                         msf::BenchmarkStatus::Success, ok(200.0))}),
                      makeCase("c3", "/d/c3.jpg", msf::BenchmarkStatus::Success, 300.0,
                               {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                         msf::BenchmarkStatus::Success, ok(300.0))})});
    d.runs.push_back(r);
    return d;
}

// B: the population rules. Skipped, Failed, Cancelled, Success-without-duration.
msf::NormalizedBenchmarkData statusDataset() {
    msf::NormalizedBenchmarkData d;
    const auto ok = [](double v) { return std::optional<double>{v}; };
    auto r = makeRun(
        "run-1", "j1", "fp-1", "images", "1.0.0", msf::GitCommitState::Known, "commitA",
        msf::BenchmarkStatus::Success, ok(1000.0),
        {// Skipped with elapsed absent: must be counted, never averaged as 0.
         makeCase("c1", "/d/c1.jpg", msf::BenchmarkStatus::Skipped, 0.0,
                  {makeMode(msf::GpuBackendKind::Cuda, msf::GpuBackendKind::Cpu,
                            msf::BenchmarkStatus::Skipped, std::nullopt)}),
         // Failed mode inside a Successful case.
         makeCase("c2", "/d/c2.jpg", msf::BenchmarkStatus::Success, 0.0,
                  {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                            msf::BenchmarkStatus::Failed, ok(5.0))}),
         // Cancelled mode.
         makeCase("c3", "/d/c3.jpg", msf::BenchmarkStatus::Success, 0.0,
                  {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                            msf::BenchmarkStatus::Cancelled, ok(7.0))}),
         // Success with a real duration.
         makeCase("c4", "/d/c4.jpg", msf::BenchmarkStatus::Success, 42.0,
                  {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                            msf::BenchmarkStatus::Success, ok(42.0))}),
         // Success but no duration recorded: MissingElapsed, not a 0 ms sample.
         makeCase("c5", "/d/c5.jpg", msf::BenchmarkStatus::Success, 0.0,
                  {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                            msf::BenchmarkStatus::Success, std::nullopt)})});
    d.runs.push_back(r);
    return d;
}

// C: run-level wall duration, including a same-second 0 and a missing terminal.
msf::NormalizedBenchmarkData runDataset() {
    msf::NormalizedBenchmarkData d;
    const auto ok = [](double v) { return std::optional<double>{v}; };
    d.runs.push_back(makeRun("run-1", "j1", "fp-1", "images", "1.0.0",
                             msf::GitCommitState::Known, "commitA", msf::BenchmarkStatus::Success,
                             ok(0.0), {}));
    d.runs.push_back(makeRun("run-2", "j2", "fp-1", "images", "1.0.0",
                             msf::GitCommitState::Known, "commitA", msf::BenchmarkStatus::Success,
                             ok(7000.0), {}));
    d.runs.push_back(makeRun("run-3", "j3", "fp-1", "images", "1.0.0",
                             msf::GitCommitState::Known, "commitA", msf::BenchmarkStatus::Success,
                             std::nullopt, {}));
    d.runs.push_back(makeRun("run-4", "j4", "fp-1", "images", "1.0.0",
                             msf::GitCommitState::Known, "commitA", msf::BenchmarkStatus::Failed,
                             ok(3000.0), {}));
    return d;
}

// D: scopes stay apart, and "all" never becomes images+videos.
msf::NormalizedBenchmarkData scopeDataset() {
    msf::NormalizedBenchmarkData d;
    const auto ok = [](double v) { return std::optional<double>{v}; };
    auto one = [&](const char* id, const char* fp, const char* scope, double v) {
        return makeRun(id, "j", fp, scope, "1.0.0", msf::GitCommitState::Known, "commitA",
                       msf::BenchmarkStatus::Success, ok(v),
                       {makeCase(std::string("c") + id, "/d/c.jpg",
                                 msf::BenchmarkStatus::Success, 10.0,
                                 {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                           msf::BenchmarkStatus::Success, ok(10.0))})});
    };
    d.runs.push_back(one("r-all1", "fp-1", "all", 1000.0));
    d.runs.push_back(one("r-all2", "fp-1", "all", 3000.0));
    d.runs.push_back(one("r-img", "fp-1", "images", 2000.0));
    d.runs.push_back(one("r-vid", "fp-1", "videos", 4000.0));
    d.runs.push_back(one("r-other", "fp-2", "images", 9000.0));
    return d;
}

// E: two builds at different commits, plus Unknown and Legacy.
msf::NormalizedBenchmarkData buildDataset() {
    msf::NormalizedBenchmarkData d;
    const auto ok = [](double v) { return std::optional<double>{v}; };
    auto one = [&](const char* id, const char* build, msf::GitCommitState gs,
                   const char* git, double v) {
        return makeRun(id, "j", "fp-1", "images", build, gs, git, msf::BenchmarkStatus::Success,
                       ok(v),
                       {makeCase(std::string("c") + id, "/d/c.jpg",
                                 msf::BenchmarkStatus::Success, 10.0,
                                 {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                           msf::BenchmarkStatus::Success, ok(10.0))})});
    };
    d.runs.push_back(one("r-a", "1.0.0", msf::GitCommitState::Known, "commitA", 1000.0));
    d.runs.push_back(one("r-b", "2.0.0", msf::GitCommitState::Known, "commitB", 2000.0));
    d.runs.push_back(one("r-u", "1.0.0", msf::GitCommitState::Unknown, "", 3000.0));
    d.runs.push_back(one("r-l", "0.9.0", msf::GitCommitState::Legacy, "", 4000.0));
    return d;
}

// Two Known builds over one dataset and scope, each with its own measured cases.
//
// This is a SYNTHETIC FIXTURE for the future known-known shape, not a benchmark result
// and not data from the real store.
msf::NormalizedBenchmarkData buildCaseElapsedDataset() {
    msf::NormalizedBenchmarkData d;
    const auto ok = [](double v) { return std::optional<double>{v}; };
    auto mk = [&](const std::string& id, const std::string& journal, const std::string& build,
                  const std::string& git, const std::vector<double>& caseMs) {
        std::vector<msf::IngestCase> cs;
        for (std::size_t i = 0; i < caseMs.size(); ++i)
            cs.push_back(makeCase("c" + std::to_string(i), msf::BenchmarkStatus::Success, caseMs[i],
                                  {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                            msf::BenchmarkStatus::Success, ok(caseMs[i]))}));
        return makeRun(id, journal, "fp-1", "images", build, msf::GitCommitState::Known, git,
                       msf::BenchmarkStatus::Success, ok(1000.0), cs);
    };
    // Build A: 100, 120  -> mean 110
    d.runs.push_back(mk("run-a", "jA", "1.0.0", "commitA", {100.0, 120.0}));
    // Build B: 150, 180  -> mean 165
    d.runs.push_back(mk("run-b", "jB", "2.0.0", "commitB", {150.0, 180.0}));
    return d;
}

}  // namespace

int main(int argc, char** argv) {
    std::printf("S6-4 benchmark aggregation\n");

    // --- case and mode statistics, basics ----------------------------------
    {
        const auto d = basicDataset();
        const auto g = msf::groupBenchmarks(d);
        const auto a = msf::aggregateBenchmarks(d, g);
        const auto* sc = findScope(a, "fp-1", "images");
        chk(sc != nullptr, "a scope cohort aggregates");

        const msf::AggregatedBuild* b = findBuild(sc, "1.0.0", msf::GitCommitState::Known);
        const msf::AggregatedMode* m =
            findMode(b, msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu);
        chk(m != nullptr, "a mode cohort aggregates");
        if (m != nullptr) {
            chk(m->elapsed.sampleCount == 3, "three mode samples");
            chk(hasEligible(m->elapsed), "mean, median, min, max and p95 are all present");
            chk(m->elapsed.minMs.has_value() && std::abs(*m->elapsed.minMs - 100.0) < kEps,
                "mode min is 100");
            chk(m->elapsed.maxMs.has_value() && std::abs(*m->elapsed.maxMs - 300.0) < kEps,
                "mode max is 300");
            chk(m->elapsed.meanMs.has_value() && std::abs(*m->elapsed.meanMs - 200.0) < kEps,
                "mode mean is 200");
            chk(m->elapsed.medianMs.has_value() && std::abs(*m->elapsed.medianMs - 200.0) < kEps,
                "mode median is 200 for an odd count");
            chk(m->elapsed.p95Ms.has_value() && std::abs(*m->elapsed.p95Ms - 300.0) < kEps,
                "mode p95 is 300 by nearest rank");
        }
        if (sc != nullptr) {
            chk(sc->caseElapsed.sampleCount == 3, "three case samples");
            chk(sc->caseElapsed.medianMs.has_value() &&
                    std::abs(*sc->caseElapsed.medianMs - 200.0) < kEps,
                "case median is 200");
            chk(sc->caseElapsed.level == msf::MetricLevel::CaseElapsed,
                "case metric is labelled CaseElapsed, not ModeElapsed");
        }
        chk(a.modePopulation.balanced() && a.casePopulation.balanced() &&
                a.runPopulation.balanced(),
            "observed == eligible + excluded at every top-level population");
        chk(a.modePopulation.observed == 3 && a.casePopulation.observed == 3,
            "populations counted three logical records, not raw lines");
    }

    // --- even-count median -------------------------------------------------
    {
        msf::NormalizedBenchmarkData d;
        const auto ok = [](double v) { return std::optional<double>{v}; };
        d.runs.push_back(makeRun("r", "j", "fp-1", "images", "1.0.0", msf::GitCommitState::Known,
                                 "commitA", msf::BenchmarkStatus::Success, ok(1000.0),
                                 {makeCase("c1", "/d/1", msf::BenchmarkStatus::Success, 10.0,
                                           {makeMode(msf::GpuBackendKind::Cpu,
                                                     msf::GpuBackendKind::Cpu,
                                                     msf::BenchmarkStatus::Success, ok(10.0))}),
                                  makeCase("c2", "/d/2", msf::BenchmarkStatus::Success, 20.0,
                                           {makeMode(msf::GpuBackendKind::Cpu,
                                                     msf::GpuBackendKind::Cpu,
                                                     msf::BenchmarkStatus::Success, ok(20.0))})}));
        const auto a = msf::aggregateBenchmarks(d, msf::groupBenchmarks(d));
        const auto* sc = findScope(a, "fp-1", "images");
        chk(sc && sc->caseElapsed.sampleCount == 2 &&
                std::abs(*sc->caseElapsed.medianMs - 15.0) < kEps,
            "median of an even count is the mean of the two middle samples");
        chk(sc && sc->caseElapsed.sampleCount == 2 &&
                std::abs(*sc->caseElapsed.p95Ms - 20.0) < kEps,
            "p95 of n=2 is reported rather than hidden, and equals the max");
    }

    // --- population rules: skipped, failed, cancelled, missing elapsed -----
    {
        const auto d = statusDataset();
        const auto a = msf::aggregateBenchmarks(d, msf::groupBenchmarks(d));
        const auto* sc = findScope(a, "fp-1", "images");
        const auto* b = findBuild(sc, "1.0.0", msf::GitCommitState::Known);

        chk(a.modePopulation.observed == 5, "all five mode results are observed");
        chk(a.modePopulation.eligible == 1, "exactly one mode is an eligible timing sample");
        chk(a.modePopulation.excluded == 4, "four modes are excluded from performance stats");
        chk(exclusionCount(a.modePopulation, msf::SampleExclusion::Skipped) == 1,
            "the skipped mode is excluded as skipped");
        chk(exclusionCount(a.modePopulation, msf::SampleExclusion::Failed) == 1,
            "the failed mode is excluded as failed");
        chk(exclusionCount(a.modePopulation, msf::SampleExclusion::Cancelled) == 1,
            "the cancelled mode is excluded as cancelled");
        chk(exclusionCount(a.modePopulation, msf::SampleExclusion::MissingElapsed) == 1,
            "a Success with no duration is excluded as missingElapsed, not read as 0");

        const auto* cpu = findMode(b, msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu);
        chk(cpu != nullptr, "the CPU cohort exists");
        if (cpu != nullptr) {
            chk(cpu->elapsed.sampleCount == 1, "the CPU elapsed population has one sample");
            chk(cpu->elapsed.meanMs.has_value() && std::abs(*cpu->elapsed.meanMs - 42.0) < kEps,
                "the mean is 42, so no excluded mode contributed 0 to it");
            chk(cpu->elapsed.minMs.has_value() && std::abs(*cpu->elapsed.minMs - 42.0) < kEps,
                "the min is 42, so a 0 was not injected");
        }
        const auto* cuda = findMode(b, msf::GpuBackendKind::Cuda, msf::GpuBackendKind::Cpu);
        chk(cuda != nullptr, "the CUDA-requested cohort exists separately from CPU");
        if (cuda != nullptr) {
            chk(cuda->elapsed.sampleCount == 0, "the skipped CUDA cohort has no elapsed samples");
            chk(!cuda->elapsed.meanMs.has_value(),
                "an empty population has no mean, rather than a mean of 0");
            chk(statusCount(cuda->statuses, msf::BenchmarkStatus::Skipped) == 1,
                "the skipped mode is still counted in the status tally");
            chk(cuda->accounting.observed == 1 && cuda->accounting.eligible == 0,
                "the skipped cohort records one observed and zero eligible");
        }

        // A Skipped CASE is excluded from the case performance population too.
        chk(a.casePopulation.observed == 5, "five cases observed");
        chk(a.casePopulation.eligible == 4, "the skipped case is not an eligible case sample");
        chk(exclusionCount(a.casePopulation, msf::SampleExclusion::Skipped) == 1,
            "the skipped case is excluded as skipped");
        chk(statusCount(sc ? sc->caseStatuses : std::vector<msf::StatusTally>{},
                        msf::BenchmarkStatus::Skipped) == 1,
            "the skipped case still appears in the case status tally");
    }

    // --- run wall duration and one-second resolution ------------------------
    {
        const auto d = runDataset();
        const auto a = msf::aggregateBenchmarks(d, msf::groupBenchmarks(d));
        const auto* sc = findScope(a, "fp-1", "images");
        chk(sc != nullptr, "run-level aggregation happens under the scope cohort");
        if (sc != nullptr) {
            chk(sc->runWallDuration.level == msf::MetricLevel::RunWallDuration,
                "run metric is labelled RunWallDuration");
            chk(sc->runWallDuration.resolution == msf::MetricResolution::OneSecond,
                "run wall duration carries OneSecond resolution");
            chk(sc->runWallDuration.sampleCount == 2,
                "two runs are eligible: the Success runs that have a wall duration");
            chk(sc->runWallDuration.minMs.has_value() &&
                    std::abs(*sc->runWallDuration.minMs) < kEps,
                "a same-second run yields 0 ms and 0 is a real sample, not a missing one");
            chk(sc->runWallDuration.maxMs.has_value() &&
                    std::abs(*sc->runWallDuration.maxMs - 7000.0) < kEps,
                "run wall max is 7000");
            chk(sc->runWallDuration.meanMs.has_value() &&
                    std::abs(*sc->runWallDuration.meanMs - 3500.0) < kEps,
                "run wall mean is 3500, computed over 0 and 7000");
            chk(sc->runWallDuration.resolution == msf::MetricResolution::OneSecond &&
                    std::abs(*sc->runWallDuration.maxMs) < 1e9 &&
                    (static_cast<long long>(*sc->runWallDuration.maxMs) % 1000) == 0,
                "every run wall value is a multiple of 1000 ms");
        }
        chk(exclusionCount(a.runPopulation, msf::SampleExclusion::MissingElapsed) == 1,
            "a Success run with no wall duration is excluded as missingElapsed");
        chk(exclusionCount(a.runPopulation, msf::SampleExclusion::Failed) == 1,
            "a failed run is excluded as failed");
        chk(a.runPopulation.observed == 4 && a.runPopulation.eligible == 2,
            "run accounting is 4 observed and 2 eligible");
        chk(a.runPopulation.balanced(), "run accounting balances");
    }

    // --- a run with no terminal record is Invalid, not an assumed Success ---
    {
        msf::NormalizedBenchmarkData d;
        const auto ok = [](double v) { return std::optional<double>{v}; };
        d.runs.push_back(makeRun("r", "j", "fp-1", "images", "1.0.0",
                                 msf::GitCommitState::Known, "commitA", std::nullopt, ok(5000.0),
                                 {}));
        const auto a = msf::aggregateBenchmarks(d, msf::groupBenchmarks(d));
        chk(exclusionCount(a.runPopulation, msf::SampleExclusion::Invalid) == 1,
            "a run with no terminal record is excluded as invalid");
        chk(a.runPopulation.eligible == 0, "a run with no status is never eligible");
        const auto* sc = findScope(a, "fp-1", "images");
        chk(sc && statusCount(sc->runStatuses, msf::BenchmarkStatus::Success) == 0,
            "a run with no status is not tallied as Success");
    }

    // --- scopes stay apart --------------------------------------------------
    {
        const auto d = scopeDataset();
        const auto a = msf::aggregateBenchmarks(d, msf::groupBenchmarks(d));
        const auto* all = findScope(a, "fp-1", "all");
        const auto* img = findScope(a, "fp-1", "images");
        const auto* vid = findScope(a, "fp-1", "videos");
        const auto* other = findScope(a, "fp-2", "images");
        chk(all != nullptr && img != nullptr && vid != nullptr,
            "all, images and videos each aggregate separately");
        chk(all && all->runAccounting.observed == 2, "the all cohort has its own two runs");
        chk(img && img->runAccounting.observed == 1, "the images cohort has one run");
        chk(vid && vid->runAccounting.observed == 1, "the videos cohort has one run");
        chk(other != nullptr && other->runAccounting.observed == 1,
            "a different fingerprint aggregates under its own dataset");
        chk(all && all->runWallDuration.sampleCount == 2 &&
                std::abs(*all->runWallDuration.meanMs - 2000.0) < kEps,
            "the all cohort's mean is its own 2000, not a blend of the three scopes");
        chk(all && img && vid && all->caseElapsed.sampleCount == 2 &&
                img->caseElapsed.sampleCount == 1 && vid->caseElapsed.sampleCount == 1,
            "no elapsed time was donated between scopes");
        chk(img && vid && img->caseElapsed.sampleCount + vid->caseElapsed.sampleCount ==
                             all->caseElapsed.sampleCount,
            "the coincidence that all equals images+videos here is arithmetic, not decomposition");
        chk(a.datasets.size() == 2, "two dataset cohorts");
    }

    // --- build provenance ---------------------------------------------------
    {
        const auto d = buildDataset();
        const auto a = msf::aggregateBenchmarks(d, msf::groupBenchmarks(d));
        const auto* sc = findScope(a, "fp-1", "images");
        const auto* bA = findBuild(sc, "1.0.0", msf::GitCommitState::Known);
        const auto* bB = findBuild(sc, "2.0.0", msf::GitCommitState::Known);
        const auto* bU = findBuild(sc, "1.0.0", msf::GitCommitState::Unknown);
        const auto* bL = findBuild(sc, "0.9.0", msf::GitCommitState::Legacy);
        chk(bA && bB && bA != bB, "two Known commits are two build cohorts");
        chk(bA && bA->key.gitCommit == "commitA", "a Known build keeps its commit");
        chk(bU != nullptr, "an Unknown build forms its own cohort");
        chk(bU && bU->key.gitCommit.empty(), "an Unknown build carries no commit value");
        chk(bL != nullptr, "a Legacy build forms its own cohort");
        chk(bL && bL->key.gitCommit.empty(), "a Legacy build carries no commit value");
        chk(bA && bA->commitComparable, "a Known build is marked commit-comparable");
        chk(bU && !bU->commitComparable, "an Unknown build is not commit-comparable");
        chk(bL && !bL->commitComparable, "a Legacy build is not commit-comparable");
        chk(sc && sc->builds.size() == 4, "four distinct build cohorts, none merged");
        chk(sc && sc->builds[0].key < sc->builds[1].key,
            "build cohorts are in typed-key order");
    }

    // --- duplicate protection ----------------------------------------------
    {
        // The real store contains a suite written three times, which yields
        // duplicated raw journal records. The normalized data holds one logical
        // record, so aggregation must count it once.
        const auto d = basicDataset();
        const auto a = msf::aggregateBenchmarks(d, msf::groupBenchmarks(d));
        chk(a.modePopulation.observed == 3,
            "three logical mode entities produce three observed samples, not nine lines");
        chk(a.casePopulation.observed == 3, "three logical cases produce three observed samples");
        const auto* c1 = findCase(a, "c1");
        chk(c1 != nullptr, "a case-level aggregate exists per file");
        chk(c1 && c1->accounting.observed == 1,
            "a case observed in one run counts once even if its journal was written repeatedly");
        chk(c1 && c1->elapsed.sampleCount == 1, "its statistics use that one sample");
    }

    // --- multi-run in one journal ------------------------------------------
    {
        msf::NormalizedBenchmarkData d;
        const auto ok = [](double v) { return std::optional<double>{v}; };
        // Both runs live in the same journal and share the scope/build cohort.
        d.runs.push_back(makeRun("run-A", "j-shared", "fp-1", "images", "1.0.0",
                                 msf::GitCommitState::Known, "commitA",
                                 msf::BenchmarkStatus::Success, ok(1000.0),
                                 {makeCase("a1", "/d/a1.jpg", msf::BenchmarkStatus::Success, 10.0,
                                           {makeMode(msf::GpuBackendKind::Cpu,
                                                     msf::GpuBackendKind::Cpu,
                                                     msf::BenchmarkStatus::Success, ok(10.0))})}));
        d.runs.push_back(makeRun("run-B", "j-shared", "fp-1", "images", "1.0.0",
                                 msf::GitCommitState::Known, "commitA",
                                 msf::BenchmarkStatus::Success, ok(2000.0),
                                 {makeCase("b1", "/d/b1.jpg", msf::BenchmarkStatus::Success, 20.0,
                                           {makeMode(msf::GpuBackendKind::Cpu,
                                                     msf::GpuBackendKind::Cpu,
                                                     msf::BenchmarkStatus::Success, ok(20.0))})}));
        const auto a = msf::aggregateBenchmarks(d, msf::groupBenchmarks(d));
        const auto* sc = findScope(a, "fp-1", "images");
        chk(sc && sc->runAccounting.observed == 2, "both runs of one journal are separate samples");
        chk(sc && sc->caseElapsed.sampleCount == 2 && std::abs(*sc->caseElapsed.meanMs - 15.0) < kEps,
            "the two runs' cases average together without mixing their identities");
        const auto* ca = findCase(a, "a1");
        const auto* cb = findCase(a, "b1");
        chk(ca && cb && ca != cb, "the two runs' cases are distinct case cohorts");
    }

    // --- metric levels never mix -------------------------------------------
    {
        const auto d = basicDataset();
        const auto a = msf::aggregateBenchmarks(d, msf::groupBenchmarks(d));
        const auto* sc = findScope(a, "fp-1", "images");
        const auto* b = findBuild(sc, "1.0.0", msf::GitCommitState::Known);
        const auto* m = findMode(b, msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu);
        chk(m && m->elapsed.level == msf::MetricLevel::ModeElapsed &&
                m->elapsed.resolution == msf::MetricResolution::Recorded,
            "a mode metric is ModeElapsed at Recorded resolution");
        chk(sc && sc->caseElapsed.level == msf::MetricLevel::CaseElapsed &&
                sc->caseElapsed.resolution == msf::MetricResolution::Recorded,
            "a case metric is CaseElapsed at Recorded resolution");
        chk(sc && sc->runWallDuration.level == msf::MetricLevel::RunWallDuration &&
                sc->runWallDuration.resolution == msf::MetricResolution::OneSecond,
            "a run metric is RunWallDuration at OneSecond resolution");
        chk(std::string(msf::metricResolutionName(msf::MetricResolution::OneSecond)) ==
                "OneSecond",
            "the resolution name is explicit in the output");
        // No metric was built by adding another level's samples.
        chk(a.modePopulation.eligible == a.casePopulation.eligible,
            "mode and case populations are separately accounted, not one shared pool");
    }

    // --- empty and no-eligible populations ----------------------------------
    {
        msf::NormalizedBenchmarkData d;
        d.runs.push_back(makeRun("r", "j", "fp-1", "images", "1.0.0", msf::GitCommitState::Known,
                                 "commitA", msf::BenchmarkStatus::Success,
                                 std::optional<double>{1000.0},
                                 {makeCase("c", "/d/c", msf::BenchmarkStatus::Skipped, 0.0,
                                           {makeMode(msf::GpuBackendKind::Cpu,
                                                     msf::GpuBackendKind::Cpu,
                                                     msf::BenchmarkStatus::Skipped,
                                                     std::nullopt)})}));
        const auto a = msf::aggregateBenchmarks(d, msf::groupBenchmarks(d));
        const auto* sc = findScope(a, "fp-1", "images");
        const auto* b = findBuild(sc, "1.0.0", msf::GitCommitState::Known);
        const auto* m = findMode(b, msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu);
        chk(m && m->elapsed.empty() && !m->elapsed.meanMs.has_value(),
            "a population with no eligible sample has no mean at all");
        chk(sc && sc->caseElapsed.empty(), "a scope with no eligible case has no statistics");
        chk(sc && sc->runWallDuration.sampleCount == 1,
            "the run itself is still an eligible wall-duration sample");
        chk(m && m->accounting.balanced() && m->accounting.observed == 1,
            "its accounting still balances");
    }

    // --- a runId shared by two journals ------------------------------------
    //
    // The real store contains suite-ORDER-A/B/C, which each wrote a journal under
    // one shared runId. runId alone is therefore not a safe identity: keying on it
    // would keep only one of the three and drop the other's cases, and deduping on
    // it would discard a measurement on the assumption the data cannot support.
    {
        msf::NormalizedBenchmarkData d;
        const auto ok = [](double v) { return std::optional<double>{v}; };
        auto mk = [&](const char* journal, const char* id, double v) {
            return makeRun(id, journal, "fp-1", "images", "1.0.0", msf::GitCommitState::Known,
                           "commitA", msf::BenchmarkStatus::Success, ok(v),
                           {makeCase("c1", "/d/c1.jpg", msf::BenchmarkStatus::Success, 10.0,
                                     {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                               msf::BenchmarkStatus::Success, ok(10.0))})});
        };
        d.runs.push_back(mk("j-A", "same-run", 1000.0));
        d.runs.push_back(mk("j-B", "same-run", 2000.0));
        d.runs.push_back(mk("j-C", "same-run", 3000.0));

        const auto a = msf::aggregateBenchmarks(d, msf::groupBenchmarks(d));
        chk(a.distinctRunIds == 1, "three accepted runs share one runId");
        chk(a.runsWithCollidingIdentity == 3, "all three runs are reported as colliding");
        chk(a.runPopulation.observed == 3, "each journal's run is a separate run sample");
        chk(a.casePopulation.observed == 3,
            "no case observation was dropped by a runId lookup");
        const auto* c1 = findCase(a, "c1");
        chk(c1 != nullptr && c1->accounting.observed == 3,
            "the case cohort holds all three observations rather than one");
        chk(c1 && c1->elapsed.sampleCount == 3, "its statistics use all three");
    }

    // --- build-level case elapsed -----------------------------------------
    //
    // The synthetic known-known shape from the S6-4 completion: two builds over one
    // dataset and scope, each with its own measured cases. This is a TEST FIXTURE, not a
    // benchmark result.
    {
        const auto d = buildCaseElapsedDataset();
        const auto a = msf::aggregateBenchmarks(d, msf::groupBenchmarks(d));
        const auto* sc = findScope(a, "fp-1", "images");
        chk(sc != nullptr, "the scope cohort aggregates");
        const auto* bA = findBuild(sc, "1.0.0", msf::GitCommitState::Known);
        const auto* bB = findBuild(sc, "2.0.0", msf::GitCommitState::Known);
        chk(bA != nullptr && bB != nullptr && bA != bB, "two Known builds form two cohorts");

        if (bA != nullptr) {
            chk(bA->caseElapsed.sampleCount == 2, "build A has 2 case elapsed samples");
            chk(bA->caseElapsed.level == msf::MetricLevel::CaseElapsed,
                "build case metric is labelled CaseElapsed");
            chk(bA->caseElapsed.resolution == msf::MetricResolution::Recorded,
                "build case metric is Recorded, not one-second");
            chk(bA->caseElapsed.minMs.has_value() && std::abs(*bA->caseElapsed.minMs - 100.0) < kEps,
                "build A case min is 100");
            chk(bA->caseElapsed.maxMs.has_value() && std::abs(*bA->caseElapsed.maxMs - 120.0) < kEps,
                "build A case max is 120");
            chk(bA->caseElapsed.meanMs.has_value() && std::abs(*bA->caseElapsed.meanMs - 110.0) < kEps,
                "build A case mean is 110");
            chk(bA->caseElapsed.medianMs.has_value() &&
                    std::abs(*bA->caseElapsed.medianMs - 110.0) < kEps,
                "build A case median is 110");
            chk(bA->caseElapsed.p95Ms.has_value() && std::abs(*bA->caseElapsed.p95Ms - 120.0) < kEps,
                "build A case p95 is 120");
            chk(bA->caseAccounting.observed == 2 && bA->caseAccounting.eligible == 2,
                "build A case accounting is 2 observed and 2 eligible");
            chk(bA->caseAccounting.balanced(), "build A case accounting balances");
        }
        if (bB != nullptr) {
            chk(bB->caseElapsed.sampleCount == 2, "build B has 2 case elapsed samples");
            chk(bB->caseElapsed.meanMs.has_value() && std::abs(*bB->caseElapsed.meanMs - 165.0) < kEps,
                "build B case mean is 165");
        }
        chk(sc && sc->caseElapsed.sampleCount == 4,
            "the scope view still sees all 4 cases, so the build view did not steal them");
        chk(sc && sc->caseElapsed.meanMs.has_value() &&
                std::abs(*sc->caseElapsed.meanMs - 137.5) < kEps,
            "the scope mean is unchanged at 137.5");
    }

    // --- mode-level case elapsed ------------------------------------------
    {
        // Same dataset and build, CPU/CPU and AUTO/CPU. The case population is filtered
        // by which mode actually ran, and the two modes do not get equalised.
        msf::NormalizedBenchmarkData d;
        const auto ok = [](double v) { return std::optional<double>{v}; };
        std::vector<msf::IngestCase> cs;
        cs.push_back(makeCase("c1", msf::BenchmarkStatus::Success, 100.0,
                              {makeMode(msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu,
                                        msf::BenchmarkStatus::Success, ok(60.0)),
                               makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                        msf::BenchmarkStatus::Success, ok(40.0))}));
        cs.push_back(makeCase("c2", msf::BenchmarkStatus::Success, 200.0,
                              {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                        msf::BenchmarkStatus::Success, ok(200.0))}));
        d.runs.push_back(makeRun("r", "j", "fp-1", "images", "1.0.0", msf::GitCommitState::Known,
                                 "commitA", msf::BenchmarkStatus::Success, ok(1000.0), cs));
        const auto a = msf::aggregateBenchmarks(d, msf::groupBenchmarks(d));
        const auto* sc = findScope(a, "fp-1", "images");
        const auto* b = findBuild(sc, "1.0.0", msf::GitCommitState::Known);
        const auto* cpu = findMode(b, msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu);
        const auto* autoMode =
            findMode(b, msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu);

        chk(cpu != nullptr && autoMode != nullptr,
            "the CPU/CPU and AUTO/CPU cohorts stay separate");
        if (cpu != nullptr) {
            chk(cpu->caseElapsed.sampleCount == 2,
                "CPU/CPU covers both cases, so its case population is 2");
            chk(cpu->caseElapsed.meanMs.has_value() &&
                    std::abs(*cpu->caseElapsed.meanMs - 150.0) < kEps,
                "CPU/CPU case mean is 150");
            chk(cpu->elapsed.sampleCount == 2, "CPU/CPU still has its own 2 mode elapsed samples");
        }
        if (autoMode != nullptr) {
            chk(autoMode->caseElapsed.sampleCount == 1,
                "AUTO/CPU covers only the case it actually ran in");
            chk(autoMode->caseElapsed.meanMs.has_value() &&
                    std::abs(*autoMode->caseElapsed.meanMs - 100.0) < kEps,
                "AUTO/CPU case mean is the single case it ran");
            chk(autoMode->caseElapsed.level == msf::MetricLevel::CaseElapsed,
                "mode case metric is labelled CaseElapsed, not ModeElapsed");
        }
        // The case total is not split between modes.
        chk(cpu && autoMode && cpu->caseElapsed.sampleCount != autoMode->caseElapsed.sampleCount,
            "the case total is filtered onto modes, never divided between them");
        // Overlapping populations are visible rather than implied.
        chk(cpu && autoMode && cpu->caseAccounting.observed == 2 &&
                autoMode->caseAccounting.observed == 1,
            "each mode cohort states its own observed count, so the overlap is visible");
    }

    // --- a skipped mode contributes no case elapsed -------------------------
    {
        msf::NormalizedBenchmarkData d;
        const auto ok = [](double v) { return std::optional<double>{v}; };
        std::vector<msf::IngestCase> cs;
        cs.push_back(makeCase("c1", msf::BenchmarkStatus::Success, 10.0,
                              {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                        msf::BenchmarkStatus::Success, ok(10.0)),
                               makeMode(msf::GpuBackendKind::Cuda, msf::GpuBackendKind::Cpu,
                                        msf::BenchmarkStatus::Skipped, std::nullopt)}));
        d.runs.push_back(makeRun("r", "j", "fp-1", "images", "1.0.0", msf::GitCommitState::Known,
                                 "commitA", msf::BenchmarkStatus::Success, ok(1000.0), cs));
        const auto a = msf::aggregateBenchmarks(d, msf::groupBenchmarks(d));
        const auto* sc = findScope(a, "fp-1", "images");
        const auto* b = findBuild(sc, "1.0.0", msf::GitCommitState::Known);
        const auto* cuda = findMode(b, msf::GpuBackendKind::Cuda, msf::GpuBackendKind::Cpu);
        const auto* cpu = findMode(b, msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu);
        chk(cuda != nullptr && cuda->caseElapsed.sampleCount == 0,
            "the skipped CUDA cohort has no case elapsed sample at all");
        chk(cuda && cuda->caseAccounting.observed == 1 && cuda->caseAccounting.excluded == 1 &&
                exclusionCount(cuda->caseAccounting, msf::SampleExclusion::Skipped) == 1,
            "the skipped case is observed and excluded as skipped, with its reason kept");
        chk(cuda && !cuda->caseElapsed.meanMs.has_value(),
            "no mean is produced for a population with no eligible case");
        chk(cpu != nullptr && cpu->caseElapsed.sampleCount == 1,
            "the CPU cohort still has its own case sample");
    }

    // --- runId collision keeps both journals' case samples ------------------
    {
        msf::NormalizedBenchmarkData d;
        const auto ok = [](double v) { return std::optional<double>{v}; };
        auto mk = [&](const std::string& journal, double caseMs, int n) {
            std::vector<msf::IngestCase> cs;
            for (int i = 0; i < n; ++i)
                cs.push_back(makeCase("c" + std::to_string(i), msf::BenchmarkStatus::Success, caseMs,
                                      {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                                msf::BenchmarkStatus::Success, ok(caseMs))}));
            return makeRun("same-run", journal, "fp-1", "images", "1.0.0",
                           msf::GitCommitState::Known, "commitA", msf::BenchmarkStatus::Success,
                           ok(1000.0), cs);
        };
        d.runs.push_back(mk("j-A", 100.0, 2));
        d.runs.push_back(mk("j-B", 200.0, 3));
        const auto a = msf::aggregateBenchmarks(d, msf::groupBenchmarks(d));
        const auto* sc = findScope(a, "fp-1", "images");
        chk(sc != nullptr && sc->caseElapsed.sampleCount == 5,
            "both journals' cases are counted even though they share one runId");
        chk(sc && sc->caseAccounting.observed == 5 && sc->caseAccounting.eligible == 5,
            "the shared runId does not drop or merge case observations");
        const auto* b = findBuild(sc, "1.0.0", msf::GitCommitState::Known);
        chk(b != nullptr && b->caseElapsed.sampleCount == 5,
            "the build level also sees all 5, so the runId collision costs nothing on either axis");
    }

    // --- determinism --------------------------------------------------------
    {
        const auto d = statusDataset();
        const auto a1 = msf::aggregateBenchmarks(d, msf::groupBenchmarks(d));
        const auto a2 = msf::aggregateBenchmarks(d, msf::groupBenchmarks(d));
        bool same = a1.datasets.size() == a2.datasets.size() &&
                    a1.cases.size() == a2.cases.size() &&
                    a1.modePopulation.observed == a2.modePopulation.observed &&
                    a1.modePopulation.eligible == a2.modePopulation.eligible &&
                    a1.modePopulation.excluded == a2.modePopulation.excluded &&
                    a1.modePopulation.exclusions.size() == a2.modePopulation.exclusions.size();
        for (std::size_t i = 0; same && i < a1.datasets.size(); ++i) {
            same = a1.datasets[i].key == a2.datasets[i].key &&
                   a1.datasets[i].scopes.size() == a2.datasets[i].scopes.size();
            for (std::size_t j = 0; same && j < a1.datasets[i].scopes.size(); ++j) {
                const auto& x = a1.datasets[i].scopes[j];
                const auto& y = a2.datasets[i].scopes[j];
                same = x.key == y.key && x.builds.size() == y.builds.size() &&
                       x.caseElapsed.sampleCount == y.caseElapsed.sampleCount;
                if (same && x.caseElapsed.medianMs.has_value() && y.caseElapsed.medianMs.has_value())
                    same = *x.caseElapsed.medianMs == *y.caseElapsed.medianMs;
            }
        }
        for (std::size_t i = 0; same && i < a1.modePopulation.exclusions.size(); ++i)
            same = a1.modePopulation.exclusions[i].reason == a2.modePopulation.exclusions[i].reason &&
                   a1.modePopulation.exclusions[i].count ==
                       a2.modePopulation.exclusions[i].count;
        chk(same, "aggregating the same input twice gives identical results and order");
    }

    // --- rounding rule ------------------------------------------------------
    {
        chk(std::abs(msf::aggregationRoundMs(1.0 / 3.0) - 0.333333) < kEps,
            "reported values round to 6 decimal places");
        chk(msf::aggregationRoundMs(42.0) == 42.0, "exact values pass through unrounded");
    }

    // --- real journal read-only --------------------------------------------
    if (argc > 1) {
        const msf::NormalizedBenchmarkData d = msf::ingestBenchmarks(argv[1]);
        const msf::BenchmarkGrouping g = msf::groupBenchmarks(d);
        const msf::BenchmarkAggregation a = msf::aggregateBenchmarks(d, g);
        const msf::BenchmarkAggregation a2 = msf::aggregateBenchmarks(d, g);

        std::printf("LIVE  runs=%zu accepted=%zu\n", d.runsFound, d.runsAccepted);
        std::printf("LIVE  mode   population: observed=%zu eligible=%zu excluded=%zu balanced=%s\n",
                    a.modePopulation.observed, a.modePopulation.eligible,
                    a.modePopulation.excluded, a.modePopulation.balanced() ? "yes" : "NO");
        for (const auto& e : a.modePopulation.exclusions)
            std::printf("LIVE    excluded %-14s = %zu\n", msf::sampleExclusionName(e.reason), e.count);
        std::printf("LIVE  case   population: observed=%zu eligible=%zu excluded=%zu balanced=%s\n",
                    a.casePopulation.observed, a.casePopulation.eligible, a.casePopulation.excluded,
                    a.casePopulation.balanced() ? "yes" : "NO");
        for (const auto& e : a.casePopulation.exclusions)
            std::printf("LIVE    excluded %-14s = %zu\n", msf::sampleExclusionName(e.reason), e.count);
        std::printf("LIVE  run    population: observed=%zu eligible=%zu excluded=%zu balanced=%s\n",
                    a.runPopulation.observed, a.runPopulation.eligible, a.runPopulation.excluded,
                    a.runPopulation.balanced() ? "yes" : "NO");
        for (const auto& e : a.runPopulation.exclusions)
            std::printf("LIVE    excluded %-14s = %zu\n", msf::sampleExclusionName(e.reason), e.count);
        std::printf("LIVE  outside any dataset scope: runs=%zu cases=%zu modes=%zu\n",
                    a.runsOutsideDatasetScope, a.caseRecordsOutsideDatasetScope,
                    a.modeRecordsOutsideDatasetScope);
        std::printf("LIVE  run identity: acceptedRuns=%zu distinctRunIds=%zu "
                    "runsWithSharedId=%zu\n",
                    d.runsAccepted, a.distinctRunIds, a.runsWithCollidingIdentity);

        std::printf("LIVE  -- scope cohorts (fingerprint / scope / runs / case n / case median / "
                    "run wall n / run wall median) --\n");
        for (const auto& ds : a.datasets)
            for (const auto& sc : ds.scopes) {
                std::printf("  %.12s / %-6s runs=%-3zu caseN=%-4zu caseMedian=%-10.3f wallN=%-3zu "
                            "wallMedian=%-8.1f res=%s\n",
                            sc.key.datasetFingerprint.c_str(), sc.key.mediaScope.c_str(),
                            sc.runAccounting.observed, sc.caseElapsed.sampleCount,
                            sc.caseElapsed.medianMs.value_or(-1.0), sc.runWallDuration.sampleCount,
                            sc.runWallDuration.medianMs.value_or(-1.0),
                            msf::metricResolutionName(sc.runWallDuration.resolution));
            }

        std::printf("LIVE  -- mode cohorts (requested / effective / observed / eligible / "
                    "skipped / n / median) --\n");
        for (const auto& ds : a.datasets)
            for (const auto& sc : ds.scopes)
                for (const auto& b : sc.builds)
                    for (const auto& m : b.modes)
                        std::printf("  %.12s %-6s %-6s %-4s obs=%-4zu elig=%-4zu skip=%-3zu n=%-4zu "
                                    "median=%-9.3f\n",
                                    sc.key.mediaScope.c_str(),
                                    msf::gpuBackendKindName(m.key.requestedMode),
                                    msf::gpuBackendKindName(m.key.effectiveMode),
                                    msf::gitCommitStateName(b.key.gitCommitState),
                                    m.accounting.observed, m.accounting.eligible,
                                    statusCount(m.statuses, msf::BenchmarkStatus::Skipped),
                                    m.elapsed.sampleCount,
                                    m.elapsed.medianMs.value_or(-1.0));

        std::printf("LIVE  -- build cohorts --\n");
        for (const auto& ds : a.datasets)
            for (const auto& sc : ds.scopes)
                for (const auto& b : sc.builds)
                    std::printf("  %-12s / %-6s %-8s %-10s runs=%-3zu comparable=%s\n",
                                sc.key.mediaScope.c_str(), b.key.buildVersion.c_str(),
                                msf::gitCommitStateName(b.key.gitCommitState),
                                b.key.gitCommit.empty() ? "-" : b.key.gitCommit.c_str(),
                                b.runAccounting.observed, b.commitComparable ? "yes" : "no");

        // Raw mode_result lines versus distinct logical entities: the duplicate
        // protection, measured.
        std::size_t logicalModes = 0;
        for (const auto& r : d.runs)
            for (const auto& c : r.cases) logicalModes += c.modes.size();
        std::printf("LIVE  duplicate check: logical mode entities=%zu observed=%zu equal=%s\n",
                    logicalModes, a.modePopulation.observed,
                    logicalModes == a.modePopulation.observed ? "yes" : "NO");

        bool same = a.modePopulation.observed == a2.modePopulation.observed &&
                    a.modePopulation.eligible == a2.modePopulation.eligible &&
                    a.casePopulation.observed == a2.casePopulation.observed &&
                    a.runPopulation.observed == a2.runPopulation.observed &&
                    a.datasets.size() == a2.datasets.size() && a.cases.size() == a2.cases.size();
        for (std::size_t i = 0; same && i < a.datasets.size(); ++i) {
            same = a.datasets[i].key == a2.datasets[i].key &&
                   a.datasets[i].scopes.size() == a2.datasets[i].scopes.size();
            for (std::size_t j = 0; same && j < a.datasets[i].scopes.size(); ++j) {
                const auto& x = a.datasets[i].scopes[j];
                const auto& y = a2.datasets[i].scopes[j];
                same = x.key == y.key && x.builds.size() == y.builds.size() &&
                       x.caseElapsed.sampleCount == y.caseElapsed.sampleCount &&
                       x.runWallDuration.sampleCount == y.runWallDuration.sampleCount;
                for (std::size_t k = 0; same && k < x.builds.size(); ++k) {
                    same = x.builds[k].key == y.builds[k].key &&
                           x.builds[k].modes.size() == y.builds[k].modes.size();
                    for (std::size_t m = 0; same && m < x.builds[k].modes.size(); ++m)
                        same = x.builds[k].modes[m].elapsed.sampleCount ==
                                   y.builds[k].modes[m].elapsed.sampleCount &&
                               x.builds[k].modes[m].accounting.observed ==
                                   y.builds[k].modes[m].accounting.observed;
                }
            }
        }
        std::printf("LIVE  determinism: %s\n", same ? "IDENTICAL" : "DIVERGED");
        const bool ok = same && logicalModes == a.modePopulation.observed &&
                        a.modePopulation.balanced() && a.casePopulation.balanced() &&
                        a.runPopulation.balanced();
        return ok ? 0 : 1;
    }

    std::printf("S6-4 aggregation: %s checks=%d fails=%d\n", gFails == 0 ? "ok" : "FAILED", gChecks,
                gFails);
    return gFails == 0 ? 0 : 1;
}