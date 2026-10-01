// S6-3 benchmark grouping.
//
// S6-2 proved the normalized records are honest about what they carry. This stage
// answers the next, narrower question: which runs and cases belong to the same
// comparison population?
//
// The checks concentrate on the ways grouping can quietly produce a wrong
// comparison, because that is the failure this stage exists to prevent:
//
//   * a single composite key would put every run in its own group and make
//     cross-build comparison impossible, so the dimensions are separate views;
//   * "all" is a distinct media scope, not images plus videos;
//   * an Unknown or Legacy commit must not be treated as a known build, and must
//     never be back-filled with the current HEAD;
//   * AUTO requested with CUDA effective is a different observation from CPU
//     requested, and the two must stay apart;
//   * a missing or empty dataset fingerprint must not become a cohort;
//   * two runs sharing one journal must not mix their cases;
//   * no statistic and no comparability verdict may leak in.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "benchmark_core.h"
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
//
// These construct NormalizedBenchmarkData directly rather than writing journals.
// Grouping sits above ingestion, so requiring a journal here would make this test
// depend on a layer it is not testing, and would obscure the contract it is
// actually about.

msf::IngestCase makeCase(const std::string& caseId, const std::string& path,
                         msf::GpuBackendKind requested, msf::GpuBackendKind effective) {
    msf::IngestCase c;
    c.caseId = caseId;
    c.path = path;
    c.media = msf::MediaKind::Image;
    c.status = msf::BenchmarkStatus::Success;
    msf::IngestModeResult m;
    m.requestedMode = requested;
    m.effectiveMode = effective;
    m.started = true;
    m.completed = true;
    m.elapsedMs = 1.0;
    c.modes.push_back(m);
    return c;
}

msf::IngestRun makeRun(const std::string& runId, const std::string& journal,
                       const std::string& fp, msf::DatasetIdentityState identity,
                       const std::string& scope, const std::string& build,
                       msf::GitCommitState gstate, const std::string& git) {
    msf::IngestRun r;
    r.sourceJournalPath = journal;
    r.runId = runId;
    r.suiteId = journal;
    r.datasetFingerprint = identity == msf::DatasetIdentityState::Missing
                              ? std::optional<std::string>{}
                              : std::optional<std::string>{fp};
    r.datasetIdentity = identity;
    if (!build.empty()) r.buildVersion = build;
    if (gstate == msf::GitCommitState::Known) r.gitCommit = git;
    r.gitCommitState = gstate;
    if (!scope.empty()) r.mediaScope = scope;
    r.runClass = msf::IngestRunClass::Complete;
    return r;
}

const msf::DatasetCohort* findDataset(const msf::BenchmarkGrouping& g, const std::string& fp) {
    for (const auto& c : g.datasetCohorts)
        if (c.key.datasetFingerprint == fp) return &c;
    return nullptr;
}

const msf::ScopeCohort* findScope(const msf::BenchmarkGrouping& g, const std::string& fp,
                                  const std::string& scope) {
    for (const auto& c : g.scopeCohorts)
        if (c.key.datasetFingerprint == fp && c.key.mediaScope == scope) return &c;
    return nullptr;
}

const msf::BuildCohort* findBuild(const msf::BenchmarkGrouping& g, const std::string& build,
                                  msf::GitCommitState st, const std::string& git) {
    for (const auto& c : g.buildCohorts)
        if (c.key.buildVersion == build && c.key.gitCommitState == st && c.key.gitCommit == git)
            return &c;
    return nullptr;
}

const msf::ModeCohort* findMode(const msf::BenchmarkGrouping& g, msf::GpuBackendKind req,
                                msf::GpuBackendKind eff) {
    for (const auto& c : g.modeCohorts)
        if (c.key.requestedMode == req && c.key.effectiveMode == eff) return &c;
    return nullptr;
}

bool cohortHasRun(const std::vector<std::string>& runIds, const std::string& runId) {
    return std::find(runIds.begin(), runIds.end(), runId) != runIds.end();
}

// --- the dataset under test -------------------------------------------------
//
// The five cohorts from the S6-3 brief, plus the edge cases.
//
//   dataset A / images / commit X        run-A-img-x
//   dataset A / images / commit Y        run-A-img-y      <- cross-build, same cohort
//   dataset A / videos / commit X        run-A-vid-x      <- same dataset, other scope
//   dataset B / images / commit X        run-B-img-x      <- other dataset
//   dataset A / images / legacy git      run-A-legacy
//   dataset A / images / unknown git     run-A-unknown
//   dataset A / scope all / commit X     run-A-all        <- "all" is its own scope
//   dataset A / no fingerprint (empty)   run-A-empty
//   dataset A / fingerprint absent       run-A-missing
//   dataset B / images / commit X, no build version   run-B-nobuild

msf::NormalizedBenchmarkData makeDataset() {
    msf::NormalizedBenchmarkData d;

    auto aImgX = makeRun("run-A-img-x", "j1", "fp-A", msf::DatasetIdentityState::Valid, "images",
                         "1.0.0", msf::GitCommitState::Known, "commitX");
    aImgX.cases.push_back(makeCase("case-1", "/data/img0.jpg", msf::GpuBackendKind::Cpu,
                                   msf::GpuBackendKind::Cpu));
    aImgX.cases.push_back(makeCase("case-2", "/data/img1.jpg", msf::GpuBackendKind::Cpu,
                                   msf::GpuBackendKind::Cpu));

    auto aImgY = makeRun("run-A-img-y", "j2", "fp-A", msf::DatasetIdentityState::Valid, "images",
                         "2.0.0", msf::GitCommitState::Known, "commitY");
    aImgY.cases.push_back(makeCase("case-1", "/data/img0.jpg", msf::GpuBackendKind::Cpu,
                                   msf::GpuBackendKind::Cpu));
    aImgY.cases.push_back(makeCase("case-2", "/data/img1.jpg", msf::GpuBackendKind::Cpu,
                                   msf::GpuBackendKind::Cpu));

    auto aVidX = makeRun("run-A-vid-x", "j3", "fp-A", msf::DatasetIdentityState::Valid, "videos",
                         "1.0.0", msf::GitCommitState::Known, "commitX");
    aVidX.cases.push_back(makeCase("vid-1", "/data/vid0.mp4", msf::GpuBackendKind::Auto,
                                   msf::GpuBackendKind::Cuda));

    auto aAllX = makeRun("run-A-all", "j4", "fp-A", msf::DatasetIdentityState::Valid, "all",
                         "1.0.0", msf::GitCommitState::Known, "commitX");
    aAllX.cases.push_back(makeCase("case-1", "/data/img0.jpg", msf::GpuBackendKind::Auto,
                                   msf::GpuBackendKind::Cuda));

    auto aLegacy = makeRun("run-A-legacy", "j5", "fp-A", msf::DatasetIdentityState::Valid, "images",
                           "0.9.0", msf::GitCommitState::Legacy, "");
    aLegacy.cases.push_back(makeCase("case-1", "/data/img0.jpg", msf::GpuBackendKind::Cpu,
                                     msf::GpuBackendKind::Cpu));

    auto aUnknown = makeRun("run-A-unknown", "j6", "fp-A", msf::DatasetIdentityState::Valid, "images",
                            "1.1.0", msf::GitCommitState::Unknown, "");
    aUnknown.cases.push_back(makeCase("case-1", "/data/img0.jpg", msf::GpuBackendKind::Cpu,
                                      msf::GpuBackendKind::Cpu));

    auto aEmpty = makeRun("run-A-empty", "j7", "", msf::DatasetIdentityState::Empty, "images",
                          "1.0.0", msf::GitCommitState::Known, "commitX");
    aEmpty.cases.push_back(makeCase("case-9", "/data/other.jpg", msf::GpuBackendKind::Cpu,
                                    msf::GpuBackendKind::Cpu));

    auto aMissing = makeRun("run-A-missing", "j8", "", msf::DatasetIdentityState::Missing, "images",
                            "1.0.0", msf::GitCommitState::Known, "commitX");

    auto bImgX = makeRun("run-B-img-x", "j9", "fp-B", msf::DatasetIdentityState::Valid, "images",
                         "1.0.0", msf::GitCommitState::Known, "commitX");
    bImgX.cases.push_back(makeCase("case-1", "/dataB/img0.jpg", msf::GpuBackendKind::Cpu,
                                   msf::GpuBackendKind::Cpu));

    auto bNoBuild = makeRun("run-B-nobuild", "j10", "fp-B", msf::DatasetIdentityState::Valid,
                            "images", "", msf::GitCommitState::Known, "commitX");

    // Two runs written into ONE journal. They must stay separate all the way
    // through grouping.
    auto multiA = makeRun("run-multi-A", "j-multi", "fp-M", msf::DatasetIdentityState::Valid,
                          "images", "1.0.0", msf::GitCommitState::Known, "commitX");
    multiA.cases.push_back(makeCase("m-1", "/data/m0.jpg", msf::GpuBackendKind::Cpu,
                                    msf::GpuBackendKind::Cpu));
    auto multiB = makeRun("run-multi-B", "j-multi", "fp-M", msf::DatasetIdentityState::Valid,
                          "images", "1.0.0", msf::GitCommitState::Known, "commitX");
    multiB.cases.push_back(makeCase("m-2", "/data/m1.jpg", msf::GpuBackendKind::Cpu,
                                    msf::GpuBackendKind::Cpu));

    for (msf::IngestRun* r : {&aImgX, &aImgY, &aVidX, &aAllX, &aLegacy, &aUnknown, &aEmpty,
                              &aMissing, &bImgX, &bNoBuild, &multiA, &multiB}) {
        d.runs.push_back(*r);
    }

    // An excluded run. It must not be grouped at all.
    msf::IngestRun excluded =
        makeRun("run-excluded", "j11", "fp-A", msf::DatasetIdentityState::Valid, "images", "1.0.0",
                msf::GitCommitState::Known, "commitX");
    d.excludedRuns.push_back(excluded);
    d.runsFound = d.runs.size() + d.excludedRuns.size();
    d.runsAccepted = d.runs.size();
    d.runsExcluded = d.excludedRuns.size();
    return d;
}

}  // namespace

int main(int argc, char** argv) {
    std::printf("S6-3 benchmark grouping\n");

    // §23 of the brief: analyse a real journal tree read-only. Ingestion is
    // delegated to S6-1/S6-2 rather than reimplemented, and grouping is applied to
    // exactly the normalized dataset the unit checks above use.
    if (argc > 1) {
        const msf::NormalizedBenchmarkData d = msf::ingestBenchmarks(argv[1]);
        const msf::BenchmarkGrouping g = msf::groupBenchmarks(d);
        const msf::BenchmarkGrouping g2 = msf::groupBenchmarks(d);

        std::printf("LIVE  journals=%zu unreadable=%zu withoutRuns=%zu\n", d.journalsDiscovered,
                    d.journalsUnreadable, d.journalsWithoutRuns);
        std::printf("LIVE  runs=%zu accepted=%zu excluded=%zu\n", d.runsFound, d.runsAccepted,
                    d.runsExcluded);
        std::printf("LIVE  grouping: considered=%zu dataset=%zu scope=%zu build=%zu mode=%zu case=%zu\n",
                    g.runsConsidered, g.datasetCohorts.size(), g.scopeCohorts.size(),
                    g.buildCohorts.size(), g.modeCohorts.size(), g.caseCohorts.size());
        std::printf("LIVE  not grouped: noDatasetIdentity=%zu noMediaScope=%zu noBuildVersion=%zu "
                    "casesWithoutDataset=%zu\n",
                    g.runsWithoutDatasetIdentity, g.runsWithoutMediaScope, g.runsWithoutBuildVersion,
                    g.casesWithoutDatasetIdentity);
        std::printf("LIVE  provenance: known=%zu unknown=%zu legacy=%zu\n", g.provenanceKnown,
                    g.provenanceUnknown, g.provenanceLegacy);
        std::printf("LIVE  -- dataset cohorts --\n");
        for (const auto& c : g.datasetCohorts)
            std::printf("  %-14s runs=%-3zu commitComparableBuilds=n/a\n",
                        c.key.datasetFingerprint.c_str(), c.runCount);
        std::printf("LIVE  -- scope cohorts (fingerprint / scope / runs) --\n");
        for (const auto& c : g.scopeCohorts)
            std::printf("  %-14s / %-8s runs=%zu\n", c.key.datasetFingerprint.c_str(),
                        c.key.mediaScope.c_str(), c.runCount);
        std::printf("LIVE  -- build cohorts (version / provenance / runs / commitComparable) --\n");
        for (const auto& c : g.buildCohorts)
            std::printf("  %-10s %-8s %-10s runs=%-3zu comparable=%s\n", c.key.buildVersion.c_str(),
                        msf::gitCommitStateName(c.key.gitCommitState),
                        c.key.gitCommit.empty() ? "-" : c.key.gitCommit.c_str(), c.runCount,
                        c.commitComparable ? "yes" : "no");
        std::printf("LIVE  -- mode cohorts (requested / effective / members) --\n");
        for (const auto& c : g.modeCohorts)
            std::printf("  %-6s / %-6s members=%zu\n", msf::gpuBackendKindName(c.key.requestedMode),
                        msf::gpuBackendKindName(c.key.effectiveMode), c.memberCount);
        std::printf("LIVE  -- case cohorts: %zu (runCount>1 means the same file recurred across runs)\n",
                    g.caseCohorts.size());
        std::size_t recurring = 0, maxRuns = 0;
        for (const auto& c : g.caseCohorts) {
            if (c.runCount > 1) ++recurring;
            if (c.runCount > maxRuns) maxRuns = c.runCount;
        }
        std::printf("LIVE  case cohorts spanning >1 run=%zu maxRunsPerCase=%zu\n", recurring, maxRuns);

        // §13 deterministic repeat, on real data.
        bool same = g.datasetCohorts.size() == g2.datasetCohorts.size() &&
                    g.scopeCohorts.size() == g2.scopeCohorts.size() &&
                    g.buildCohorts.size() == g2.buildCohorts.size() &&
                    g.modeCohorts.size() == g2.modeCohorts.size() &&
                    g.caseCohorts.size() == g2.caseCohorts.size();
        for (std::size_t i = 0; same && i < g.buildCohorts.size(); ++i)
            same = g.buildCohorts[i].key == g2.buildCohorts[i].key &&
                   g.buildCohorts[i].runIds == g2.buildCohorts[i].runIds;
        std::printf("LIVE  determinism: %s\n", same ? "IDENTICAL" : "DIVERGED");
        return same ? 0 : 1;
    }
    const msf::NormalizedBenchmarkData data = makeDataset();
    const msf::BenchmarkGrouping g = msf::groupBenchmarks(data);

    // --- only the accepted runs are considered ------------------------------
    chk(g.runsConsidered == data.runs.size(), "grouping considers the accepted runs only");
    chk(g.runsConsidered == 12, "accepted run count is 12");

    // --- dataset cohorts ----------------------------------------------------
    {
        const auto* a = findDataset(g, "fp-A");
        const auto* b = findDataset(g, "fp-B");
        const auto* m = findDataset(g, "fp-M");
        chk(a != nullptr, "dataset A forms a cohort");
        chk(b != nullptr, "dataset B forms a cohort");
        chk(m != nullptr, "dataset M forms a cohort");
        chk(g.datasetCohorts.size() == 3, "one cohort per distinct fingerprint (3)");
        chk(a && a->runCount == 6, "dataset A holds 6 runs across builds, scopes and provenance");
        chk(a && cohortHasRun(a->runIds, "run-A-img-x") && cohortHasRun(a->runIds, "run-A-img-y"),
            "dataset A groups multiple runs");
        chk(b && b->runCount == 2, "dataset B holds its own 2 runs");
        chk(a != b, "different fingerprints never share a cohort");
    }

    // --- empty and missing fingerprints are not cohorts ---------------------
    {
        chk(findDataset(g, "") == nullptr, "an empty fingerprint never becomes a cohort");
        chk(g.runsWithoutDatasetIdentity == 2,
            "runs with an empty or missing identity are counted, not dropped silently");
        chk(g.runsWithoutDatasetIdentity == 2 &&
                g.casesWithoutDatasetIdentity == 1,
            "cases of an unusable-identity run are counted rather than attached elsewhere");
    }

    // --- media scope --------------------------------------------------------
    {
        const auto* img = findScope(g, "fp-A", "images");
        const auto* vid = findScope(g, "fp-A", "videos");
        const auto* all = findScope(g, "fp-A", "all");
        chk(img != nullptr && vid != nullptr && all != nullptr,
            "images, videos and all each form their own scope cohort");
        chk(img && vid && img != vid, "images and videos of one dataset are different cohorts");
        chk(all && img && all != img, "\"all\" is not folded into images");
        chk(all && vid && all != vid, "\"all\" is not folded into videos");
        chk(img && img->runCount == 4, "images cohort holds 4 runs (incl. legacy and unknown)");
        chk(vid && vid->runCount == 1, "videos cohort holds 1 run");
    }

    // --- cross-build: same dataset and scope, different builds --------------
    {
        const auto* cohort = findScope(g, "fp-A", "images");
        chk(cohort && cohortHasRun(cohort->runIds, "run-A-img-x") &&
                cohortHasRun(cohort->runIds, "run-A-img-y"),
            "two different commits coexist in one dataset/scope cohort");
        chk(cohort && cohortHasRun(cohort->runIds, "run-A-legacy") &&
                cohortHasRun(cohort->runIds, "run-A-unknown"),
            "legacy and unknown provenance appear in the cohort but as their own builds");
    }

    // --- same build grouping ------------------------------------------------
    {
        const auto* b1x = findBuild(g, "1.0.0", msf::GitCommitState::Known, "commitX");
        chk(b1x != nullptr, "build 1.0.0 at commit X forms a build cohort");
        chk(b1x && b1x->runCount == 8,
            "that build cohort holds all 8 runs built at 1.0.0/commitX, including the two whose "
            "dataset identity is unusable, since \"which build ran this\" does not depend on the "
            "dataset");
        chk(b1x && b1x->commitComparable, "a Known commit cohort is commit-comparable");
        const auto* b2y = findBuild(g, "2.0.0", msf::GitCommitState::Known, "commitY");
        chk(b2y != nullptr && b2y != b1x,
            "the same version at a different commit is a different build cohort");
    }

    // --- provenance is never merged or back-filled --------------------------
    {
        const auto* legacy = findBuild(g, "0.9.0", msf::GitCommitState::Legacy, "");
        const auto* unknown = findBuild(g, "1.1.0", msf::GitCommitState::Unknown, "");
        chk(legacy != nullptr, "a Legacy run forms its own build cohort");
        chk(unknown != nullptr, "an Unknown run forms its own build cohort");
        chk(legacy && unknown && legacy != unknown,
            "Legacy and Unknown never share a build cohort");
        chk(legacy && legacy->key.gitCommit.empty(),
            "a Legacy build key carries no commit value");
        chk(legacy && !legacy->commitComparable,
            "a Legacy cohort is not commit-comparable");
        chk(unknown && !unknown->commitComparable,
            "an Unknown cohort is not commit-comparable, since sharing a missing commit is not "
            "evidence of a shared binary");
        for (const auto& c : g.buildCohorts) {
            if (c.key.gitCommitState != msf::GitCommitState::Known)
                chk(c.key.gitCommit.empty(), "only a Known cohort carries a commit value");
        }
    }

    // --- a build with no version is counted, not invented -------------------
    chk(g.runsWithoutBuildVersion == 1, "a run without a build version is counted separately");

    // --- provenance distribution --------------------------------------------
    chk(g.provenanceKnown == 10 && g.provenanceUnknown == 1 && g.provenanceLegacy == 1,
        "provenance distribution is known=10 unknown=1 legacy=1");

    // --- mode grouping: requested and effective stay distinct ---------------
    {
        const auto* cpu = findMode(g, msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu);
        const auto* autoCuda = findMode(g, msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cuda);
        chk(cpu != nullptr, "CPU requested + CPU effective forms a mode cohort");
        chk(autoCuda != nullptr, "AUTO requested + CUDA effective forms a mode cohort");
        chk(cpu && autoCuda && cpu != autoCuda,
            "AUTO requested with CUDA effective is not merged into CPU requested");
        chk(findMode(g, msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cuda) == nullptr,
            "CPU requested with CUDA effective does not exist in this dataset");
        chk(autoCuda && autoCuda->memberCount == 2,
            "the AUTO/CUDA cohort has both of its (run, case) members");
    }

    // --- canonical mode ordering is AUTO, CPU, GPU-MAX ----------------------
    {
        bool ok = true;
        for (std::size_t i = 1; i < g.modeCohorts.size(); ++i) {
            if (!(g.modeCohorts[i - 1].key < g.modeCohorts[i].key)) ok = false;
        }
        chk(ok && g.modeCohorts.size() == 2, "mode cohorts are strictly ordered");
        chk(g.modeCohorts.front().key.requestedMode == msf::GpuBackendKind::Auto,
            "AUTO sorts before CPU, which is not the enum's declaration order");
    }

    // --- case cohorts -------------------------------------------------------
    {
        std::size_t sameCaseRuns = 0;
        const msf::CaseCohort* case1 = nullptr;
        for (const auto& c : g.caseCohorts) {
            if (c.key.datasetFingerprint == "fp-A" && c.key.caseId == "case-1") case1 = &c;
        }
        chk(case1 != nullptr, "a case appears as a cohort of its own");
        if (case1) {
            sameCaseRuns = case1->runCount;
            chk(cohortHasRun(case1->runIds, "run-A-img-x") && cohortHasRun(case1->runIds, "run-A-img-y"),
                "the same caseId in two runs is one case cohort");
            chk(!cohortHasRun(case1->runIds, "run-A-vid-x"),
                "a run with different cases does not join that cohort");
            chk(case1->path == "/data/img0.jpg", "the case cohort keeps a path for display");
        }
        chk(sameCaseRuns == 5, "case-1 was observed in 5 runs of dataset A");

        bool datasetScoped = true;
        for (const auto& c : g.caseCohorts)
            if (c.key.datasetFingerprint.empty()) datasetScoped = false;
        chk(datasetScoped, "every case cohort is scoped to a dataset fingerprint");
    }

    // --- multi-run in one journal -------------------------------------------
    {
        const auto* m = findDataset(g, "fp-M");
        chk(m && m->runCount == 2, "both runs of one journal form one dataset cohort");
        chk(m && !cohortHasRun(m->runIds, "run-excluded"), "no excluded run leaks into a cohort");

        bool separated = true;
        for (const auto& c : g.caseCohorts) {
            if (c.key.datasetFingerprint != "fp-M") continue;
            if (c.runCount != 1) separated = false;   // m-1 and m-2 must not merge
        }
        chk(separated, "cases of the two runs sharing a journal stay in separate cohorts");
    }

    // --- excluded runs are never grouped ------------------------------------
    {
        bool leaked = false;
        for (const auto& c : g.datasetCohorts) if (cohortHasRun(c.runIds, "run-excluded")) leaked = true;
        for (const auto& c : g.scopeCohorts) if (cohortHasRun(c.runIds, "run-excluded")) leaked = true;
        for (const auto& c : g.buildCohorts) if (cohortHasRun(c.runIds, "run-excluded")) leaked = true;
        for (const auto& c : g.modeCohorts)
            for (const auto& mm : c.members) if (mm.first == "run-excluded") leaked = true;
        chk(!leaked, "an excluded run appears in no cohort at all");
    }

    // --- unavailable axes are not grouping keys ------------------------------
    {
        // distance, resourcePolicy and gpuBackend are absent on every run. If any
        // grouping key had absorbed one of them, these counts would differ.
        bool allAbsent = true;
        for (const auto& r : data.runs) {
            if (r.distance.has_value() || r.resourcePolicy.has_value() || r.gpuBackend.has_value())
                allAbsent = false;
        }
        chk(allAbsent, "distance, resourcePolicy and gpuBackend stay absent on every run");
        chk(g.datasetCohorts.size() == 3 && g.scopeCohorts.size() == 5,
            "no cohort was split by an unavailable axis");
    }

    // --- no statistics leak in ----------------------------------------------
    {
        // Grouping must not have computed anything from the measurements. The
        // build cohorts still describe only membership.
        std::size_t memberRuns = 0;
        for (const auto& c : g.buildCohorts) memberRuns += c.runCount;
        chk(memberRuns == 11, "build cohorts account for every run that had a build version");
        chk(g.modeCohorts.front().memberCount == 2, "mode cohort counts membership, not a statistic");
    }

    // --- determinism --------------------------------------------------------
    {
        const msf::BenchmarkGrouping g2 = msf::groupBenchmarks(data);
        bool same = g2.datasetCohorts.size() == g.datasetCohorts.size() &&
                    g2.scopeCohorts.size() == g.scopeCohorts.size() &&
                    g2.buildCohorts.size() == g.buildCohorts.size() &&
                    g2.modeCohorts.size() == g.modeCohorts.size() &&
                    g2.caseCohorts.size() == g.caseCohorts.size();
        for (std::size_t i = 0; same && i < g.datasetCohorts.size(); ++i)
            same = g.datasetCohorts[i].key == g2.datasetCohorts[i].key &&
                   g.datasetCohorts[i].runIds == g2.datasetCohorts[i].runIds;
        for (std::size_t i = 0; same && i < g.buildCohorts.size(); ++i)
            same = g.buildCohorts[i].key == g2.buildCohorts[i].key &&
                   g.buildCohorts[i].runIds == g2.buildCohorts[i].runIds;
        for (std::size_t i = 0; same && i < g.modeCohorts.size(); ++i)
            same = g.modeCohorts[i].key == g2.modeCohorts[i].key &&
                   g.modeCohorts[i].members == g2.modeCohorts[i].members;
        for (std::size_t i = 0; same && i < g.caseCohorts.size(); ++i)
            same = g.caseCohorts[i].key == g2.caseCohorts[i].key &&
                   g.caseCohorts[i].runIds == g2.caseCohorts[i].runIds;
        chk(same, "grouping the same input twice yields identical cohorts, order and membership");
    }

    // --- empty dataset ------------------------------------------------------
    {
        const msf::NormalizedBenchmarkData empty;
        const msf::BenchmarkGrouping ge = msf::groupBenchmarks(empty);
        chk(ge.runsConsidered == 0 && ge.datasetCohorts.empty() && ge.scopeCohorts.empty() &&
                ge.buildCohorts.empty() && ge.modeCohorts.empty() && ge.caseCohorts.empty(),
            "an empty dataset yields no cohorts and no invented ones");
    }

    std::printf("S6-3 grouping: %s checks=%d fails=%d\n", gFails == 0 ? "ok" : "FAILED", gChecks, gFails);
    return gFails == 0 ? 0 : 1;
}