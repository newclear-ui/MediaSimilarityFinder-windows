// S6-5 comparison eligibility and candidate generation.
//
// S6-4 produced statistics. This stage decides which two populations may be compared
// at all and records why the others may not. The checks concentrate on the ways a
// candidate can be created that should not exist:
//
//   * comparing different datasets, scopes, metric levels or mode semantics;
//   * treating a Legacy or Unknown commit as a known build, or Legacy+Legacy as the
//     same binary;
//   * creating a performance candidate out of Skipped records, which would make a
//     machine without GPU support look fast;
//   * identifying a run by runId alone, which silently loses two of the three runs that
//     share one runId in the real store;
//   * emitting a candidate and dropping the fact that distance, resourcePolicy and GPU
//     backend were never recorded.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "benchmark_core.h"
#include "benchmark_data_aggregation.h"
#include "benchmark_data_comparison.h"
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

msf::IngestCase makeCase(const std::string& id, msf::BenchmarkStatus st, double elapsed,
                         const std::vector<msf::IngestModeResult>& modes) {
    msf::IngestCase c;
    c.caseId = id;
    c.path = "/d/" + id;
    c.media = msf::MediaKind::Image;
    c.status = st;
    c.elapsedMs = elapsed;
    c.modes = modes;
    return c;
}

msf::IngestRun makeRun(const std::string& runId, const std::string& journal,
                       const std::string& fp, const std::string& scope,
                       const std::string& build, msf::GitCommitState gs, const std::string& git,
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
    if (!build.empty()) r.buildVersion = build;
    r.gitCommitState = gs;
    if (gs == msf::GitCommitState::Known) r.gitCommit = git;
    r.mediaScope = scope;
    r.runStatus = msf::BenchmarkStatus::Success;
    r.wallDurationMs = wallMs;
    r.runClass = msf::IngestRunClass::Complete;
    r.cases = cases;
    return r;
}

// One run with a single successful CPU case.
msf::IngestRun cpuRun(const std::string& id, const std::string& journal, const std::string& fp,
                     const std::string& scope, const std::string& build, msf::GitCommitState gs,
                     const std::string& git, double wall, double caseMs, int cases = 1) {
    std::vector<msf::IngestCase> cs;
    for (int i = 0; i < cases; ++i)
        cs.push_back(makeCase("c" + std::to_string(i) + id, msf::BenchmarkStatus::Success, caseMs,
                              {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                        msf::BenchmarkStatus::Success,
                                        std::optional<double>{caseMs})}));
    return makeRun(id, journal, fp, scope, build, gs, git, std::optional<double>{wall}, cs);
}

msf::ComparisonAnalysis analyse(const msf::NormalizedBenchmarkData& d) {
    const msf::BenchmarkGrouping g = msf::groupBenchmarks(d);
    return msf::findComparisonCandidates(d, msf::aggregateBenchmarks(d, g));
}

std::size_t rejectionCount(const msf::ComparisonAnalysis& a, msf::ComparisonEligibility e) {
    for (const auto& t : a.rejectionReasons)
        if (t.reason == e) return t.count;
    return 0;
}

msf::ComparisonSide side(std::string fp, std::string scope, msf::MetricLevel lvl,
                         msf::GpuBackendKind req, msf::GpuBackendKind eff, std::string build,
                         msf::GitCommitState gs, std::string git, std::size_t n) {
    msf::ComparisonSide s;
    s.datasetFingerprint = std::move(fp);
    s.mediaScope = std::move(scope);
    s.metricLevel = lvl;
    s.metricResolution = (lvl == msf::MetricLevel::RunWallDuration)
                             ? msf::MetricResolution::OneSecond
                             : msf::MetricResolution::Recorded;
    s.requestedMode = req;
    s.effectiveMode = eff;
    s.buildVersion = std::move(build);
    s.gitCommitState = gs;
    s.gitCommit = std::move(git);
    s.eligibleSamples = n;
    return s;
}

const double kEps = 1e-6;

}  // namespace

int main(int argc, char** argv) {
    std::printf("S6-5 benchmark comparison\n");

    // --- 1. build-vs-build with two known commits is eligible ---------------
    {
        msf::NormalizedBenchmarkData d;
        d.runs.push_back(cpuRun("run-a", "jA", "fp-1", "images", "1.0.0",
                                msf::GitCommitState::Known, "commitA", 1000.0, 10.0, 3));
        d.runs.push_back(cpuRun("run-b", "jB", "fp-1", "images", "2.0.0",
                                msf::GitCommitState::Known, "commitB", 2000.0, 20.0, 4));
        const auto a = analyse(d);
        chk(a.candidates.size() > 0, "two known commits in one dataset/scope/mode produce candidates");
        bool found = false;
        for (const auto& c : a.candidates)
            if (c.dimension == msf::ComparisonDimension::BuildProvenance) found = true;
        chk(found, "a build-provenance candidate exists");
        chk(a.accounted(), "opportunities == eligible + rejected");

        // Population vs population, not a run pairing.
        bool hasWall = false, hasMode = false;
        std::size_t wallN = 0;
        for (const auto& c : a.candidates) {
            if (c.metricLevel == msf::MetricLevel::RunWallDuration) {
                hasWall = true;
                wallN = c.totalSampleCount();
            }
            if (c.metricLevel == msf::MetricLevel::ModeElapsed) hasMode = true;
        }
        chk(hasWall, "a run wall duration candidate exists");
        chk(hasMode, "a mode elapsed candidate exists");
        chk(wallN == 2, "each side carries one run, so the total sample count is 2");
    }

    // --- 2. mismatched dimensions are refused --------------------------------
    {
        const auto A = side("fp-1", "images", msf::MetricLevel::ModeElapsed,
                            msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu, "1.0.0",
                            msf::GitCommitState::Known, "commitA", 5);
        const auto B = side("fp-2", "images", msf::MetricLevel::ModeElapsed,
                            msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu, "2.0.0",
                            msf::GitCommitState::Known, "commitB", 5);
        chk(msf::evaluateComparison(A, B, msf::ComparisonDimension::BuildProvenance, true) ==
                msf::ComparisonEligibility::MismatchedDataset,
            "a different datasetFingerprint is refused");

        const auto C = side("fp-1", "videos", msf::MetricLevel::ModeElapsed,
                            msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu, "1.0.0",
                            msf::GitCommitState::Known, "commitA", 5);
        chk(msf::evaluateComparison(A, C, msf::ComparisonDimension::BuildProvenance, true) ==
                msf::ComparisonEligibility::MismatchedScope,
            "images vs videos is refused");

        const auto D = side("fp-1", "all", msf::MetricLevel::ModeElapsed,
                            msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu, "1.0.0",
                            msf::GitCommitState::Known, "commitA", 5);
        chk(msf::evaluateComparison(A, D, msf::ComparisonDimension::BuildProvenance, true) ==
                msf::ComparisonEligibility::MismatchedScope,
            "images vs all is refused, and all is never decomposed");
        chk(msf::evaluateComparison(D, C, msf::ComparisonDimension::BuildProvenance, true) ==
                msf::ComparisonEligibility::MismatchedScope,
            "all vs videos is refused");

        const auto E = side("fp-1", "images", msf::MetricLevel::CaseElapsed,
                            msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu, "1.0.0",
                            msf::GitCommitState::Known, "commitA", 5);
        chk(msf::evaluateComparison(A, E, msf::ComparisonDimension::BuildProvenance, true) ==
                msf::ComparisonEligibility::MismatchedMetric,
            "mode elapsed vs case elapsed is refused");

        const auto F = side("fp-1", "images", msf::MetricLevel::RunWallDuration,
                            msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu, "1.0.0",
                            msf::GitCommitState::Known, "commitA", 5);
        chk(msf::evaluateComparison(A, F, msf::ComparisonDimension::BuildProvenance, true) ==
                msf::ComparisonEligibility::MismatchedMetric,
            "mode elapsed vs run wall duration is refused, and resolution counts");

        const auto G = side("fp-1", "images", msf::MetricLevel::ModeElapsed,
                            msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu, "2.0.0",
                            msf::GitCommitState::Known, "commitB", 5);
        chk(msf::evaluateComparison(A, G, msf::ComparisonDimension::BuildProvenance, true) ==
                msf::ComparisonEligibility::MismatchedMode,
            "a CPU-requested side is not compared against an AUTO-requested one that resolved "
            "to CPU");

        const auto H = side("", "images", msf::MetricLevel::ModeElapsed,
                            msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu, "1.0.0",
                            msf::GitCommitState::Known, "commitA", 5);
        chk(msf::evaluateComparison(H, B, msf::ComparisonDimension::BuildProvenance, true) ==
                msf::ComparisonEligibility::MismatchedDataset,
            "a missing fingerprint never produces a performance candidate");
    }

    // --- 3. provenance ------------------------------------------------------
    {
        const auto known = side("fp-1", "images", msf::MetricLevel::ModeElapsed,
                                msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu, "1.0.0",
                                msf::GitCommitState::Known, "commitA", 5);
        const auto legacy1 = side("fp-1", "images", msf::MetricLevel::ModeElapsed,
                                  msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu, "1.0.0",
                                  msf::GitCommitState::Legacy, "", 5);
        const auto legacy2 = side("fp-1", "images", msf::MetricLevel::ModeElapsed,
                                  msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu, "2.0.0",
                                  msf::GitCommitState::Legacy, "", 5);
        const auto unknown1 = side("fp-1", "images", msf::MetricLevel::ModeElapsed,
                                   msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu, "1.0.0",
                                   msf::GitCommitState::Unknown, "", 5);
        const auto other = side("fp-1", "images", msf::MetricLevel::ModeElapsed,
                                msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu, "3.0.0",
                                msf::GitCommitState::Known, "commitZ", 5);

        chk(msf::evaluateComparison(known, other, msf::ComparisonDimension::BuildProvenance, true) ==
                msf::ComparisonEligibility::Eligible,
            "two different known commits are eligible");
        chk(msf::evaluateComparison(legacy1, other, msf::ComparisonDimension::BuildProvenance,
                                    true) == msf::ComparisonEligibility::MissingProvenance,
            "a Legacy side cannot join a commit-level comparison");
        chk(msf::evaluateComparison(unknown1, other, msf::ComparisonDimension::BuildProvenance,
                                    true) == msf::ComparisonEligibility::MissingProvenance,
            "an Unknown side cannot join a commit-level comparison");
        chk(msf::evaluateComparison(legacy1, legacy2, msf::ComparisonDimension::BuildProvenance,
                                    true) == msf::ComparisonEligibility::MissingProvenance,
            "Legacy + Legacy is not treated as two known builds, nor as a comparison");
        chk(msf::evaluateComparison(unknown1, unknown1, msf::ComparisonDimension::BuildProvenance,
                                    true) == msf::ComparisonEligibility::SameProvenance,
            "an Unknown side compared with itself is refused as identical provenance");
        chk(legacy1.commitComparable() == false && unknown1.commitComparable() == false &&
                known.commitComparable(),
            "commitComparable is true only for a Known commit");

        // Same buildVersion, different commits: version alone must not decide.
        const auto sameVerOtherCommit = side("fp-1", "images", msf::MetricLevel::ModeElapsed,
                                             msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                             "1.0.0", msf::GitCommitState::Known, "commitZ", 5);
        chk(msf::evaluateComparison(known, sameVerOtherCommit,
                                    msf::ComparisonDimension::BuildProvenance, true) ==
                msf::ComparisonEligibility::Eligible,
            "the same buildVersion with a different known commit is still a comparison candidate");
    }

    // --- 4. no comparable samples ------------------------------------------
    {
        const auto a = side("fp-1", "images", msf::MetricLevel::ModeElapsed,
                            msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu, "1.0.0",
                            msf::GitCommitState::Known, "commitA", 0);
        const auto b = side("fp-1", "images", msf::MetricLevel::ModeElapsed,
                            msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu, "2.0.0",
                            msf::GitCommitState::Known, "commitB", 7);
        chk(msf::evaluateComparison(a, b, msf::ComparisonDimension::BuildProvenance, true) ==
                msf::ComparisonEligibility::NoComparableSamples,
            "an empty side yields no comparison, rather than a 0 that looks fast");
    }

    // --- 5. skipped records never become a performance candidate -------------
    {
        msf::NormalizedBenchmarkData d;
        // Two different known commits whose ONLY mode is the skipped CUDA request.
        auto skippedRun = [](const std::string& id, const std::string& journal,
                             const std::string& build, const std::string& git) {
            std::vector<msf::IngestCase> cs;
            cs.push_back(makeCase("c" + id, msf::BenchmarkStatus::Skipped, 0.0,
                                  {makeMode(msf::GpuBackendKind::Cuda, msf::GpuBackendKind::Cpu,
                                            msf::BenchmarkStatus::Skipped, std::nullopt)}));
            return makeRun(id, journal, "fp-1", "images", build, msf::GitCommitState::Known, git,
                           std::optional<double>{1000.0}, cs);
        };
        d.runs.push_back(skippedRun("run-a", "jA", "1.0.0", "commitA"));
        d.runs.push_back(skippedRun("run-b", "jB", "2.0.0", "commitB"));
        const auto a = analyse(d);
        std::size_t cudaCandidates = 0;
        for (const auto& c : a.candidates)
            if (c.left.requestedMode == msf::GpuBackendKind::Cuda ||
                c.right.requestedMode == msf::GpuBackendKind::Cuda)
                ++cudaCandidates;
        chk(cudaCandidates == 0, "no candidate is built from Skipped CUDA records");
        chk(rejectionCount(a, msf::ComparisonEligibility::NoComparableSamples) > 0,
            "the refusal is recorded as no-comparable-samples");
        chk(a.accounted(), "the accounting still balances");
        for (const auto& c : a.candidates)
            chk(c.left.eligibleSamples > 0 && c.right.eligibleSamples > 0,
                "every emitted candidate has eligible samples on both sides");
    }

    // --- 6. case elapsed IS comparable by build and mode now ----------------
    //
    // Before the aggregation completion this axis did not exist and the comparison was
    // refused as InsufficientData. Two Known commits now produce a real CaseElapsed
    // candidate, which is the point of the completion.
    {
        msf::NormalizedBenchmarkData d;
        d.runs.push_back(cpuRun("run-a", "jA", "fp-1", "images", "1.0.0",
                                msf::GitCommitState::Known, "commitA", 1000.0, 100.0, 2));
        d.runs.push_back(cpuRun("run-b", "jB", "fp-1", "images", "2.0.0",
                                msf::GitCommitState::Known, "commitB", 2000.0, 150.0, 2));
        const auto a = analyse(d);
        std::size_t caseBuild = 0;
        for (const auto& c : a.candidates)
            if (c.metricLevel == msf::MetricLevel::CaseElapsed &&
                c.dimension == msf::ComparisonDimension::BuildProvenance)
                ++caseBuild;
        chk(caseBuild == 1,
            "two Known commits now produce a real CaseElapsed build-provenance candidate");
        chk(a.accounted(), "the accounting still balances");

        for (const auto& c : a.candidates) {
            if (c.metricLevel != msf::MetricLevel::CaseElapsed) continue;
            chk(c.left.eligibleSamples == 2 && c.right.eligibleSamples == 2,
                "the CaseElapsed candidate carries the per-build case sample counts");
            chk(c.metricResolution == msf::MetricResolution::Recorded,
                "CaseElapsed is a Recorded metric, not a one-second one");
        }
    }

    // --- 6b. a Legacy side still blocks a CaseElapsed comparison ------------
    {
        msf::NormalizedBenchmarkData d;
        d.runs.push_back(cpuRun("run-a", "jA", "fp-1", "images", "1.0.0",
                                msf::GitCommitState::Known, "commitA", 1000.0, 100.0, 2));
        d.runs.push_back(cpuRun("run-b", "jB", "fp-1", "images", "0.9.0",
                                msf::GitCommitState::Legacy, "", 2000.0, 150.0, 2));
        const auto a = analyse(d);
        std::size_t caseCandidates = 0;
        for (const auto& c : a.candidates)
            if (c.metricLevel == msf::MetricLevel::CaseElapsed) ++caseCandidates;
        chk(caseCandidates == 0,
            "a Legacy build still produces no CaseElapsed candidate, so the axis existing does "
            "not weaken the provenance rule");
        chk(rejectionCount(a, msf::ComparisonEligibility::MissingProvenance) > 0,
            "the refusal is missing-provenance, not insufficient-data: the axis now exists, the "
            "commit simply is not known");
    }

    // --- 6c. CaseElapsed by mode, with requested/effective kept apart -------
    {
        msf::NormalizedBenchmarkData d;
        std::vector<msf::IngestCase> cs;
        cs.push_back(makeCase("c1", msf::BenchmarkStatus::Success, 10.0,
                              {makeMode(msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu,
                                        msf::BenchmarkStatus::Success,
                                        std::optional<double>{6.0}),
                               makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                        msf::BenchmarkStatus::Success,
                                        std::optional<double>{4.0})}));
        // A second case where only CPU ran, so the two mode populations differ in size.
        cs.push_back(makeCase("c2", msf::BenchmarkStatus::Success, 20.0,
                              {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                        msf::BenchmarkStatus::Success,
                                        std::optional<double>{20.0})}));
        d.runs.push_back(makeRun("run-a", "jA", "fp-1", "images", "1.0.0",
                                 msf::GitCommitState::Known, "commitA",
                                 std::optional<double>{1000.0}, cs));
        const auto a = analyse(d);
        std::size_t caseModeDim = 0;
        for (const auto& c : a.candidates)
            if (c.metricLevel == msf::MetricLevel::CaseElapsed &&
                c.dimension == msf::ComparisonDimension::ModeSemantics)
                ++caseModeDim;
        chk(caseModeDim == 1, "a CaseElapsed mode-semantics candidate is produced");
        for (const auto& c : a.candidates) {
            if (c.metricLevel != msf::MetricLevel::CaseElapsed) continue;
            chk(c.left.requestedMode != c.right.requestedMode,
                "the mode-semantics case candidate separates the two requested modes");
            chk(c.left.eligibleSamples != c.right.eligibleSamples,
                "the per-mode case populations keep their own sizes instead of being equalised");
            chk(c.left.eligibleSamples == 1 && c.right.eligibleSamples == 2,
                "CPU/CPU covers both cases while AUTO/CPU covers only the case it ran in");
        }
        chk(a.accounted(), "the accounting balances");
    }

    // --- 6d. a skipped mode contributes no case elapsed ---------------------
    {
        msf::NormalizedBenchmarkData d;
        std::vector<msf::IngestCase> cs;
        cs.push_back(makeCase("c1", msf::BenchmarkStatus::Success, 10.0,
                              {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                        msf::BenchmarkStatus::Success,
                                        std::optional<double>{10.0}),
                               // The real shape: CUDA requested, CPU effective, skipped.
                               makeMode(msf::GpuBackendKind::Cuda, msf::GpuBackendKind::Cpu,
                                        msf::BenchmarkStatus::Skipped, std::nullopt)}));
        d.runs.push_back(makeRun("run-a", "jA", "fp-1", "images", "1.0.0",
                                 msf::GitCommitState::Known, "commitA",
                                 std::optional<double>{1000.0}, cs));
        const auto a = analyse(d);
        // The CUDA/CPU cohort must have no eligible case sample, so a CUDA-vs-CPU
        // CaseElapsed comparison cannot be built from it.
        std::size_t cudaCaseDim = 0;
        for (const auto& c : a.candidates)
            if (c.metricLevel == msf::MetricLevel::CaseElapsed &&
                (c.left.requestedMode == msf::GpuBackendKind::Cuda ||
                 c.right.requestedMode == msf::GpuBackendKind::Cuda))
                ++cudaCaseDim;
        chk(cudaCaseDim == 0,
            "a Skipped CUDA request never enters a CaseElapsed mode comparison as a sample");
        chk(rejectionCount(a, msf::ComparisonEligibility::NoComparableSamples) > 0,
            "the refusal is recorded as no-comparable-samples");
    }

    // --- 7. mode semantics dimension ---------------------------------------
    {
        msf::NormalizedBenchmarkData d;
        std::vector<msf::IngestCase> cs;
        cs.push_back(makeCase("c1", msf::BenchmarkStatus::Success, 10.0,
                              {makeMode(msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu,
                                        msf::BenchmarkStatus::Success,
                                        std::optional<double>{6.0}),
                               makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                        msf::BenchmarkStatus::Success,
                                        std::optional<double>{4.0})}));
        d.runs.push_back(makeRun("run-a", "jA", "fp-1", "images", "1.0.0",
                                 msf::GitCommitState::Known, "commitA",
                                 std::optional<double>{1000.0}, cs));
        const auto a = analyse(d);
        std::size_t modeDim = 0;
        for (const auto& c : a.candidates)
            if (c.dimension == msf::ComparisonDimension::ModeSemantics) ++modeDim;
        chk(modeDim == 2,
            "AUTO/CPU and CPU/CPU in one build produce two mode-semantics candidates: one "
            "ModeElapsed and one CaseElapsed");
        chk(a.accounted(), "the accounting balances");
        for (const auto& c : a.candidates)
            chk(c.dimension != msf::ComparisonDimension::ModeSemantics ||
                    (c.left.buildVersion == c.right.buildVersion),
                "a mode-semantics candidate never mixes two builds");
    }

    // --- 8. runId collision stays two distinct run references ---------------
    {
        msf::NormalizedBenchmarkData d;
        d.runs.push_back(cpuRun("same-run", "j-ORDER-A", "fp-1", "images", "1.0.0",
                                msf::GitCommitState::Known, "commitA", 1000.0, 10.0, 2));
        d.runs.push_back(cpuRun("same-run", "j-ORDER-B", "fp-1", "images", "1.0.0",
                                msf::GitCommitState::Known, "commitA", 2000.0, 30.0, 2));
        const auto a = analyse(d);
        chk(a.distinctRunIds == 1, "the two runs share one runId");
        chk(a.runsWithCollidingIdentity == 2, "both runs are reported as colliding");
        chk(a.runReferences.size() == 2, "two distinct run references are kept");
        chk(a.runReferences[0].sourceJournalPath == "j-ORDER-A" &&
                a.runReferences[1].sourceJournalPath == "j-ORDER-B",
            "they are told apart by sourceJournalPath, in sorted order");
        for (const auto& c : a.candidates) {
            bool sameJournal = false;
            for (std::size_t i = 0; i < c.left.runReferences.size(); ++i)
                for (std::size_t j = 0; j < c.right.runReferences.size(); ++j)
                    if (c.left.runReferences[i].sourceJournalPath ==
                            c.right.runReferences[j].sourceJournalPath &&
                        c.left.runReferences[i].runId == c.right.runReferences[j].runId)
                        sameJournal = true;
            chk(!sameJournal, "a candidate never lists the same (journal, runId) on both sides");
        }
    }

    // --- 9. limitations travel with every candidate -------------------------
    {
        msf::NormalizedBenchmarkData d;
        d.runs.push_back(cpuRun("run-a", "jA", "fp-1", "images", "1.0.0",
                                msf::GitCommitState::Known, "commitA", 1000.0, 10.0, 2));
        d.runs.push_back(cpuRun("run-b", "jB", "fp-1", "images", "2.0.0",
                                msf::GitCommitState::Known, "commitB", 2000.0, 20.0, 2));
        const auto a = analyse(d);
        chk(!a.candidates.empty(), "candidates exist for the limitation check");
        for (const auto& c : a.candidates) {
            chk(c.limitations.distanceUnavailable, "distance unavailable is recorded");
            chk(c.limitations.resourcePolicyUnavailable, "resourcePolicy unavailable is recorded");
            chk(c.limitations.gpuBackendUnavailable, "gpuBackend unavailable is recorded");
            chk(c.limitations.controlledEnvironmentUndefined,
                "the uncontrolled environment is recorded");
            chk(c.limitations.filesystemCacheUncontrolled, "the OS filesystem cache is recorded");
            chk(c.limitations.processIsolationUncontrolled, "process isolation is recorded");
            chk(c.limitations.repeatedRunsInsufficient, "insufficient repetition is recorded");
            chk(c.limitations.runWallDurationOneSecondResolution,
                "the one-second wall resolution is recorded");
            chk(!c.limitations.empty(), "a candidate is never presented as unconditional");
        }
        chk(a.limitations.cancellationUnobserved,
            "no run_cancelled sample exists, which is recorded");
        chk(a.limitations.count() == 9, "nine limitations are carried");
    }

    // --- 10. empty data ------------------------------------------------------
    {
        const msf::NormalizedBenchmarkData empty;
        const auto a = analyse(empty);
        chk(a.candidates.empty(), "an empty dataset produces no candidates");
        chk(a.comparisonOpportunities == 0, "and no opportunities");
        chk(a.accounted(), "and the accounting still balances");
    }

    // --- 10b. the unusable-dimension guard still reports InsufficientData ----
    //
    // Generation no longer needs this, because the CaseElapsed axes now exist. The state
    // stays part of the contract for a caller judging a combination the result cannot
    // express, so it must keep working rather than being deleted untested.
    {
        const auto x = side("fp-1", "images", msf::MetricLevel::CaseElapsed,
                            msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu, "1.0.0",
                            msf::GitCommitState::Known, "commitA", 5);
        const auto y = side("fp-1", "images", msf::MetricLevel::CaseElapsed,
                            msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu, "2.0.0",
                            msf::GitCommitState::Known, "commitB", 5);
        chk(msf::evaluateComparison(x, y, msf::ComparisonDimension::BuildProvenance, true) ==
                msf::ComparisonEligibility::Eligible,
            "the same pair is now eligible, because the axis exists");
        chk(msf::evaluateComparison(x, y, msf::ComparisonDimension::BuildProvenance, false) ==
                msf::ComparisonEligibility::InsufficientData,
            "declaring the combination unusable reports insufficient-data rather than pretending "
            "to compare");
        chk(msf::evaluateComparison(y, x, msf::ComparisonDimension::BuildProvenance, true) ==
                msf::ComparisonEligibility::Eligible,
            "the verdict does not depend on which side is passed first");
    }

    // --- 11. determinism ------------------------------------------------------
    {
        msf::NormalizedBenchmarkData d;
        d.runs.push_back(cpuRun("run-a", "jA", "fp-1", "images", "1.0.0",
                                msf::GitCommitState::Known, "commitA", 1000.0, 10.0, 2));
        d.runs.push_back(cpuRun("run-b", "jB", "fp-1", "images", "2.0.0",
                                msf::GitCommitState::Known, "commitB", 2000.0, 20.0, 3));
        d.runs.push_back(cpuRun("run-c", "jC", "fp-1", "images", "1.5.0",
                                msf::GitCommitState::Known, "commitC", 1500.0, 15.0, 2));
        const auto a1 = analyse(d);
        const auto a2 = analyse(d);
        bool same = a1.candidates.size() == a2.candidates.size() &&
                    a1.rejections.size() == a2.rejections.size() &&
                    a1.comparisonOpportunities == a2.comparisonOpportunities &&
                    a1.eligibleCandidates == a2.eligibleCandidates &&
                    a1.rejectionReasons.size() == a2.rejectionReasons.size();
        for (std::size_t i = 0; same && i < a1.candidates.size(); ++i) {
            same = msf::comparisonCandidateLess(a1.candidates[i], a2.candidates[i]) ==
                       msf::comparisonCandidateLess(a2.candidates[i], a1.candidates[i]);
            same = same && a1.candidates[i].dimension == a2.candidates[i].dimension &&
                   a1.candidates[i].datasetFingerprint == a2.candidates[i].datasetFingerprint &&
                   a1.candidates[i].mediaScope == a2.candidates[i].mediaScope &&
                   a1.candidates[i].left.buildVersion == a2.candidates[i].left.buildVersion &&
                   a1.candidates[i].left.gitCommit == a2.candidates[i].left.gitCommit &&
                   a1.candidates[i].right.buildVersion == a2.candidates[i].right.buildVersion &&
                   a1.candidates[i].right.gitCommit == a2.candidates[i].right.gitCommit &&
                   a1.candidates[i].left.eligibleSamples ==
                       a2.candidates[i].left.eligibleSamples &&
                   a1.candidates[i].right.eligibleSamples ==
                       a2.candidates[i].right.eligibleSamples;
        }
        for (std::size_t i = 0; same && i < a1.rejectionReasons.size(); ++i)
            same = a1.rejectionReasons[i].reason == a2.rejectionReasons[i].reason &&
                   a1.rejectionReasons[i].count == a2.rejectionReasons[i].count;
        chk(same, "the same input produces identical candidates, order and rejection counts");

        // No pair is emitted twice.
        for (std::size_t i = 0; i < a1.candidates.size(); ++i)
            for (std::size_t j = i + 1; j < a1.candidates.size(); ++j)
                chk(!(a1.candidates[i].left.buildVersion == a1.candidates[j].left.buildVersion &&
                      a1.candidates[i].right.buildVersion == a1.candidates[j].right.buildVersion &&
                      a1.candidates[i].mediaScope == a1.candidates[j].mediaScope &&
                      a1.candidates[i].metricLevel == a1.candidates[j].metricLevel),
                    "no candidate pair is emitted twice");
    }

    // --- 12. a sample count of 1 is a candidate, not a sufficient statistic -
    {
        msf::NormalizedBenchmarkData d;
        d.runs.push_back(cpuRun("run-a", "jA", "fp-1", "images", "1.0.0",
                                msf::GitCommitState::Known, "commitA", 1000.0, 10.0, 1));
        d.runs.push_back(cpuRun("run-b", "jB", "fp-1", "images", "2.0.0",
                                msf::GitCommitState::Known, "commitB", 2000.0, 20.0, 1));
        const auto a = analyse(d);
        chk(!a.candidates.empty(), "an n=1 on each side still yields a candidate");
        bool smallestOne = false;
        for (const auto& c : a.candidates)
            if (c.smallerSideSampleCount() == 1) smallestOne = true;
        chk(smallestOne, "the sample count travels with the candidate so a later stage can "
                        "refuse to over-read it");
        chk(a.limitations.repeatedRunsInsufficient,
            "n=1 is not described as statistically sufficient");
    }

    // --- real journal read-only ---------------------------------------------
    if (argc > 1) {
        const msf::NormalizedBenchmarkData d = msf::ingestBenchmarks(argv[1]);
        const msf::BenchmarkGrouping g = msf::groupBenchmarks(d);
        const msf::BenchmarkAggregation agg = msf::aggregateBenchmarks(d, g);
        const msf::ComparisonAnalysis a = msf::findComparisonCandidates(d, agg);
        const msf::ComparisonAnalysis a2 = msf::findComparisonCandidates(d, agg);

        std::printf("LIVE  runs=%zu accepted=%zu\n", d.runsFound, d.runsAccepted);
        std::printf("LIVE  run identity: distinctRunIds=%zu sharedRunIdRuns=%zu references=%zu\n",
                    a.distinctRunIds, a.runsWithCollidingIdentity, a.runReferences.size());
        std::printf("LIVE  opportunities=%zu eligible=%zu rejected=%zu accounted=%s\n",
                    a.comparisonOpportunities, a.eligibleCandidates, a.rejectedCandidates,
                    a.accounted() ? "yes" : "NO");
        std::printf("LIVE  -- rejection reasons --\n");
        for (const auto& t : a.rejectionReasons)
            std::printf("LIVE    %-24s = %zu\n", msf::comparisonEligibilityName(t.reason), t.count);
        std::printf("LIVE  -- rejections in detail --\n");
        for (const auto& r : a.rejections)
            std::printf("LIVE    %-18s %-10.10s %-6s %-16s L=%s(n=%zu) R=%s(n=%zu)\n",
                        msf::comparisonDimensionName(r.dimension), r.datasetFingerprint.c_str(),
                        r.mediaScope.c_str(), msf::metricLevelName(r.metricLevel),
                        r.leftLabel.c_str(), r.leftSampleCount, r.rightLabel.c_str(),
                        r.rightSampleCount);
        std::printf("LIVE  -- candidates (%zu) --\n", a.candidates.size());
        for (const auto& c : a.candidates)
            std::printf("LIVE    %-18s %-10.10s %-6s %-10s L[%s/%s n=%zu] R[%s/%s n=%zu] "
                        "limitations=%zu\n",
                        msf::comparisonDimensionName(c.dimension), c.datasetFingerprint.c_str(),
                        c.mediaScope.c_str(), msf::metricLevelName(c.metricLevel),
                        c.left.buildVersion.c_str(),
                        msf::gitCommitStateName(c.left.gitCommitState), c.left.eligibleSamples,
                        c.right.buildVersion.c_str(),
                        msf::gitCommitStateName(c.right.gitCommitState), c.right.eligibleSamples,
                        c.limitations.count());

        std::printf("LIVE  -- mode cohorts with eligible samples (why CUDA/AUTO yield nothing) --\n");
        for (const auto& ds : agg.datasets)
            for (const auto& sc : ds.scopes)
                for (const auto& b : sc.builds)
                    for (const auto& m : b.modes)
                        std::printf("LIVE    %-6s %-4s>%-4s %-8s observed=%-4zu eligible=%-4zu\n",
                                    sc.key.mediaScope.c_str(),
                                    msf::gpuBackendKindName(m.key.requestedMode),
                                    msf::gpuBackendKindName(m.key.effectiveMode),
                                    msf::gitCommitStateName(b.key.gitCommitState),
                                    m.accounting.observed, m.accounting.eligible);
        std::printf("LIVE  limitations carried=%zu (distance/resourcePolicy/gpuBackend/controlled "
                    "env/fs cache/process isolation/repeats/1s resolution/cancellation)\n",
                    a.limitations.count());

        bool same = a.candidates.size() == a2.candidates.size() &&
                    a.rejections.size() == a2.rejections.size() &&
                    a.comparisonOpportunities == a2.comparisonOpportunities &&
                    a.eligibleCandidates == a2.eligibleCandidates &&
                    a.rejectionReasons.size() == a2.rejectionReasons.size();
        for (std::size_t i = 0; same && i < a.candidates.size(); ++i)
            same = msf::comparisonCandidateLess(a.candidates[i], a2.candidates[i]) ==
                       msf::comparisonCandidateLess(a2.candidates[i], a.candidates[i]);
        std::printf("LIVE  determinism: %s\n", same ? "IDENTICAL" : "DIVERGED");
        return same && a.accounted() ? 0 : 1;
    }

    std::printf("S6-5 comparison: %s checks=%d fails=%d\n", gFails == 0 ? "ok" : "FAILED", gChecks,
                gFails);
    return gFails == 0 ? 0 : 1;
}