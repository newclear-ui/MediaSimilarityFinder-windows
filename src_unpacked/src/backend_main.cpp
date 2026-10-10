// MediaSimilarityFinderBackend entry point.
//
// P1: --help/--version only. P3 (0.9.4.69): --backend serves the GUI-spawned
// IPC endpoint: line-oriented JSON over stdin (commands) / stdout (events,
// flushed per message) / stderr (diagnostics). Owns one BackendSession
// (scan thread + worker + monitor). Qt6::Core only — never Widgets/Gui.
//
// Shutdown discipline: SHUTDOWN cancels the scan and waits boundedly (8s)
// for the worker, then exits with SHUTDOWN_ACK sent. Stdin EOF (GUI gone)
// self-exits the same way (G6 channel-loss rule, not command inactivity).
#include <QCoreApplication>
#include <QDateTime>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <thread>

#include "backend_ipc.h"
#include "backend_sysinfo.h"
#include "backend_log.h"
#include "backend_session.h"
#include "backend_thumb.h"
#include "msf_build_version.h"

namespace {

using msf_ipc::Message;

constexpr int kShutdownWaitMs = 8000;
constexpr int kHealthPeriodMs = 1000;
constexpr int kMaxMatchBatch = 500;
constexpr int kMaxResultsPage = 2000;
constexpr int kMaxEncodedThumbBytes = 200 * 1024;
constexpr int kMalformedAbortAfter = 10;

void printUsage(QTextStream& out) {
    out << "Usage:\n"
        << "  MediaSimilarityFinderBackend --help\n"
        << "  MediaSimilarityFinderBackend --version\n"
        << "  MediaSimilarityFinderBackend --backend --nonce <token> [--appdir <dir>]\n"
        << "\n"
        << "Backend process for MediaSimilarityFinder (GUI-spawned IPC endpoint).\n";
}

QJsonObject matchJson(const QString& l, const QString& r, double pct, int kind) {
    QJsonObject o;
    o[QStringLiteral("left")] = l;
    o[QStringLiteral("right")] = r;
    o[QStringLiteral("percent")] = pct;
    o[QStringLiteral("kind")] = kind;
    return o;
}

class BackendServer : public QObject {
public:
    BackendServer(const QString& nonce, bool silent, QObject* parent = nullptr)
        : QObject(parent), nonce_(nonce), silent_(silent) {
        session_ = new BackendSession(this);
        connect(session_, &BackendSession::progress, this, &BackendServer::onProgress);
        connect(session_, &BackendSession::progressCount, this, &BackendServer::onProgressCount);
        connect(session_, &BackendSession::walkedCount, this, &BackendServer::onWalkedCount);
        connect(session_, &BackendSession::fingerprintProgress, this, &BackendServer::onFingerprintProgress);
        connect(session_, &BackendSession::targetCount, this, &BackendServer::onTargetCount);
        connect(session_, &BackendSession::listingProgress, this, &BackendServer::onListingProgress);
        connect(session_, &BackendSession::matchesBatch, this, &BackendServer::onMatchesBatch);
        connect(session_, &BackendSession::quickLoaded, this, &BackendServer::onQuickLoaded);
        connect(session_, &BackendSession::revalidated, this, &BackendServer::onRevalidated);
        connect(session_, &BackendSession::revalidateProgress, this, &BackendServer::onRevalidateProgress);
        connect(session_, &BackendSession::results, this, &BackendServer::onResults);
        connect(session_, &BackendSession::telemetryReady, this, &BackendServer::onTelemetryReady);
        connect(session_, &BackendSession::finished, this, &BackendServer::onFinished);
        connect(session_, &BackendSession::failed, this, &BackendServer::onFailed);
        connect(session_, &BackendSession::statusSnapshot, this, &BackendServer::onStatusSnapshot);
        connect(session_, &BackendSession::monitorEvent, this, &BackendServer::onMonitorEvent);
        connect(session_, &BackendSession::monitorSnapshot, this, &BackendServer::onMonitorSnapshot);
        connect(session_, &BackendSession::stateChanged, this, &BackendServer::onStateChanged);
        connect(session_, &BackendSession::policyApplied, this, &BackendServer::onPolicyApplied);
        health_.setInterval(kHealthPeriodMs);
        connect(&health_, &QTimer::timeout, this, &BackendServer::onHealthTick);
        startMs_ = QDateTime::currentMSecsSinceEpoch();
    }

    void run() {
        health_.start();
        reader_ = std::thread([this] { readLoop(); });
    }

    bool greeted() const { return greeted_; }

private:
    void send(Message m) {
        m.sequence = ++sequence_;
        m.nonce = nonce_;
        out_ << QString::fromUtf8(msf_ipc::encodeLine(m)) << "\n";
        out_.flush(); // flush per message: pipes must not batch events (G7)
        lastEventMs_ = QDateTime::currentMSecsSinceEpoch();
    }
    void sendError(const QString& reason, quint64 requestId) {
        Message m;
        m.type = QString::fromLatin1(msf_ipc::kError);
        m.requestId = requestId;
        m.payload[QStringLiteral("reason")] = reason;
        send(m);
    }
    void log(const QString& line) {
        // Diagnostics go to stderr only: the GUI forwards them to msf_scan.log
        // (single writer, no duplicate lines from two processes).
        err_ << line << "\n";
        err_.flush();
    }

    void readLoop() {
        // Blocking stdin read on a dedicated thread (never the event thread).
        // Null line = EOF = control channel lost (G6) -> orderly self-exit.
        QTextStream in(stdin, QIODevice::ReadOnly);
        while (true) {
            const QString line = in.readLine();
            if (line.isNull()) {
                QMetaObject::invokeMethod(this, [this] { onChannelLost(); });
                return;
            }
            const QString copy = line;
            QMetaObject::invokeMethod(this, [this, copy] { onCommandLine(copy); });
        }
    }

    void onCommandLine(const QString& line) {
        Message m;
        QString reject;
        if (!msf_ipc::decodeLine(line.toUtf8(), m, reject)) {
            log(QStringLiteral("reject: ") + reject);
            if (++malformed_ > kMalformedAbortAfter) {
                log(QStringLiteral("too many malformed lines; exiting"));
                requestQuit(3);
                return;
            }
            sendError(reject, 0);
            return;
        }
        malformed_ = 0;
        if (!greeted_ && m.type != QLatin1String(msf_ipc::kHello)) {
            sendError(QStringLiteral("not-ready"), m.requestId);
            return;
        }
        const QString t = m.type;
        if (t == QLatin1String(msf_ipc::kHello)) {
            greeted_ = true;
            if (silent_) return; // MSF_TEST_BACKEND_SILENT: never answer
            Message ack;
            ack.type = QString::fromLatin1(msf_ipc::kHelloAck);
            ack.requestId = m.requestId;
            ack.payload[QStringLiteral("version")] = QString::fromLatin1(MSF_BUILD_VERSION);
            send(ack);
            sendReady();
        } else if (t == QLatin1String(msf_ipc::kStartScan)) {
            startScanFrom(m);
        } else if (t == QLatin1String(msf_ipc::kPause)) {
            session_->pause();
        } else if (t == QLatin1String(msf_ipc::kResume)) {
            session_->resume();
        } else if (t == QLatin1String(msf_ipc::kCancel)) {
            session_->cancel();
        } else if (t == QLatin1String(msf_ipc::kConfigure)) {
            configureFrom(m);
        } else if (t == QLatin1String(msf_ipc::kUpdatePolicy)) {
            updatePolicyFrom(m);
        } else if (t == QLatin1String(msf_ipc::kGetThumbnail)) {
            serveThumbnail(m);
        } else if (t == QLatin1String(msf_ipc::kGetFileMeta)) {
            const msf::FileMeta fm =
                session_->requestFileMeta(m.payload.value(QStringLiteral("path")).toString().toStdString());
            Message out;
            out.type = QString::fromLatin1(msf_ipc::kFileMeta);
            out.requestId = m.requestId;
            out.payload[QStringLiteral("path")] = m.payload.value(QStringLiteral("path")).toString();
            out.payload[QStringLiteral("width")] = fm.width;
            out.payload[QStringLiteral("height")] = fm.height;
            out.payload[QStringLiteral("duration")] = fm.duration;
            out.payload[QStringLiteral("ok")] = fm.ok;
            send(out);
        } else if (t == QLatin1String(msf_ipc::kShutdown)) {
            onShutdown(m.requestId);
        } else {
            sendError(QStringLiteral("unknown command"), m.requestId);
        }
    }

    void sendReady() {
        Message m;
        m.type = QString::fromLatin1(msf_ipc::kReady);
        m.payload[QStringLiteral("version")] = QString::fromLatin1(MSF_BUILD_VERSION);
        send(m);
    }

    void startScanFrom(const Message& m) {
        const QJsonObject p = m.payload;
        const QJsonObject exec = p.value(QStringLiteral("exec")).toObject();
        const QStringList ignoredList =
            p.value(QStringLiteral("ignored")).toVariant().toStringList();
        const QSet<QString> ignored(ignoredList.begin(), ignoredList.end());
        // P4: exec carries the two-axis policy with identical meaning (no
        // Backend reinterpretation). Scalars stay as fallback for older senders.
        session_->startScan(p.value(QStringLiteral("root")).toString(),
                            p.value(QStringLiteral("appDir")).toString(),
                            p.value(QStringLiteral("distance")).toInt(8),
                            exec.value(QStringLiteral("cpuPercent")).toInt(
                                p.value(QStringLiteral("cpu")).toInt(50)),
                            exec.value(QStringLiteral("gpuPercent")).toInt(
                                p.value(QStringLiteral("gpuPercent")).toInt(50)),
                            exec.value(QStringLiteral("gpuEnabled")).toBool(
                                p.value(QStringLiteral("gpuEnabled")).toBool(false)),
                            p.value(QStringLiteral("scanImages")).toBool(true),
                            p.value(QStringLiteral("scanVideos")).toBool(true),
                            ignored,
                            p.value(QStringLiteral("detailedLog")).toBool(true),
                            exec.value(QStringLiteral("cpuMode")).toInt(0),
                            exec.value(QStringLiteral("strategy")).toInt(0),
                            p.value(QStringLiteral("testMode")).toBool(false),
                            p.value(QStringLiteral("analyzeMode")).toInt(0));
    }

    void updatePolicyFrom(const Message& m) {
        const QJsonObject p = m.payload;
        session_->updateResourcePolicy(p.value(QStringLiteral("cpuMode")).toInt(0),
                                       p.value(QStringLiteral("cpuPercent")).toInt(55),
                                       p.value(QStringLiteral("gpuPercent")).toInt(60),
                                       p.value(QStringLiteral("strategy")).toInt(0),
                                       p.value(QStringLiteral("gpuEnabled")).toBool(true));
        // APPLIED report is emitted by the session policyApplied signal
        // (wired in run()).
    }

    void configureFrom(const Message& m) {
        // CONFIGURE carries monitor control and/or a monitor policy refresh
        // (Type A). Engine policy travels per-scan in START_SCAN.
        const QJsonObject p = m.payload;
        const QJsonObject pol = p.value(QStringLiteral("monitorPolicy")).toObject();
        if (!pol.isEmpty()) {
            storedPolicy_.cpuPercent = pol.value(QStringLiteral("cpuPercent")).toInt(55);
            storedPolicy_.gpuPercent = pol.value(QStringLiteral("gpuPercent")).toInt(60);
            storedPolicy_.gpuEnabled = pol.value(QStringLiteral("gpuEnabled")).toBool(true);
            const int mode = pol.value(QStringLiteral("mode")).toInt(-1);
            if (mode >= 1 && mode <= 5)
                storedPolicy_.mode = static_cast<msf::ResourceMode>(mode);
            session_->setMonitorPolicy(storedPolicy_);
        }
        const QJsonObject mon = p.value(QStringLiteral("monitor")).toObject();
        if (mon.isEmpty()) return;
        if (mon.value(QStringLiteral("action")).toString() == QStringLiteral("stop")) {
            session_->stopMonitor();
            return;
        }
        if (mon.value(QStringLiteral("action")).toString() != QStringLiteral("start")) return;
        QStringList watch, compare;
        for (const auto& r : mon.value(QStringLiteral("watchRoots")).toArray())
            watch.push_back(r.toString());
        for (const auto& r : mon.value(QStringLiteral("compareRoots")).toArray())
            compare.push_back(r.toString());
        session_->startMonitor(
            watch, compare,
            mon.value(QStringLiteral("appDir")).toString(QCoreApplication::applicationDirPath()),
            mon.value(QStringLiteral("thresholdPercent")).toDouble(90.0),
            mon.value(QStringLiteral("stableSeconds")).toInt(3),
            mon.value(QStringLiteral("pollSeconds")).toInt(2),
            mon.value(QStringLiteral("gpuEnabled")).toBool(true), storedPolicy_);
    }

    void serveThumbnail(const Message& m) {
        const QJsonObject p = m.payload;
        const QString path = p.value(QStringLiteral("path")).toString();
        const bool isVideo = p.value(QStringLiteral("isVideo")).toBool(false);
        // Store serves finished JPEG end to end (no re-encode here).
        const msf::JpegThumb jpg = session_->requestThumb(path, isVideo);
        Message out;
        out.type = QString::fromLatin1(msf_ipc::kThumbnail);
        out.requestId = m.requestId;
        QJsonObject op;
        op[QStringLiteral("path")] = path;
        op[QStringLiteral("ok")] = false;
        if (jpg.ok && !jpg.bytes.empty() && (int)jpg.bytes.size() <= kMaxEncodedThumbBytes) {
            op[QStringLiteral("ok")] = true;
            op[QStringLiteral("width")] = jpg.width;
            op[QStringLiteral("height")] = jpg.height;
            op[QStringLiteral("jpegBase64")] = QString::fromLatin1(
                QByteArray(reinterpret_cast<const char*>(jpg.bytes.data()),
                           (int)jpg.bytes.size())
                    .toBase64());
        }
        out.payload = op;
        send(out);
    }

    void onShutdown(quint64 requestId) {
        Message ack;
        ack.type = QString::fromLatin1(msf_ipc::kState);
        ack.requestId = requestId;
        ack.payload[QStringLiteral("state")] = QStringLiteral("SHUTTING_DOWN");
        send(ack);
        session_->shutdown();
        // Bounded worker wind-down (8s): cancel is cooperative, a pathological
        // decode must not hold process exit forever. Matches the GUI-side
        // teardown expectation (quit/wait) with an explicit cap.
        const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
        while (sessionBusy() &&
               QDateTime::currentMSecsSinceEpoch() - t0 < kShutdownWaitMs)
            QThread::msleep(100);
        Message done;
        done.type = QString::fromLatin1(msf_ipc::kState);
        done.payload[QStringLiteral("state")] = QStringLiteral("SHUTDOWN_ACK");
        send(done);
        requestQuit(0);
    }

    bool sessionBusy() const { return session_ && session_->scanActive(); }

    void onChannelLost() {
        log(QStringLiteral("control channel lost; self-exit"));
        session_->shutdown();
        requestQuit(0);
    }

    void requestQuit(int code) {
        exitCode_ = code;
        health_.stop();
        if (reader_.joinable()) reader_.detach();
        QCoreApplication::exit(code);
    }

    // ---- session signal fan-out (all run on this thread via queued links) ----
    void onProgress(int pct, QString path) {
        Message m;
        m.type = QString::fromLatin1(msf_ipc::kProgress);
        m.payload[QStringLiteral("pct")] = pct;
        m.payload[QStringLiteral("path")] = path;
        send(m);
    }
    void onProgressCount(qulonglong done, qulonglong total) {
        Message m;
        m.type = QString::fromLatin1(msf_ipc::kProgress);
        m.payload[QStringLiteral("kind")] = QStringLiteral("count");
        m.payload[QStringLiteral("done")] = QString::number(done);
        m.payload[QStringLiteral("total")] = QString::number(total);
        send(m);
    }
    void onWalkedCount(qulonglong n) {
        Message m;
        m.type = QString::fromLatin1(msf_ipc::kListingProgress);
        m.payload[QStringLiteral("kind")] = QStringLiteral("walked");
        m.payload[QStringLiteral("n")] = QString::number(n);
        send(m);
    }
    // 0.9.4.85: revalidation progress (pairs checked / total) over the STATE
    // channel, so a long engine-version revalidation is visible in the UI.
    void onRevalidateProgress(qulonglong done, qulonglong total) {
        Message m;
        m.type = QString::fromLatin1(msf_ipc::kState);
        m.payload[QStringLiteral("state")] = QStringLiteral("REVALIDATE_PROGRESS");
        m.payload[QStringLiteral("done")] = QString::number(done);
        m.payload[QStringLiteral("total")] = QString::number(total);
        send(m);
    }
    void onFingerprintProgress(qulonglong files, qulonglong bytes, QString path) {
        Message m;
        m.type = QString::fromLatin1(msf_ipc::kFingerprintProgress);
        m.payload[QStringLiteral("files")] = QString::number(files);
        m.payload[QStringLiteral("bytes")] = QString::number(bytes);
        m.payload[QStringLiteral("path")] = path;
        send(m);
    }
    void onTargetCount(qulonglong n) {
        Message m;
        m.type = QString::fromLatin1(msf_ipc::kListingProgress);
        m.payload[QStringLiteral("kind")] = QStringLiteral("target");
        m.payload[QStringLiteral("n")] = QString::number(n);
        send(m);
    }
    void onListingProgress(std::size_t n) {
        Message m;
        m.type = QString::fromLatin1(msf_ipc::kListingProgress);
        m.payload[QStringLiteral("kind")] = QStringLiteral("listed");
        m.payload[QStringLiteral("n")] = QString::number((qulonglong)n);
        send(m);
    }
    void onMatchesBatch(const QVector<LiveMatch>& batch) {
        // Lossless, chunked (G4): never one giant line.
        for (int i = 0; i < batch.size(); i += kMaxMatchBatch) {
            QJsonArray arr;
            for (int k = i; k < batch.size() && k < i + kMaxMatchBatch; ++k)
                arr.push_back(matchJson(batch[k].left, batch[k].right,
                                        batch[k].percent, batch[k].kind));
            Message m;
            m.type = QString::fromLatin1(msf_ipc::kMatchesBatch);
            m.payload[QStringLiteral("matches")] = arr;
            send(m);
        }
    }
    void onQuickLoaded(int n) {
        Message m;
        m.type = QString::fromLatin1(msf_ipc::kState);
        m.payload[QStringLiteral("state")] = QStringLiteral("QUICK_LOADED");
        m.payload[QStringLiteral("n")] = n;
        send(m);
    }
    void onRevalidated(int kept, int dropped) {
        Message m;
        m.type = QString::fromLatin1(msf_ipc::kState);
        m.payload[QStringLiteral("state")] = QStringLiteral("REVALIDATED");
        m.payload[QStringLiteral("kept")] = kept;
        m.payload[QStringLiteral("dropped")] = dropped;
        send(m);
    }
    void onResults(const QVector<GuiFile>& files, const QStringList& rows) {
        // Chunked like matches (a full file list can be megabytes).
        const int total = files.size();
        const int pages = (total + kMaxResultsPage - 1) / kMaxResultsPage;
        for (int pg = 0; pg < pages; ++pg) {
            QJsonArray arr;
            for (int i = pg * kMaxResultsPage;
                 i < total && i < (pg + 1) * kMaxResultsPage; ++i) {
                QJsonObject o;
                o[QStringLiteral("path")] = files[i].path;
                o[QStringLiteral("size")] = QString::number(files[i].size);
                o[QStringLiteral("fpHex")] = files[i].fpHex;
                o[QStringLiteral("duration")] = files[i].duration;
                arr.push_back(o);
            }
            Message m;
            m.type = QString::fromLatin1(msf_ipc::kState);
            m.payload[QStringLiteral("state")] = QStringLiteral("RESULTS_PAGE");
            m.payload[QStringLiteral("page")] = pg;
            m.payload[QStringLiteral("pages")] = pages;
            m.payload[QStringLiteral("files")] = arr;
            if (pg == pages - 1) {
                QJsonArray ra;
                for (const auto& r : rows) ra.push_back(r);
                m.payload[QStringLiteral("matchRows")] = ra;
            }
            send(m);
        }
        if (total == 0) {
            Message m;
            m.type = QString::fromLatin1(msf_ipc::kState);
            m.payload[QStringLiteral("state")] = QStringLiteral("RESULTS_PAGE");
            m.payload[QStringLiteral("page")] = 0;
            m.payload[QStringLiteral("pages")] = 0;
            QJsonArray ra;
            for (const auto& r : rows) ra.push_back(r);
            m.payload[QStringLiteral("matchRows")] = ra;
            send(m);
        }
    }
    void onTelemetryReady(QString json) {
        Message m;
        m.type = QString::fromLatin1(msf_ipc::kTelemetry);
        m.payload[QStringLiteral("json")] = json;
        send(m);
    }
    void onFinished(QString msg) {
        Message m;
        m.type = QString::fromLatin1(msf_ipc::kFinished);
        m.payload[QStringLiteral("message")] = msg;
        send(m);
    }
    void onFailed(QString msg) {
        Message m;
        m.type = QString::fromLatin1(msf_ipc::kFailed);
        m.payload[QStringLiteral("message")] = msg;
        send(m);
    }
    void onStatusSnapshot(const SessionStatus& st) {
        Message m;
        m.type = QString::fromLatin1(msf_ipc::kStatus);
        m.payload[QStringLiteral("analyzed")] = QString::number(st.analyzed);
        m.payload[QStringLiteral("unchanged")] = QString::number(st.unchanged);
        m.payload[QStringLiteral("gpuActive")] = st.gpuActive;
        m.payload[QStringLiteral("gpuAvailable")] = st.gpuAvailable;
        m.payload[QStringLiteral("gpuDone")] = QString::number(st.gpuDone);
        send(m);
    }
    void onMonitorEvent(const msf::MonitorEvent& e) {
        Message m;
        m.type = QString::fromLatin1(msf_ipc::kState);
        m.payload[QStringLiteral("state")] = QStringLiteral("MONITOR_EVENT");
        m.payload[QStringLiteral("kind")] = static_cast<int>(e.type);
        m.payload[QStringLiteral("path")] = QString::fromStdString(e.path);
        m.payload[QStringLiteral("detail")] = QString::fromStdString(e.detail);
        QJsonArray arr;
        for (const auto& mm : e.matches) {
            QJsonObject o;
            o[QStringLiteral("newPath")] = QString::fromStdString(mm.newPath);
            o[QStringLiteral("existingPath")] = QString::fromStdString(mm.existingPath);
            o[QStringLiteral("percent")] = mm.percent;
            arr.push_back(o);
        }
        m.payload[QStringLiteral("matches")] = arr;
        send(m);
    }
    void onMonitorSnapshot(const SessionMonitorStatus& s) {
        Message m;
        m.type = QString::fromLatin1(msf_ipc::kMonitorStatus);
        m.payload[QStringLiteral("running")] = s.running;
        m.payload[QStringLiteral("loadState")] = s.loadState;
        m.payload[QStringLiteral("cpuPercent")] = s.cpuPercent;
        m.payload[QStringLiteral("memoryPercent")] = s.memoryPercent;
        m.payload[QStringLiteral("gpuPercent")] = s.gpuPercent;
        m.payload[QStringLiteral("analyzed")] = QString::number(s.analyzed);
        m.payload[QStringLiteral("pending")] = QString::number(s.pending);
        send(m);
    }
    void onStateChanged(QString state) {
        sessionState_ = state;
        Message m;
        m.type = QString::fromLatin1(msf_ipc::kState);
        m.payload[QStringLiteral("state")] = state;
        send(m);
    }
    void onPolicyApplied(bool partial, int mode, int cpu, int gpu, int strategy, bool gpuOn) {
        Message m;
        m.type = QString::fromLatin1(msf_ipc::kState);
        m.payload[QStringLiteral("state")] = QStringLiteral("POLICY_APPLIED");
        m.payload[QStringLiteral("partial")] = partial;
        m.payload[QStringLiteral("mode")] = mode;
        m.payload[QStringLiteral("cpuPercent")] = cpu;
        m.payload[QStringLiteral("gpuPercent")] = gpu;
        m.payload[QStringLiteral("strategy")] = strategy;
        m.payload[QStringLiteral("gpuEnabled")] = gpuOn;
        send(m);
    }
    void onHealthTick() {
        if (session_->monitorRunning()) session_->refreshMonitor();
        double cpu = 0.0;
        unsigned long long rss = 0;
        // 0.9.4.88: backendCpu now covers the backend process AND every child it
        // spawns (nvidia-smi / ffprobe via captureSilent), so the summary's
        // combined CPU accounts for all scan/index work, not just one process.
        msf::sampleProcessTree(cpu, rss);
        Message m;
        m.type = QString::fromLatin1(msf_ipc::kHealth);
        m.payload[QStringLiteral("state")] = sessionState_;
        m.payload[QStringLiteral("uptimeMs")] =
            QString::number(QDateTime::currentMSecsSinceEpoch() - startMs_);
        m.payload[QStringLiteral("backendCpu")] = cpu;
        m.payload[QStringLiteral("backendRssMB")] = QString::number(rss);
        send(m);
    }

    BackendSession* session_ = nullptr;
    QTimer health_;
    std::thread reader_;
    QTextStream out_{stdout};
    QTextStream err_{stderr};
    QString nonce_;
    quint64 sequence_ = 0;
    qint64 startMs_ = 0;
    qint64 lastEventMs_ = 0;
    int malformed_ = 0;
    int exitCode_ = 0;
    bool greeted_ = false;
    bool silent_ = false;
    QString sessionState_ = QStringLiteral("STARTING");
    msf::ResourcePolicy storedPolicy_;
};

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("MediaSimilarityFinderBackend"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(MSF_BUILD_VERSION));
    const QStringList args = app.arguments();
    QTextStream out(stdout);
    QTextStream err(stderr);
    if (args.contains(QStringLiteral("--help")) || args.contains(QStringLiteral("-h"))) {
        printUsage(out);
        return 0;
    }
    if (args.contains(QStringLiteral("--version"))) {
        out << "MediaSimilarityFinderBackend " << MSF_BUILD_VERSION << "\n";
        return 0;
    }
    if (!args.contains(QStringLiteral("--backend"))) {
        printUsage(err);
        return 2;
    }
    QString nonce;
    QString appDir = QCoreApplication::applicationDirPath();
    for (int i = 1; i + 1 < args.size(); ++i) {
        if (args[i] == QStringLiteral("--nonce")) nonce = args[i + 1];
        if (args[i] == QStringLiteral("--appdir")) appDir = args[i + 1];
    }
    if (nonce.isEmpty()) {
        err << "MediaSimilarityFinderBackend: --backend requires --nonce <token>\n";
        return 2;
    }
    // Test-only fault injection (P3): deterministic startup failures for the
    // supervisor budget tests. Production never sets these variables.
    // FAIL_FAST exits before the handshake (unexpected-exit budget path);
    // SILENT never answers (READY-timeout budget path).
    if (qEnvironmentVariableIsSet("MSF_TEST_BACKEND_FAIL_FAST")) return 1;
    const bool silent = qEnvironmentVariableIsSet("MSF_TEST_BACKEND_SILENT");
    Q_UNUSED(appDir);
    BackendServer server(nonce, silent);
    server.run();
    return app.exec();
}
