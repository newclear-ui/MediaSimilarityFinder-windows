// StallWatchdog unit test (0.9.4.78). Pure logic under fake clocks: no
// threads, no timing, no backend. Proves the diagnose/cancel policy:
// diagnose first, cancel only bounded image-batch stalls on repeat silence.
#include "stall_watchdog.h"
#include <iostream>

namespace {

int gChecks = 0;
bool gOk = true;

void check(bool cond, const char* what) {
    ++gChecks;
    if (!cond) { gOk = false; std::cerr << "  [FAIL] " << what << std::endl; }
    else       { std::cout << "  [ok] " << what << std::endl; }
}

msf::StallUnit unit(const char* kind) {
    msf::StallUnit u;
    u.kind = kind;
    u.samplePaths = {"a.jpg", "b.jpg"};
    u.total = 2;
    return u;
}

} // namespace

int main() {
    using msf::StallWatchdog;
    // Silence below threshold: nothing fires.
    {
        StallWatchdog w(600000);
        w.noteActivity(0);
        w.beginUnit(unit("images"), 0);
        check(w.check(599999) == StallWatchdog::None, "no fire below threshold");
    }
    // First silent episode: diagnose, for any unit kind.
    {
        StallWatchdog w(600000);
        w.noteActivity(0);
        w.beginUnit(unit("videos"), 0);
        check(w.check(600000) == StallWatchdog::Diagnose, "first silence diagnoses (video)");
    }
    // Activity resets the clock.
    {
        StallWatchdog w(600000);
        w.noteActivity(0);
        w.beginUnit(unit("images"), 0);
        w.noteActivity(300000);
        check(w.check(899999) == StallWatchdog::None, "activity resets the clock");
        check(w.check(900000) == StallWatchdog::Diagnose, "silence measured from last activity");
    }
    // Second consecutive silent episode on an image batch: cancel.
    {
        StallWatchdog w(600000);
        w.noteActivity(0);
        w.beginUnit(unit("images"), 0);
        check(w.check(600000) == StallWatchdog::Diagnose, "first episode diagnoses (images)");
        check(w.check(1200000) == StallWatchdog::Cancel, "second episode cancels image batch");
    }
    // Video/analyze units never cancel, however long the silence.
    {
        StallWatchdog w(600000);
        w.noteActivity(0);
        w.beginUnit(unit("videos"), 0);
        check(w.check(600000) == StallWatchdog::Diagnose, "video: first diagnose");
        check(w.check(3600000) == StallWatchdog::Diagnose, "video: still diagnose, never cancel");
    }
    {
        StallWatchdog w(600000);
        w.noteActivity(0);
        w.beginUnit(unit("analyze"), 0);
        check(w.check(600000) == StallWatchdog::Diagnose, "analyze: diagnose");
        check(w.check(3600000) == StallWatchdog::Diagnose, "analyze: never cancel");
    }
    // A completion inside the stuck unit re-arms to diagnose-only.
    {
        StallWatchdog w(600000);
        w.noteActivity(0);
        w.beginUnit(unit("images"), 0);
        check(w.check(600000) == StallWatchdog::Diagnose, "episode opens");
        w.noteActivity(700000);
        check(w.check(1300000) == StallWatchdog::Diagnose, "progress re-arms to diagnose");
    }
    // Report carries the forensics fields.
    {
        msf::StallState st;
        st.phase = "walk";
        st.stalledMs = 600001;
        st.queueDepth = 4096;
        st.walkerDone = false;
        st.walkerBlockedTicks = 12;
        st.walkerStarvedTicks = 3;
        st.scanned = 24244;
        st.completed = 0;
        st.unit = unit("videos");
        st.unitElapsedMs = 601000;
        const std::string r = StallWatchdog::buildReport(st, "diagnosed");
        check(r.find("WATCHDOG") != std::string::npos, "report tags WATCHDOG");
        check(r.find("stalledMs=600001") != std::string::npos, "report carries stall time");
        check(r.find("queue=4096") != std::string::npos, "report carries queue depth");
        check(r.find("walker=alive") != std::string::npos, "report carries walker state");
        check(r.find("unit=videos") != std::string::npos, "report carries unit kind");
        check(r.find("a.jpg") != std::string::npos, "report carries sample paths");
    }
    std::cout << "stall_watchdog_selfcheck=" << (gOk ? "ok" : "FAILED")
              << " checks=" << gChecks << "\n";
    return gOk ? 0 : 1;
}
