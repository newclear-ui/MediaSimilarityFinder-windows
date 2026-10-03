#pragma once
// S6 Data-mining: grouping.
//
// SCOPE. Grouping sits directly on top of the S6-2 normalized dataset and answers
// one question: which runs, and which cases, belong to the same comparison
// population? It computes membership, counts and provenance, and nothing else.
//
// It deliberately computes no mean, median, percentile, delta, speedup, failure
// rate or regression figure, and it reaches no verdict about whether two groups
// are actually comparable. Those are later stages, and 짠15 of the S6 brief
// records why: the journal still lacks distance, resourcePolicy and GPU backend,
// no controlled environment has been defined, and repeated runs do not exist yet.
//
// The single most important design rule here is that there is NO one composite
// key. A dataset+scope+build+mode key baked into one string would make
// cross-build comparison impossible, because every run would land in its own
// group. Instead the analysis dimensions are separate typed keys, and a caller
// composes whichever view its question needs.
//
// Recorded in docs/worklog/0.9.4.*.md (S6 Grouping), because getting this wrong
// is easy to repeat: it looks tidier to have one key and quietly destroys every
// cross-build comparison.

// ---------------------------------------------------------------------------
// Typed grouping keys
// ---------------------------------------------------------------------------
//
// These are structs, not concatenated strings, for two reasons: a field boundary
// cannot be lost inside a separator, and a key can carry an explicit "missing"
// state that a string cannot express without inventing a placeholder value.

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "benchmark_core.h"          // GpuBackendKind
#include "benchmark_data_mining.h"   // NormalizedBenchmarkData, GitCommitState

namespace msf {

// The widest population: one measured dataset, across builds and modes.
//
// Only a Valid dataset identity reaches this key. A Missing or Empty identity
// never becomes a cohort member, and is never given a substitute fingerprint.
struct DatasetCohortKey {
    std::string datasetFingerprint;

    bool operator<(const DatasetCohortKey& o) const {
        return datasetFingerprint < o.datasetFingerprint;
    }
    bool operator==(const DatasetCohortKey& o) const {
        return datasetFingerprint == o.datasetFingerprint;
    }
};

// Dataset plus media scope: the baseline for cross-build observation, because two
// runs are only in the same comparison population if they measured the same
// dataset AND the same scope.
//
// "all" is its own scope. It is never decomposed into images+videos, because
// nothing in the journal says how many cases of each the run actually covered.
struct ScopeCohortKey {
    std::string datasetFingerprint;
    std::string mediaScope;

    bool operator<(const ScopeCohortKey& o) const {
        if (datasetFingerprint != o.datasetFingerprint) return datasetFingerprint < o.datasetFingerprint;
        return mediaScope < o.mediaScope;
    }
    bool operator==(const ScopeCohortKey& o) const {
        return datasetFingerprint == o.datasetFingerprint && mediaScope == o.mediaScope;
    }
};

// One build. Carries the provenance STATE as well as the value, so a Known, an
// Unknown and a Legacy run can never land in the same cohort.
struct BuildCohortKey {
    std::string buildVersion;
    GitCommitState gitCommitState = GitCommitState::Legacy;
    std::string gitCommit;

    bool operator<(const BuildCohortKey& o) const {
        if (buildVersion != o.buildVersion) return buildVersion < o.buildVersion;
        if (gitCommitState != o.gitCommitState) return gitCommitState < o.gitCommitState;
        return gitCommit < o.gitCommit;
    }
    bool operator==(const BuildCohortKey& o) const {
        return buildVersion == o.buildVersion &&
               gitCommitState == o.gitCommitState &&
               gitCommit == o.gitCommit;
    }
};

// The canonical order modes are sorted in: AUTO, then CPU, then GPU-MAX.
//
// This is NOT the declaration order of GpuBackendKind, which is Auto, Cuda, Cpu.
// Sorting by the enum would put CUDA between AUTO and CPU, which is not the order
// the mode list is meant to be read in, so the rank is stated explicitly rather
// than inherited from the enum.
//
// This orders DISPLAY and GROUP results only. It does not redefine execution order,
// which S2 and S5 already fixed; a group is sorted, an execution is not re-planned.
inline int modeCanonicalRank(GpuBackendKind k) {
    switch (k) {
        case GpuBackendKind::Auto: return 0;   // AUTO
        case GpuBackendKind::Cpu: return 1;    // CPU
        case GpuBackendKind::Cuda: return 2;   // GPU-MAX
    }
    return 3;
}

// A mode as the run recorded it.
//
// requestedMode and effectiveMode are never merged. AUTO requested with CUDA
// effective is a distinct observation from CPU requested with CPU effective, and
// collapsing them would erase exactly the capability transition later analysis
// needs.
struct ModeCohortKey {
    GpuBackendKind requestedMode = GpuBackendKind::Auto;
    GpuBackendKind effectiveMode = GpuBackendKind::Cpu;

    bool operator<(const ModeCohortKey& o) const {
        const int r = modeCanonicalRank(requestedMode);
        const int ro = modeCanonicalRank(o.requestedMode);
        if (r != ro) return r < ro;
        return modeCanonicalRank(effectiveMode) < modeCanonicalRank(o.effectiveMode);
    }
    bool operator==(const ModeCohortKey& o) const {
        return requestedMode == o.requestedMode && effectiveMode == o.effectiveMode;
    }
};

// One file, across runs.
//
// S2 derives caseId as IndexManager::folderId(file.path), a hash of the
// canonical path, so the same file in two runs of the same dataset yields the
// same caseId (confirmed against real journals). It is therefore stable across
// runs and is usable as a case identity, but it is paired with the dataset
// fingerprint rather than trusted alone: the id is derived from an absolute
// path, and no new hash is invented here.
struct CaseCohortKey {
    std::string datasetFingerprint;
    std::string caseId;

    bool operator<(const CaseCohortKey& o) const {
        if (datasetFingerprint != o.datasetFingerprint) return datasetFingerprint < o.datasetFingerprint;
        return caseId < o.caseId;
    }
    bool operator==(const CaseCohortKey& o) const {
        return datasetFingerprint == o.datasetFingerprint && caseId == o.caseId;
    }
};

// ---------------------------------------------------------------------------
// Cohorts
// ---------------------------------------------------------------------------

struct DatasetCohort {
    DatasetCohortKey key;
    std::vector<std::string> runIds;   // sorted, unique: the group provenance
    std::size_t runCount = 0;
};

struct ScopeCohort {
    ScopeCohortKey key;
    std::vector<std::string> runIds;
    std::size_t runCount = 0;
};

struct BuildCohort {
    BuildCohortKey key;

    // Whether this cohort's provenance supports commit-level comparison.
    //
    // True only for a Known commit. An Unknown cohort groups runs that all lack a
    // commit, which is not evidence that they were the same binary, and a Legacy
    // cohort groups runs written before the field existed. Neither may be treated
    // as a known build. This is a statement about PROVENANCE QUALITY, not a
    // verdict about whether the underlying data is comparable.
    bool commitComparable = false;

    std::vector<std::string> runIds;
    std::size_t runCount = 0;
};

struct ModeCohort {
    ModeCohortKey key;
    // (runId, caseId) pairs, sorted, so membership is traceable back to both the
    // run and the file.
    //
    // This is a SET of distinct memberships, not a count of journal lines. The
    // real benchmark store contains suites written more than once, so one
    // (run, case, mode) can appear in several identical records; each is counted
    // once here. Counting raw lines instead would make membership depend on how
    // many times a suite happened to be written.
    std::vector<std::pair<std::string, std::string>> members;
    std::size_t memberCount = 0;
};

struct CaseCohort {
    CaseCohortKey key;
    std::vector<std::string> runIds;   // the runs this file appeared in
    std::string path;                  // first observed path, for display
    std::size_t runCount = 0;
};

// ---------------------------------------------------------------------------
// The grouping result
// ---------------------------------------------------------------------------
//
// Every vector below is in a defined order so the same input always yields the
// same output. Filesystem enumeration order is never used.
//
// Note what is absent: distance, resourcePolicy and GPU backend. The journal does
// not carry them, and inferring them would be a fabrication, so they are not
// grouping axes at all.
struct BenchmarkGrouping {
    std::vector<DatasetCohort> datasetCohorts;
    std::vector<ScopeCohort> scopeCohorts;
    std::vector<BuildCohort> buildCohorts;
    std::vector<ModeCohort> modeCohorts;
    std::vector<CaseCohort> caseCohorts;

    // Runs considered, which is the accepted set only. Excluded runs are never
    // grouped; they remain in the ingestion result where they can still be
    // inspected.
    std::size_t runsConsidered = 0;

    // Runs left out of a cohort because the key field was not available. These
    // are counted, never silently dropped and never substituted.
    std::size_t runsWithoutDatasetIdentity = 0;   // Missing or Empty
    std::size_t runsWithoutMediaScope = 0;
    std::size_t runsWithoutBuildVersion = 0;

    // Provenance distribution over the considered runs.
    std::size_t provenanceKnown = 0;
    std::size_t provenanceUnknown = 0;
    std::size_t provenanceLegacy = 0;

    // Cases skipped from a cohort because their run had no usable dataset
    // identity. Counted rather than dropped.
    std::size_t casesWithoutDatasetIdentity = 0;
};

// Groups an already-normalized dataset.
//
// Grouping is layered on ingestion/normalization and does not re-read any
// journal, re-derive any recovery outcome, or recompute any normalized field.
BenchmarkGrouping groupBenchmarks(const NormalizedBenchmarkData& data);

}  // namespace msf