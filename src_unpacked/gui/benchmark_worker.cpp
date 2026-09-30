#include "benchmark_worker.h"

#include <exception>

#include <QElapsedTimer>

#include "dataset_fingerprint.h"

namespace {

// Project one case's mode results into display lines, preserving S2's order so
// the UI shows AUTO -> CPU -> GPU-max exactly as they ran.
std::vector<BenchmarkGuiModeLine> toModeLines(const msf::BenchmarkCaseResult& c) {
    std::vector<BenchmarkGuiModeLine> lines;
    lines.reserve(c.modeResults.size());
    for (const auto& m : c.modeResults) {
        BenchmarkGuiModeLine l;
        l.requestedMode = QString::fromLatin1(msf::benchmarkModeDirName(m.requestedMode));
        l.effectiveMode = QString::fromLatin1(msf::gpuBackendKindName(m.effectiveMode));
        l.status = QString::fromLatin1(msf::benchmarkStatusName(m.status));
        l.started = m.started;
        l.completed = m.completed;
        l.elapsedMs = m.elapsedMs;
        l.errorMessage = QString::fromStdString(m.errorMessage);
        lines.push_back(std::move(l));
    }
    return lines;
}

} // namespace

BenchmarkWorker::BenchmarkWorker(msf::BenchmarkRequest request,
                                 std::vector<msf::GpuBackendKind> modes,
                                 msf::BenchmarkGuiStorage* storage,
                                 std::string datasetFingerprint)
    : request_(std::move(request)),
      modes_(std::move(modes)),
      datasetFingerprint_(std::move(datasetFingerprint)) {
    qRegisterMetaType<BenchmarkGuiProgress>("BenchmarkGuiProgress");
    qRegisterMetaType<BenchmarkGuiOutcome>("BenchmarkGuiOutcome");

    // Pin the run id and the per-mode index directories from the storage policy.
    // attach() installs no journal hooks, which is exactly what the GUI wants.
    if (storage) storage->attach(request_);
}

void BenchmarkWorker::cancel() {
    // Nothing else to do: BenchmarkRequest::isCancelled() reads this flag, and the
    // runner consults it before each file and each mode.
    cancel_.store(true);
}

void BenchmarkWorker::run() {
    // One benchmark at a time per worker. A second concurrent run() is refused
    // rather than silently interleaving two passes over the same index tree.
    if (running_.exchange(true)) return;

    emit busyChanged(true);

    auto finish = [this]() {
        running_.store(false);
        emit busyChanged(false);
    };

    if (modes_.empty()) {
        emit failed(QStringLiteral("no benchmark mode selected"));
        finish();
        return;
    }
    if (request_.sourceRoot.empty()) {
        emit failed(QStringLiteral("no source folder selected"));
        finish();
        return;
    }

    // The cancellation boundary is S2's own: the runner re-reads this lambda
    // before every file and before every mode.
    request_.isCancelled = [this] { return cancel_.load(); };

    // Observation only. These hooks report progress and write nothing. The GUI
    // snapshot is written by the caller from BenchmarkGuiOutcome::run.
    request_.onRunStarted = [this](const msf::BenchmarkRun& run) {
        caseTotal_ = static_cast<int>(run.filesStarted);
        emit started(caseTotal_);
    };
    request_.onCaseComplete = [this](const msf::BenchmarkCaseResult& c) {
        ++casesDelivered_;
        BenchmarkGuiProgress p;
        p.caseIndex = casesDelivered_;
        p.caseTotal = caseTotal_;
        p.caseLine.caseId = QString::fromStdString(c.caseId);
        p.caseLine.path = QString::fromStdString(c.path);
        p.caseLine.status = QString::fromLatin1(msf::benchmarkStatusName(c.status));
        p.modeLines = toModeLines(c);
        emit caseProgress(std::move(p));
    };

    // The dataset fingerprint is the product's own canonical value. It is computed
    // here, on the worker thread, because the walk reads file contents and must not
    // block the GUI. No new hash and no new serialisation are introduced: the
    // DatasetFingerprint's existing fingerprint string is passed through verbatim,
    // and it stays empty only when that computation reports the root as unavailable
    // or failed.
    std::string fingerprint = datasetFingerprint_;
    if (fingerprint.empty()) {
        fingerprint = msf::computeDatasetFingerprint(request_.sourceRoot).fingerprint;
    }

    // The executor is built here, inside the worker thread, exactly like
    // ScanWorker owns its engine: no engine is ever shared across threads.
    msf::ProductionBenchmarkExecutor executor(fingerprint);
    msf::BenchmarkRunner runner(executor);

    QElapsedTimer wall;
    wall.start();

    msf::BenchmarkRun result;
    try {
        // The single invocation. The mode subset and the file-level order come
        // from S2; this worker never reorders them into per-mode passes.
        result = runner.run(request_, modes_);
    } catch (const std::exception& e) {
        emit failed(QStringLiteral("benchmark failed: %1").arg(QString::fromUtf8(e.what())));
        finish();
        return;
    } catch (...) {
        emit failed(QStringLiteral("benchmark failed with an unknown error"));
        finish();
        return;
    }

    BenchmarkGuiOutcome outcome;
    outcome.status = QString::fromLatin1(msf::benchmarkStatusName(result.status));
    outcome.casesCompleted = static_cast<int>(result.filesCompleted);
    outcome.casesTotal = static_cast<int>(result.filesStarted);
    outcome.wallMs = static_cast<double>(wall.elapsed());
    outcome.run = std::move(result);

    emit finished(std::move(outcome));
    finish();
}
