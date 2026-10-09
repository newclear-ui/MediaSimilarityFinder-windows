// Stall watchdog (0.9.4.78): pure, dependency-free stall detector for the
// scan worker thread. Decides DIAGNOSE vs CANCEL from completion activity and
// the current unit of work; the engine supplies state, this unit only decides
// and formats. Deterministic under fake clocks, so it is unit-tested without
// threads or timing.
//
// Policy (documented, see build-history/0.9.4.78):
// - A stall episode opens after `diagnoseMs` with zero completion activity.
// - First fire is always DIAGNOSE (log a snapshot, never act).
// - CANCEL fires only for image-batch stalls on the second consecutive
//   episode: an image batch is bounded work (hundreds of files, seconds), so
//   2x threshold with zero completions is definitively stuck, never merely
//   slow. Video ranges (unbounded per-video cost) and the analyze phase only
//   ever diagnose; cancelling them could abort legitimate long work.
#pragma once
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace msf {

struct StallUnit {
    // "images" (bounded batch), "videos" (unbounded range), "walk", "analyze".
    std::string kind;
    // First few paths of the unit (bounded sample for logs).
    std::vector<std::string> samplePaths;
    std::size_t total = 0;
    std::int64_t startedSteadyMs = 0;
};

// Point-in-time engine state formatted into a WATCHDOG log line.
struct StallState {
    std::string phase;
    std::int64_t stalledMs = 0;
    std::size_t queueDepth = 0;
    bool walkerDone = false;
    // Walker telemetry counters (blocked = backpressured, starved = idle).
    std::size_t walkerBlockedTicks = 0;
    std::size_t walkerStarvedTicks = 0;
    std::size_t scanned = 0;
    std::size_t completed = 0;
    StallUnit unit;
    // Milliseconds the current unit has been running.
    std::int64_t unitElapsedMs = 0;
};

class StallWatchdog {
public:
    enum Verdict { None = 0, Diagnose = 1, Cancel = 2 };

    explicit StallWatchdog(std::int64_t diagnoseMs = 10LL * 60 * 1000)
        : diagnoseMs_(diagnoseMs) {}

    // Any forward movement (completion, skip, streamed match). Resets the
    // stall clock and closes the current episode.
    void noteActivity(std::int64_t nowSteadyMs) {
        lastActivityMs_ = nowSteadyMs;
        ++completions_;
        episodeFired_ = false;
    }

    std::int64_t lastActivityMs() const { return lastActivityMs_; }

    // A new unit of work begins (batch/range entry). Completions during the
    // unit keep the clock fresh through noteActivity.
    void beginUnit(const StallUnit& unit, std::int64_t nowSteadyMs) {
        unit_ = unit;
        unit_.startedSteadyMs = nowSteadyMs;
        completionsAtUnitStart_ = completions_;
        episodeFired_ = false;
    }

    Verdict check(std::int64_t nowSteadyMs) {        if (nowSteadyMs - lastActivityMs_ < diagnoseMs_) return None;
        if (!episodeFired_) {
            episodeFired_ = true;
            lastFireMs_ = nowSteadyMs;
            return Diagnose;
        }
        // Second consecutive silent episode. Cancel only bounded image work;
        // video/analyze stalls keep diagnosing on the same cadence.
        if (unit_.kind == "images" && completions_ == completionsAtUnitStart_) {
            lastFireMs_ = nowSteadyMs;
            return Cancel;
        }
        if (nowSteadyMs - lastFireMs_ >= diagnoseMs_) {
            lastFireMs_ = nowSteadyMs;
            return Diagnose;
        }
        return None;
    }

    static std::string buildReport(const StallState& st, const char* action) {
        std::ostringstream out;
        out << "WATCHDOG action=" << (action ? action : "logged")
            << " stalledMs=" << st.stalledMs
            << " phase=" << st.phase
            << " scanned=" << st.scanned << " completed=" << st.completed
            << " queue=" << st.queueDepth
            << " walker=" << (st.walkerDone ? "done" : "alive")
            << " blocked=" << st.walkerBlockedTicks
            << " starved=" << st.walkerStarvedTicks
            << " unit=" << st.unit.kind
            << " unitElapsedMs=" << st.unitElapsedMs
            << " unitTotal=" << st.unit.total
            << " sample=[";
        for (std::size_t i = 0; i < st.unit.samplePaths.size() && i < 8; ++i) {
            if (i) out << " | ";
            out << st.unit.samplePaths[i];
        }
        out << "]";
        return out.str();
    }

private:
    std::int64_t diagnoseMs_;
    std::int64_t lastActivityMs_ = 0;
    std::int64_t lastFireMs_ = 0;
    std::size_t completions_ = 0;
    std::size_t completionsAtUnitStart_ = 0;
    bool episodeFired_ = false;
    StallUnit unit_;
};

} // namespace msf
