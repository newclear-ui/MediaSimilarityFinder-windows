// BackendSupervisor implementation (P3: 0.9.4.69). See backend_supervisor.h.
#include "backend_supervisor.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>
#include <QUuid>

#include <cstddef>

#include "backend_ipc.h"
#include "backend_thumb.h"

using msf_ipc::Message;

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

BackendSupervisor::BackendSupervisor(QObject* parent, const SupervisorOptions& opt)
    : BackendClient(parent), opt_(opt) {
    qRegisterMetaType<QVector<BackendMatch>>();
    qRegisterMetaType<QVector<BackendFile>>();
    qRegisterMetaType<ThumbResult>();
    qRegisterMetaType<BackendStatus>();
    qRegisterMetaType<BackendMonitorStatus>();
    qRegisterMetaType<BackendMonitorMatch>();
    qRegisterMetaType<BackendMonitorEvent>();
    proc_ = new QProcess(this);
    connect(proc_, &QProcess::started, this, &BackendSupervisor::onStarted);
    connect(proc_, &QProcess::finished, this, &BackendSupervisor::onFinished);
    connect(proc_, &QProcess::errorOccurred, this, &BackendSupervisor::onProcessError);
    connect(proc_, &QProcess::readyReadStandardOutput, this, &BackendSupervisor::onStdout);
    connect(proc_, &QProcess::readyReadStandardError, this, &BackendSupervisor::onStderr);
    connect(&healthTimer_, &QTimer::timeout, this, &BackendSupervisor::onHealthTick);
    connect(&escTimer_, &QTimer::timeout, this, &BackendSupervisor::onEscalationTimeout);
    connect(&restartTimer_, &QTimer::timeout, this, &BackendSupervisor::onRestartTimeout);
    healthTimer_.setInterval(1000);
    escTimer_.setSingleShot(true);
    restartTimer_.setSingleShot(true);
}

BackendSupervisor::~BackendSupervisor() {
    shutdown();
#ifdef _WIN32
    if (job_) {
        CloseHandle((HANDLE)job_);
        job_ = nullptr;
    }
#endif
}

qint64 BackendSupervisor::backendPid() const {
    if (!proc_ || proc_->state() == QProcess::NotRunning) return -1;
    return proc_->processId();
}

void BackendSupervisor::ensureRunning() {
    if (explicitShutdown_) return;
    if (procState_ == ProcState::Down) {
        spawn();
        return;
    }
    if (procState_ == ProcState::Failed) {
        // A way back from FAILED (e.g. user shows the window again after a
        // crash storm): fresh budget, fresh backend. The FAILED banner stays
        // until READY proves otherwise. Never stack onto a live wedged
        // process (G5): kill-proof backends need a manual kill or app restart.
        if (processAlive()) {
            emit backendLogLine(QStringLiteral("backend wedged (PID %1): kill it manually or restart the app — not spawning (G5)").arg(proc_->processId()));
            return;
        }
        attempts_ = 0;
        spawn();
    }
}

bool BackendSupervisor::processAlive() const {
    return proc_ && proc_->state() != QProcess::NotRunning;
}

// ---- commands ----

void BackendSupervisor::startScan(const BackendScanConfig& cfg) {
    if (!backendReady_ || !processAlive()) {
        emit failed(QStringLiteral("backend unavailable — scan not started"));
        return;
    }
    QJsonObject p;
    p[QStringLiteral("root")] = cfg.root;
    p[QStringLiteral("appDir")] = cfg.appDir;
    p[QStringLiteral("distance")] = cfg.distance;
    p[QStringLiteral("cpu")] = cfg.cpu;
    p[QStringLiteral("gpuPercent")] = cfg.gpuPercent;
    p[QStringLiteral("gpuEnabled")] = cfg.gpuEnabled;
    p[QStringLiteral("scanImages")] = cfg.scanImages;
    p[QStringLiteral("scanVideos")] = cfg.scanVideos;
    QJsonArray ig;
    for (const auto& s : cfg.ignored) ig.push_back(s);
    p[QStringLiteral("ignored")] = ig;
    p[QStringLiteral("detailedLog")] = cfg.detailedLog;
    p[QStringLiteral("testMode")] = cfg.testMode;
    p[QStringLiteral("analyzeMode")] = static_cast<int>(cfg.analyzeMode);
    QJsonObject exec;
    exec[QStringLiteral("cpuMode")] = cfg.exec.cpuMode;
    exec[QStringLiteral("cpuPercent")] = cfg.exec.cpuPercent;
    exec[QStringLiteral("gpuPercent")] = cfg.exec.gpuPercent;
    exec[QStringLiteral("strategy")] = cfg.exec.strategy;
    exec[QStringLiteral("gpuEnabled")] = cfg.exec.gpuEnabled;
    p[QStringLiteral("exec")] = exec;
    scanning_ = true;
    sendCommand(msf_ipc::kStartScan, p);
}

void BackendSupervisor::pause() {
    if (processAlive()) sendCommand(msf_ipc::kPause, {});
}

void BackendSupervisor::resume() {
    if (processAlive()) sendCommand(msf_ipc::kResume, {});
}

void BackendSupervisor::cancel() {
    if (processAlive()) sendCommand(msf_ipc::kCancel, {});
}

void BackendSupervisor::shutdown() {
    // Application-exit path: bounded synchronous wind-down (documented G3
    // exception — no event pumping, so no reentrancy). The Job Object
    // finishes the job if we exit first.
    explicitShutdown_ = true;
    restartTimer_.stop();
    escTimer_.stop();
    if (!processAlive()) return;
    sendCommand(msf_ipc::kShutdown, {});
    const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
    while (processAlive() &&
           QDateTime::currentMSecsSinceEpoch() - t0 < opt_.shutdownWaitMs)
        QThread::msleep(50);
    if (processAlive()) proc_->terminate();
}

static QJsonObject policyJson(const msf::ResourcePolicy& policy) {
    QJsonObject o;
    o[QStringLiteral("mode")] = static_cast<int>(policy.mode);
    o[QStringLiteral("cpuPercent")] = policy.cpuPercent;
    o[QStringLiteral("gpuPercent")] = policy.gpuPercent;
    o[QStringLiteral("gpuEnabled")] = policy.gpuEnabled;
    return o;
}

void BackendSupervisor::startMonitor(const QStringList& watchRoots, const QStringList& compareRoots,
                                     const QString& appDir, double thresholdPercent,
                                     int stableSeconds, int pollSeconds, bool gpuEnabled,
                                     const msf::ResourcePolicy& policy) {
    monWatch_ = watchRoots;
    monCompare_ = compareRoots;
    monAppDir_ = appDir;
    monThreshold_ = thresholdPercent;
    monStable_ = stableSeconds;
    monPoll_ = pollSeconds;
    monGpu_ = gpuEnabled;
    monPolicy_ = policy;
    monitorActive_ = true;
    if (!backendReady_ || !processAlive()) return; // re-sent on READY
    sendMonitorStart();
}

void BackendSupervisor::sendMonitorStart() {
    QJsonObject mon;
    mon[QStringLiteral("action")] = QStringLiteral("start");
    QJsonArray w, c;
    for (const auto& s : monWatch_) w.push_back(s);
    for (const auto& s : monCompare_) c.push_back(s);
    mon[QStringLiteral("watchRoots")] = w;
    mon[QStringLiteral("compareRoots")] = c;
    mon[QStringLiteral("appDir")] = monAppDir_;
    mon[QStringLiteral("thresholdPercent")] = monThreshold_;
    mon[QStringLiteral("stableSeconds")] = monStable_;
    mon[QStringLiteral("pollSeconds")] = monPoll_;
    mon[QStringLiteral("gpuEnabled")] = monGpu_;
    QJsonObject p;
    p[QStringLiteral("monitor")] = mon;
    p[QStringLiteral("monitorPolicy")] = policyJson(monPolicy_);
    sendCommand(msf_ipc::kConfigure, p);
}

void BackendSupervisor::stopMonitor() {
    monitorActive_ = false;
    if (!backendReady_ || !processAlive()) return;
    QJsonObject mon;
    mon[QStringLiteral("action")] = QStringLiteral("stop");
    QJsonObject p;
    p[QStringLiteral("monitor")] = mon;
    sendCommand(msf_ipc::kConfigure, p);
}

void BackendSupervisor::setMonitorPolicy(const msf::ResourcePolicy& policy) {
    monPolicy_ = policy;
    if (!backendReady_ || !processAlive()) return;
    QJsonObject p;
    p[QStringLiteral("monitorPolicy")] = policyJson(policy);
    sendCommand(msf_ipc::kConfigure, p);
}

void BackendSupervisor::updateResourcePolicy(const ExecutionPolicy& exec) {
    monPolicy_.mode = (exec.cpuMode >= 1 && exec.cpuMode <= 5)
                          ? static_cast<msf::ResourceMode>(exec.cpuMode)
                          : msf::ResourceMode::Custom;
    monPolicy_.cpuPercent = exec.cpuPercent;
    monPolicy_.gpuPercent = exec.gpuPercent;
    monPolicy_.gpuEnabled = exec.gpuEnabled;
    if (!backendReady_ || !processAlive()) return;
    QJsonObject p;
    p[QStringLiteral("cpuMode")] = exec.cpuMode;
    p[QStringLiteral("cpuPercent")] = exec.cpuPercent;
    p[QStringLiteral("gpuPercent")] = exec.gpuPercent;
    p[QStringLiteral("strategy")] = exec.strategy;
    p[QStringLiteral("gpuEnabled")] = exec.gpuEnabled;
    sendCommand(msf_ipc::kUpdatePolicy, p);
}

void BackendSupervisor::requestThumb(const QString& path, const QSize& size, bool isVideo,
                                     quint64 requestId) {
    if (!backendReady_ || !processAlive()) {
        emit thumbReady(requestId, ThumbResult()); // honest miss, clears pending
        return;
    }
    QJsonObject p;
    p[QStringLiteral("path")] = path;
    p[QStringLiteral("width")] = size.width();
    p[QStringLiteral("height")] = size.height();
    p[QStringLiteral("isVideo")] = isVideo;
    sendCommand(msf_ipc::kGetThumbnail, p, requestId);
}

void BackendSupervisor::requestFileMeta(const QString& path, quint64 requestId) {
    if (!backendReady_ || !processAlive()) {
        emit fileMetaReady(requestId, FileMetaResult());
        return;
    }
    QJsonObject p;
    p[QStringLiteral("path")] = path;
    sendCommand(msf_ipc::kGetFileMeta, p, requestId);
}

// ---- lifecycle ----

void BackendSupervisor::spawn() {
    if (processAlive()) return; // G5: never stack a new backend on a live one
    // Fresh process, fresh escalation state: a stale armed timer from a
    // previous round must never kill this backend (0.9.4.78).
    escStage_ = 0;
    killExhausted_ = false;
    healthMisses_ = 0;
    nonce_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    sequenceOut_ = 0;
    sequenceIn_ = 0;
    malformed_ = 0;
    stdoutBuf_.clear();
    stderrBuf_.clear();
    assemblingPages_ = -1;
    assemblingFiles_.clear();
    assemblingRows_.clear();
    const QString program =
        QCoreApplication::applicationDirPath() + QStringLiteral("/MediaSimilarityFinderBackend.exe");
    QStringList args;
    args << QStringLiteral("--backend") << QStringLiteral("--nonce") << nonce_
         << QStringLiteral("--appdir") << QCoreApplication::applicationDirPath();
    proc_->setProgram(program);
    proc_->setArguments(args);
    procState_ = ProcState::Starting;
    setPhase(QStringLiteral("Starting"));
    spawnMs_ = QDateTime::currentMSecsSinceEpoch();
    lastHealthMs_ = spawnMs_;
    emit backendLogLine(QStringLiteral("backend spawn: %1 pid pending (nonce %2)").arg(program, nonce_));
    proc_->start();
    if (!healthTimer_.isActive()) healthTimer_.start();
}

void BackendSupervisor::assignJobObject() {
#ifdef _WIN32
    if (!job_) {
        job_ = CreateJobObjectW(nullptr, nullptr);
        if (job_) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &info, sizeof(info));
        }
    }
    if (job_ && proc_) {
        HANDLE h = OpenProcess(PROCESS_ALL_ACCESS, FALSE, (DWORD)proc_->processId());
        if (h) {
            AssignProcessToJobObject(job_, h);
            CloseHandle(h);
        }
    }
#else
    (void)0;
#endif
}

void BackendSupervisor::onStarted() {
    assignJobObject();
    emit backendLogLine(QStringLiteral("backend started pid=%1").arg(proc_->processId()));
    Message hello;
    hello.type = QString::fromLatin1(msf_ipc::kHello);
    sendCommand(msf_ipc::kHello, {});
}

void BackendSupervisor::sendCommand(const QString& type, const QJsonObject& payload, quint64 requestId) {
    if (!processAlive()) return;
    msf_ipc::Message m;
    m.type = type;
    m.requestId = requestId;
    m.sequence = ++sequenceOut_;
    m.nonce = nonce_;
    m.payload = payload;
    proc_->write(msf_ipc::encodeLine(m) + '\n');
}

void BackendSupervisor::onStdout() {
    stdoutBuf_ += proc_->readAllStandardOutput();
    while (true) {
        const int nl = stdoutBuf_.indexOf('\n');
        if (nl < 0) {
            if (stdoutBuf_.size() > msf_ipc::kMaxLineBytes + 1024 * 1024) {
                stdoutBuf_.clear();
                handleMalformed(QStringLiteral("runaway lineless output"));
            }
            return;
        }
        const QByteArray line = stdoutBuf_.left(nl);
        stdoutBuf_ = stdoutBuf_.mid(nl + 1);
        handleLine(line);
    }
}

void BackendSupervisor::onStderr() {
    stderrBuf_ += proc_->readAllStandardError();
    while (true) {
        const int nl = stderrBuf_.indexOf('\n');
        if (nl < 0) return;
        const QByteArray line = stderrBuf_.left(nl);
        stderrBuf_ = stderrBuf_.mid(nl + 1);
        emit backendLogLine(QStringLiteral("[backend] ") +
                            QString::fromUtf8(line.left(4096)));
    }
}

void BackendSupervisor::handleMalformed(const QString& reason) {
    emit backendLogLine(QStringLiteral("backend protocol reject: ") + reason);
    if (++malformed_ > opt_.malformedLimit && processAlive() && !explicitShutdown_) {
        enterUnexpectedExit(QStringLiteral("protocol garbage from backend"), -1);
    }
}

void BackendSupervisor::handleLine(const QByteArray& line) {
    using msf_ipc::Message;
    Message m;
    QString reject;
    if (!msf_ipc::decodeLine(line, m, reject)) {
        handleMalformed(reject);
        return;
    }
    if (m.nonce != nonce_) return; // stale event from a previous instance: drop
    if (m.sequence <= sequenceIn_ && sequenceIn_ > 0) return; // duplicate: drop
    if (m.sequence > sequenceIn_ + 1 && sequenceIn_ > 0)
        emit backendLogLine(QStringLiteral("backend event gap: %1 -> %2")
                                .arg(sequenceIn_)
                                .arg(m.sequence));
    sequenceIn_ = m.sequence;
    lastHealthMs_ = QDateTime::currentMSecsSinceEpoch();
    healthMisses_ = 0; // any backend traffic is proof of life
    dispatchEvent(m.type, m.payload, m.requestId);
}

void BackendSupervisor::dispatchEvent(const QString& type, const QJsonObject& payload, quint64 requestId) {
    using namespace msf_ipc;
    const auto S = [](const QJsonValue& v) { return v.toString(); };
    if (type == QLatin1String(kHelloAck)) {
        emit backendLogLine(QStringLiteral("backend hello_ack version=") +
                            payload.value("version").toString());
        return;
    }
    if (type == QLatin1String(kReady)) {
        backendReady_ = true;
        attempts_ = 0; // fresh proof of life; the restart budget renews
        procState_ = ProcState::Ready;
        setPhase(QStringLiteral("Ready"));
        emit backendConnection(true, QStringLiteral("ready"));
        if (monitorActive_) sendMonitorStart(); // ambient config re-established
        return;
    }
    if (type == QLatin1String(kState)) {
        const QString st = S(payload.value("state"));
        if (st == QStringLiteral("QUICK_LOADED")) {
            emit quickLoaded(payload.value("n").toInt(0));
        } else if (st == QStringLiteral("REVALIDATED")) {
            emit revalidated(payload.value("kept").toInt(0), payload.value("dropped").toInt(0));
        } else if (st == QStringLiteral("RESULTS_PAGE")) {
            const int page = payload.value("page").toInt(0);
            const int pages = payload.value("pages").toInt(0);
            if (page == 0) {
                assemblingFiles_.clear();
                assemblingRows_.clear();
                assemblingPages_ = pages;
            }
            if (assemblingPages_ < 0) return;
            for (const auto& f : payload.value("files").toArray()) {
                const QJsonObject o = f.toObject();
                assemblingFiles_.push_back(
                    {o.value("path").toString(), o.value("size").toString().toULongLong(),
                     o.value("fpHex").toString(), o.value("duration").toDouble()});
            }
            if (page == pages - 1 || pages == 0) {
                for (const auto& r : payload.value("matchRows").toArray())
                    assemblingRows_.push_back(r.toString());
                lastFiles_ = assemblingFiles_;
                emit results(lastFiles_, assemblingRows_);
                assemblingPages_ = -1;
            }
        } else if (st == QStringLiteral("MONITOR_EVENT")) {
            BackendMonitorEvent ev;
            ev.type = payload.value("kind").toInt(0);
            ev.path = S(payload.value("path"));
            ev.detail = S(payload.value("detail"));
            for (const auto& mm : payload.value("matches").toArray()) {
                const QJsonObject o = mm.toObject();
                ev.matches.push_back({o.value("newPath").toString(),
                                      o.value("existingPath").toString(),
                                      o.value("percent").toDouble()});
            }
            emit monitorEvent(ev);
        } else if (st == QStringLiteral("POLICY_APPLIED")) {
            emit backendLogLine(QStringLiteral("policy APPLIED partial=%1 mode=%2 cpu=%3 gpu=%4 strategy=%5 gpuOn=%6")
                                    .arg(payload.value("partial").toBool(false))
                                    .arg(payload.value("mode").toInt(0))
                                    .arg(payload.value("cpuPercent").toInt(0))
                                    .arg(payload.value("gpuPercent").toInt(0))
                                    .arg(payload.value("strategy").toInt(0))
                                    .arg(payload.value("gpuEnabled").toBool(false)));
        }
        // SHUTTING_DOWN/SHUTDOWN_ACK/SCANNING/PAUSED/... : informational only;
        // scan state stays MainWindow-owned.
        return;
    }
    if (type == QLatin1String(kProgress)) {
        if (payload.contains("kind"))
            emit progressCount(payload.value("done").toString().toULongLong(),
                               payload.value("total").toString().toULongLong());
        else
            emit progress(payload.value("pct").toInt(0), S(payload.value("path")));
        return;
    }
    if (type == QLatin1String(kListingProgress)) {
        const QString kind = S(payload.value("kind"));
        const qulonglong n = payload.value("n").toString().toULongLong();
        if (kind == QStringLiteral("walked"))
            emit walkedCount(n);
        else if (kind == QStringLiteral("target"))
            emit targetCount(n);
        else
            emit listingProgress((std::size_t)n);
        return;
    }
    if (type == QLatin1String(kFingerprintProgress)) {
        emit fingerprintProgress(payload.value("files").toString().toULongLong(),
                                 payload.value("bytes").toString().toULongLong(),
                                 S(payload.value("path")));
        return;
    }
    if (type == QLatin1String(kMatchesBatch)) {
        QVector<BackendMatch> batch;
        for (const auto& mm : payload.value("matches").toArray()) {
            const QJsonObject o = mm.toObject();
            batch.push_back({o.value("left").toString(), o.value("right").toString(),
                             o.value("percent").toDouble(), o.value("kind").toInt(1)});
        }
        emit matchesBatch(batch);
        return;
    }
    if (type == QLatin1String(kTelemetry)) {
        emit telemetryReady(S(payload.value("json")));
        return;
    }
    if (type == QLatin1String(kFinished)) {
        scanning_ = false;
        emit finished(S(payload.value("message")));
        return;
    }
    if (type == QLatin1String(kFailed)) {
        scanning_ = false;
        emit failed(S(payload.value("message")));
        return;
    }
    if (type == QLatin1String(kHealth)) {
        // Own-process numbers ride the health ticker (P4 display contract).
        lastStatus_.backendCpu = payload.value("backendCpu").toDouble(0.0);
        lastStatus_.backendRssMB = payload.value("backendRssMB").toString().toULongLong();
        emit statusSnapshot(lastStatus_);
        return; // liveness already recorded; nothing else to display per tick
    }
    if (type == QLatin1String(kStatus)) {
        // Carry the last Health-sampled own-process CPU/RSS forward: this
        // message only carries engine counters, so a fresh BackendStatus would
        // zero backendCpu/backendRssMB and the summary would flip to 0% between
        // health ticks (the 0.9.4.72 display defect).
        BackendStatus st = lastStatus_;
        st.analyzed = payload.value("analyzed").toString().toULongLong();
        st.unchanged = payload.value("unchanged").toString().toULongLong();
        st.gpuActive = payload.value("gpuActive").toBool(false);
        st.gpuAvailable = payload.value("gpuAvailable").toBool(false);
        st.gpuDone = payload.value("gpuDone").toString().toULongLong();
        lastStatus_ = st;
        emit statusSnapshot(st);
        return;
    }
    if (type == QLatin1String(kMonitorStatus)) {
        BackendMonitorStatus st;
        st.running = payload.value("running").toBool(false);
        st.loadState = payload.value("loadState").toInt(0);
        st.cpuPercent = payload.value("cpuPercent").toDouble(0.0);
        st.memoryPercent = payload.value("memoryPercent").toDouble(0.0);
        st.gpuPercent = payload.value("gpuPercent").toDouble(-1.0);
        st.analyzed = payload.value("analyzed").toString().toULongLong();
        st.pending = payload.value("pending").toString().toULongLong();
        lastMonStatus_ = st;
        emit monitorSnapshot(st);
        return;
    }
    if (type == QLatin1String(kThumbnail)) {
        emitThumbReady(requestId, payload);
        return;
    }
    if (type == QLatin1String(kFileMeta)) {
        FileMetaResult r;
        r.path = S(payload.value("path"));
        r.width = payload.value("width").toInt(0);
        r.height = payload.value("height").toInt(0);
        r.duration = payload.value("duration").toDouble(0.0);
        r.ok = payload.value("ok").toBool(false);
        emit fileMetaReady(requestId, r);
        return;
    }
    if (type == QLatin1String(kError)) {
        emit backendLogLine(QStringLiteral("backend error: ") + S(payload.value("reason")));
        return;
    }
}

void BackendSupervisor::emitThumbReady(quint64 requestId, const QJsonObject& payload) {
    ThumbResult r;
    if (payload.value("ok").toBool(false)) {
        const QByteArray jpeg = QByteArray::fromBase64(payload.value("jpegBase64").toString().toLatin1());
        if (!jpeg.isEmpty() && jpeg.size() <= 1024 * 1024) {
            // Shared libjpeg-turbo decode (see backend_thumb.h): Qt's JPEG
            // plugin needs jpeg62.dll, which is not in the deployed set, so
            // QImage::fromData returns null and no thumbnail would paint.
            const msf::Argb32Image im = msf::decodeJpegArgb32(
                reinterpret_cast<const unsigned char*>(jpeg.constData()), (std::size_t)jpeg.size());
            if (im.ok) {
                r.rgba = QByteArray(reinterpret_cast<const char*>(im.bytes.data()), (int)im.bytes.size());
                r.width = im.width;
                r.height = im.height;
                r.ok = true;
            }
        }
    }
    emit thumbReady(requestId, r); // always emitted: clears GUI pending either way
}

// ---- failure / restart ----

void BackendSupervisor::onFinished(int exitCode, QProcess::ExitStatus status) {
    if (explicitShutdown_) {
        procState_ = ProcState::Down;
        setPhase(QStringLiteral("Down"));
        emit backendConnection(false, QStringLiteral("shutdown"));
        return;
    }
    const QString exitKind = status == QProcess::CrashExit
                                 ? QStringLiteral("crash exit")
                                 : QStringLiteral("normal exit");
    enterUnexpectedExit(QStringLiteral("backend process exited (%1, code %2)")
                            .arg(exitKind).arg(exitCode), exitCode);
}

void BackendSupervisor::onProcessError(QProcess::ProcessError error) {
    if (explicitShutdown_) return;
    if (error == QProcess::FailedToStart && procState_ == ProcState::Starting) {
        enterUnexpectedExit(QStringLiteral("backend failed to start"), -1);
    }
    // Other errors (timed out writing, etc.) surface through finished() too;
    // the exit path is the single decision point.
}

void BackendSupervisor::enterUnexpectedExit(const QString& reason, int exitCode) {
    backendReady_ = false;
    const bool scanAborted = scanning_;
    scanning_ = false;
    emit backendLogLine(QStringLiteral("backend unexpected exit: ") + reason);
    scheduleRestart();
    // Arm the restart timer before any GUI failure handler can enter a modal
    // dialog. The old ordering emitted failed() first; QMessageBox::critical()
    // then blocked this stack frame, so scheduleRestart() was not reached until
    // the user dismissed the popup.
    if (procState_ != ProcState::Failed)
        emit backendConnection(false, reason + QStringLiteral(" — restarting…"));
    if (scanAborted) {
        QString message = QStringLiteral("backend process exited unexpectedly — scan aborted");
        if (exitCode != -1) message += QStringLiteral(" (exit code %1)").arg(exitCode);
        emit failed(message);
    }
}

void BackendSupervisor::scheduleRestart() {
    if (attempts_ >= opt_.maxAttempts) {
        enterFailed(QStringLiteral("backend restart budget exhausted"));
        return;
    }
    const int backoff = opt_.backoffMs[qMin(attempts_, opt_.backoffMs.size() - 1)];
    ++attempts_;
    procState_ = ProcState::Down;
    setPhase(QStringLiteral("Recovering"));
    emit backendLogLine(QStringLiteral("backend restart %1/%2 in %3ms")
                            .arg(attempts_)
                            .arg(opt_.maxAttempts)
                            .arg(backoff));
    restartTimer_.start(backoff);
}

void BackendSupervisor::onRestartTimeout() {
    if (explicitShutdown_) return;
    if (processAlive()) return; // G5: never stack on a live backend
    setPhase(QStringLiteral("Restarting"));
    spawn();
}

void BackendSupervisor::enterFailed(const QString& reason) {
    procState_ = ProcState::Failed;
    setPhase(QStringLiteral("Failed"));
    emit backendLogLine(QStringLiteral("backend FAILED: ") + reason);
    emit backendConnection(false, reason);
    // Leave a wedged process to the escalation path, not to hope: kill it
    // asynchronously so no dual writer can linger. Once kill has proven
    // ineffective (killExhausted_), stop re-arming: re-terminating on every
    // timer tick only spams the log while the process stays alive.
    if (processAlive() && !killExhausted_) {
        proc_->terminate();
        escTimer_.start(opt_.killGraceMs);
    } else if (processAlive()) {
        emit backendLogLine(QStringLiteral("backend wedged (PID %1): kill it manually or restart the app — no new backend will be stacked on it (G5)").arg(proc_->processId()));
    }
}

void BackendSupervisor::terminateAndRespawn(const QString& reason) {
    if (!processAlive() || explicitShutdown_) return;
    backendReady_ = false;
    const bool scanAborted = scanning_;
    scanning_ = false;
    emit backendLogLine(QStringLiteral("backend watchdog: ") + reason);
    emit backendConnection(false, reason + QStringLiteral(" — restarting…"));
    escStage_ = 0;
    proc_->terminate();
    escTimer_.start(opt_.killGraceMs);
    if (scanAborted)
        emit failed(QStringLiteral("backend lost — scan aborted"));
}

void BackendSupervisor::onEscalationTimeout() {
    if (!processAlive() || explicitShutdown_) {
        if (!explicitShutdown_ && procState_ != ProcState::Failed) onRestartTimeout();
        return;
    }
    if (escStage_ == 0) {
        escStage_ = 1;
        proc_->kill();
        escTimer_.start(opt_.killWaitMs);
        return;
    }
    // Kill did not take the process down: spawning now would risk a SQLite
    // double writer (G5), so hold FAILED instead of hoping. Latch the
    // exhaustion so enterFailed cannot re-arm escalation into a log-spam loop.
    killExhausted_ = true;
    emit backendLogLine(QStringLiteral("backend refuses to die; holding FAILED"));
    enterFailed(QStringLiteral("backend process will not exit"));
}

void BackendSupervisor::onHealthTick() {
    if (explicitShutdown_ || !processAlive()) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (procState_ == ProcState::Starting) {
        if (now - spawnMs_ > opt_.readyTimeoutMs) {
            emit backendLogLine(QStringLiteral("backend startup READY timeout"));
            terminateAndRespawn(QStringLiteral("backend startup timeout"));
        }
        return;
    }
    if (!backendReady_) return;
    // 0.9.4.78: three-strike health rule. One silent window is usually a match
    // burst saturating the pipe/GUI (backend busy, not dead): only the third
    // consecutive miss escalates. Any backend message resets the count (the
    // reset lives where lastHealthMs_ is refreshed: every valid event).
    if (now - lastHealthMs_ > opt_.healthTimeoutMs) {
        if (++healthMisses_ < 3) {
            emit backendLogLine(QStringLiteral("backend health quiet %1/3").arg(healthMisses_));
            return;
        }
        healthMisses_ = 0;
        emit backendLogLine(QStringLiteral("backend health timeout"));
        terminateAndRespawn(QStringLiteral("backend health timeout"));
    }
}
