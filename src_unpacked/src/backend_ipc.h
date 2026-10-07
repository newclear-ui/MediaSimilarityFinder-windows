// Backend IPC codec (P3b: 0.9.4.69). Line-oriented JSON over stdio:
// GUI writes commands to the Backend stdin, the Backend writes events to
// stdout (flushed per message) and diagnostics to stderr. Shared by the
// Backend server and the GUI supervisor so both ends speak one framing.
// QtCore only — no widgets, usable from msf_core and Qt tests.
#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace msf_ipc {

// Wire protocol revision. Bump only with a matching Backend + GUI.
inline constexpr int kProtocol = 1;
// Oversized lines are rejected before parsing (brief §22 message cap).
inline constexpr int kMaxLineBytes = 64 * 1024 * 1024;

struct Message {
    QString type;
    quint64 requestId = 0;
    quint64 sequence = 0;
    QString nonce;
    QJsonObject payload;
};

// Encode one message as a single compact-JSON line (no trailing newline;
// the transport adds it). UTF-8 by construction (QString → UTF-8).
QByteArray encodeLine(const Message& m);

// Decode one line. Returns false (and fills rejectReason) on malformed
// JSON, framing errors, protocol mismatch, or oversize — never throws.
bool decodeLine(const QByteArray& line, Message& out, QString& rejectReason);

// Command types (GUI → Backend).
inline const char* kHello = "HELLO";
inline const char* kStartScan = "START_SCAN";
inline const char* kPause = "PAUSE";
inline const char* kResume = "RESUME";
inline const char* kCancel = "CANCEL";
inline const char* kConfigure = "CONFIGURE";
inline const char* kShutdown = "SHUTDOWN";
inline const char* kGetThumbnail = "GET_THUMBNAIL";
inline const char* kGetFileMeta = "GET_FILE_META";

// Event types (Backend → GUI).
inline const char* kHelloAck = "HELLO_ACK";
inline const char* kReady = "READY";
inline const char* kState = "STATE";
inline const char* kProgress = "PROGRESS";
inline const char* kListingProgress = "LISTING_PROGRESS";
inline const char* kFingerprintProgress = "FINGERPRINT_PROGRESS";
inline const char* kMatchesBatch = "MATCHES_BATCH";
inline const char* kTelemetry = "TELEMETRY";
inline const char* kFinished = "FINISHED";
inline const char* kFailed = "FAILED";
inline const char* kHealth = "HEALTH";
inline const char* kStatus = "STATUS";
inline const char* kMonitorStatus = "MONITOR_STATUS";
inline const char* kThumbnail = "THUMBNAIL";
inline const char* kFileMeta = "FILE_META";
inline const char* kError = "ERROR";

} // namespace msf_ipc
