#pragma once
// S4 GUI Benchmark Worker — runs BenchmarkRunner on a worker thread.
//
// SCOPE. Execution glue only. This worker owns no benchmark logic and no
// storage policy: it calls S2's BenchmarkRunner exactly once with the modes the
// user selected, and reports what came back through Qt signals. Persistence is
// the caller's job (BenchmarkGuiStorage::writeSnapshots).
//
// Relationship to ScanWorker
// -------------------------
// Same shape on purpose: a QObject that MainWindow moves to a QThread, whose
// run() does the work on that thread and reports through signals. Two behaviours
// are copied deliberately:
//
//   * cancel() is a DIRECT call, never a queued slot. While run() occupies the
//     worker thread's event loop a queued slot cannot fire in time, which is
//     exactly why ScanWorker::cancel is called directly too.
//   * the executor/engine is constructed inside run(), so no engine instance is
//     ever shared across threads.
//
// What is NOT here
// ----------------
//   * No Pause/Resume. The GUI scan has it; benchmark deliberately does not.
//   * No journal hooks. onRunStarted and onCaseComplete are used ONLY to observe
//     progress. Nothing is persisted from inside this worker; the Console journal
//     belongs to S3/S5 and the GUI snapshot is written by the caller.
//   * No second execution engine. BenchmarkRunner is the only path.
//   * No scheduling against ScanWorker. The worker never starts a scan and never
//     inspects one; it only reports busyChanged so the UI can keep the scan and
//     benchmark starts mutually exclusive (S4 §4-13).
//
// Progress granularity — an honest limitation
// -------------------------------------------
// S2 reports a case only when it is COMPLETE, and exposes no intra-case hook.
// There is therefore no way to know which file or which mode is executing right
// now without changing S2, which is out of S4 scope. The progress signal
// consequently describes the most recently COMPLETED case. Reporting a guess as
// "current file" would be inventing a measurement, so it is not done; the
// per-mode detail below is that completed case's real result.

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include <QObject>
#include <QString>

#include "../src/benchmark_core.h"
#include "../src/benchmark_gui_store.h"

// One completed case, projected for display.
struct BenchmarkGuiCaseLine {
    QString caseId;
    QString path;
    QString status;  // SUCCESS / FAILED / CANCELLED / SKIPPED
};

// One mode result of the completed case, in S2's execution order.
struct BenchmarkGuiModeLine {
    QString requestedMode;  // auto / cpu / gpu-max
    QString effectiveMode;
    QString status;         // SUCCESS / FAILED / CANCELLED / SKIPPED
    bool started = false;
    bool completed = false;
    double elapsedMs = 0.0;
    QString errorMessage;
};

struct BenchmarkGuiProgress {
    int caseIndex = 0;  // 1-based number of cases delivered so far
    int caseTotal = 0;  // filesStarted, reported by S2 at run start
    BenchmarkGuiCaseLine caseLine;
    std::vector<BenchmarkGuiModeLine> modeLines;
};

struct BenchmarkGuiOutcome {
    QString status;  // the run's aggregate status
    int casesCompleted = 0;
    int casesTotal = 0;
    // GUI-side wall clock for the whole worker call. This is NOT a benchmark
    // measurement and must not be confused with the per-mode elapsedMs values,
    // which come from S2.
    double wallMs = 0.0;
    msf::BenchmarkRun run;  // the complete S2 result
};

Q_DECLARE_METATYPE(BenchmarkGuiProgress)
Q_DECLARE_METATYPE(BenchmarkGuiOutcome)

class BenchmarkWorker : public QObject {
    Q_OBJECT
public:
    // storage may be null. When given, its attach() pins request.runId and injects
    // the per-mode index application directory, so benchmark indexes live under the
    // GUI suite runtime tree and never beside the production Search Index.
    //
    // datasetFingerprint is the canonical value only. Leave it empty and the worker
    // computes it with the product's own msf::computeDatasetFingerprint(), which is
    // the same call MediaSearchEngine already makes during a normal scan. No new
    // hash and no new string format are introduced: the DatasetFingerprint's own
    // fingerprint string is recorded verbatim.
    BenchmarkWorker(msf::BenchmarkRequest request,
                    std::vector<msf::GpuBackendKind> modes,
                    msf::BenchmarkGuiStorage* storage = nullptr,
                    std::string datasetFingerprint = std::string());
    ~BenchmarkWorker() override = default;

    // Cancellation. This is the ONLY stop mechanism; there is no Pause.
    // Must be called DIRECTLY from the GUI thread, never through a queued
    // connection, for the same reason ScanWorker::cancel is.
    //
    // It sets the flag that BenchmarkRequest::isCancelled reads. The runner
    // re-reads that flag before every file and before every mode, so cancelling
    // mid-run yields exactly S2's contract: the interrupted mode becomes
    // Cancelled and a mode that never started becomes Skipped.
    void cancel();

    bool cancelled() const { return cancel_.load(); }
    // True while run() is executing. Guards against a second concurrent run.
    bool running() const { return running_.load(); }

    const msf::BenchmarkRequest& request() const { return request_; }
    const std::vector<msf::GpuBackendKind>& modes() const { return modes_; }

public slots:
    // Executes one benchmark. Emits started(), caseProgress() per completed case,
    // then either finished() or failed(). Exactly one of those terminal signals
    // is emitted per run(), and busyChanged(false) always follows.
    void run();

signals:
    // Emitted once, before any execution, with the file count S2 discovered.
    void started(int caseTotal);
    // Emitted after each COMPLETED case. See the granularity note above.
    void caseProgress(BenchmarkGuiProgress progress);
    void finished(BenchmarkGuiOutcome outcome);
    void failed(QString message);
    // UI gating only: lets the caller keep the scan start and the benchmark start
    // mutually exclusive without this worker knowing anything about ScanWorker.
    void busyChanged(bool busy);

private:
    msf::BenchmarkRequest request_;
    std::vector<msf::GpuBackendKind> modes_;
    std::string datasetFingerprint_;
    std::atomic_bool cancel_{false};
    std::atomic_bool running_{false};
    int caseTotal_ = 0;
    int casesDelivered_ = 0;
};
