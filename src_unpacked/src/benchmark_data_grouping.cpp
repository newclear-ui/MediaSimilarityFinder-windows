#include "benchmark_data_grouping.h"

#include <algorithm>
#include <map>
#include <set>

namespace msf {

namespace {

// A run joins a dataset-keyed cohort only when the identity is genuinely a
// measurement of something. Missing means the field was absent; Empty means the
// writer measured and could not fingerprint the source. Neither is a dataset, and
// neither is ever given a substitute value to make it groupable.
bool hasUsableDataset(const IngestRun& run) {
    return run.datasetIdentity == DatasetIdentityState::Valid && run.datasetFingerprint.has_value() &&
           !run.datasetFingerprint->empty();
}

}  // namespace

BenchmarkGrouping groupBenchmarks(const NormalizedBenchmarkData& data) {
    BenchmarkGrouping g;
    g.runsConsidered = data.runs.size();

    // Only the accepted runs are grouped. An excluded run is deliberately absent
    // here: it is retained in the ingestion result, where it can be inspected, but
    // it must never dilute a cohort that is meant to be analysable.
    std::map<DatasetCohortKey, std::set<std::string>> datasets;
    std::map<ScopeCohortKey, std::set<std::string>> scopes;
    std::map<BuildCohortKey, std::set<std::string>> builds;
    std::map<ModeCohortKey, std::set<std::pair<std::string, std::string>>> modes;
    std::map<CaseCohortKey, std::set<std::string>> cases;
    std::map<CaseCohortKey, std::string> casePaths;

    for (const IngestRun& run : data.runs) {
        switch (run.gitCommitState) {
            case GitCommitState::Known: g.provenanceKnown++; break;
            case GitCommitState::Unknown: g.provenanceUnknown++; break;
            case GitCommitState::Legacy: g.provenanceLegacy++; break;
        }

        // --- build cohort ----------------------------------------------------
        //
        // Reached regardless of dataset identity: "which build produced this" is a
        // question worth answering even for a run whose dataset is unusable.
        //
        // The provenance state is part of the key, so a Known, an Unknown and a
        // Legacy run can never share a cohort. The commit value is used only when
        // it is actually present; a Legacy run's key carries an empty string
        // because it is genuinely absent, and never the current HEAD.
        if (run.buildVersion.has_value() && !run.buildVersion->empty()) {
            BuildCohortKey bk;
            bk.buildVersion = *run.buildVersion;
            bk.gitCommitState = run.gitCommitState;
            bk.gitCommit = (run.gitCommitState == GitCommitState::Known && run.gitCommit.has_value())
                               ? *run.gitCommit
                               : std::string();
            builds[bk].insert(run.runId);
        } else {
            g.runsWithoutBuildVersion++;
        }

        // --- dataset / scope / mode / case ------------------------------------
        if (!hasUsableDataset(run)) {
            g.runsWithoutDatasetIdentity++;
            // The cases of a run with no usable dataset identity cannot be keyed,
            // so they are counted rather than attached to a cohort they do not
            // belong to.
            g.casesWithoutDatasetIdentity += run.cases.size();
            continue;
        }
        const std::string& fp = *run.datasetFingerprint;

        datasets[DatasetCohortKey{fp}].insert(run.runId);

        // Scope is what makes two runs comparable in the first place, so "all" is
        // kept as its own scope and never folded into images+videos.
        if (run.mediaScope.has_value() && !run.mediaScope->empty()) {
            scopes[ScopeCohortKey{fp, *run.mediaScope}].insert(run.runId);
        } else {
            g.runsWithoutMediaScope++;
        }

        // Cases are keyed by dataset plus caseId. caseId comes from S2 and is a
        // hash of the canonical path, so the same file across two runs of the same
        // dataset shares it; pairing it with the fingerprint keeps a same-id
        // collision from merging two different datasets. Two runs never borrow each
        // other's cases.
        for (const IngestCase& c : run.cases) {
            if (c.caseId.empty()) continue;
            CaseCohortKey ck{fp, c.caseId};
            cases[ck].insert(run.runId);
            if (casePaths.find(ck) == casePaths.end() && !c.path.empty()) casePaths[ck] = c.path;

            for (const IngestModeResult& m : c.modes) {
                modes[ModeCohortKey{m.requestedMode, m.effectiveMode}].insert({run.runId, c.caseId});
            }
        }
    }

    // --- materialise, in key order ------------------------------------------
    //
    // std::map already yields a deterministic order given a deterministic input, so
    // these vectors need no re-sorting and filesystem enumeration order plays no
    // part.

    g.datasetCohorts.reserve(datasets.size());
    for (auto& [k, ids] : datasets) {
        DatasetCohort c;
        c.key = k;
        c.runIds.assign(ids.begin(), ids.end());
        c.runCount = c.runIds.size();
        g.datasetCohorts.push_back(std::move(c));
    }

    g.scopeCohorts.reserve(scopes.size());
    for (auto& [k, ids] : scopes) {
        ScopeCohort c;
        c.key = k;
        c.runIds.assign(ids.begin(), ids.end());
        c.runCount = c.runIds.size();
        g.scopeCohorts.push_back(std::move(c));
    }

    g.buildCohorts.reserve(builds.size());
    for (auto& [k, ids] : builds) {
        BuildCohort c;
        c.key = k;
        // A commit-level comparison is only supported by a Known commit. Unknown
        // means the field was absent for a writer that had it; Legacy means it
        // predates the field. Neither is evidence that the runs share a binary, so
        // the cohort is formed but flagged as not commit-comparable. This describes
        // provenance quality and is not a judgement about the data.
        c.commitComparable = (k.gitCommitState == GitCommitState::Known);
        c.runIds.assign(ids.begin(), ids.end());
        c.runCount = c.runIds.size();
        g.buildCohorts.push_back(std::move(c));
    }

    g.modeCohorts.reserve(modes.size());
    for (auto& [k, members] : modes) {
        ModeCohort c;
        c.key = k;
        c.members.assign(members.begin(), members.end());
        c.memberCount = c.members.size();
        g.modeCohorts.push_back(std::move(c));
    }

    g.caseCohorts.reserve(cases.size());
    for (auto& [k, ids] : cases) {
        CaseCohort c;
        c.key = k;
        c.runIds.assign(ids.begin(), ids.end());
        c.runCount = c.runIds.size();
        auto p = casePaths.find(k);
        c.path = (p == casePaths.end()) ? std::string() : p->second;
        g.caseCohorts.push_back(std::move(c));
    }

    return g;
}

}  // namespace msf