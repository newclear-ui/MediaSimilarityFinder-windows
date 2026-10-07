// See backend_ipc.h.
#include "backend_ipc.h"

#include <QJsonDocument>
#include <QJsonParseError>

namespace msf_ipc {

QByteArray encodeLine(const Message& m) {
    QJsonObject o;
    o[QStringLiteral("protocol")] = kProtocol;
    o[QStringLiteral("type")] = m.type;
    o[QStringLiteral("requestId")] = QString::number(m.requestId);
    o[QStringLiteral("sequence")] = QString::number(m.sequence);
    o[QStringLiteral("nonce")] = m.nonce;
    o[QStringLiteral("payload")] = m.payload;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

static quint64 toU64(const QJsonValue& v, bool& ok) {
    if (v.isDouble()) {
        const double d = v.toDouble();
        if (d < 0 || d >= 1.8446744073709552e19 || d != (quint64)d) { ok = false; return 0; }
        ok = true;
        return (quint64)d;
    }
    if (v.isString()) {
        const quint64 n = v.toString().toULongLong(&ok);
        if (!ok) return 0;
        return n;
    }
    ok = false;
    return 0;
}

bool decodeLine(const QByteArray& line, Message& out, QString& rejectReason) {
    out = Message();
    if (line.size() > kMaxLineBytes) {
        rejectReason = QStringLiteral("oversize line");
        return false;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        rejectReason = QStringLiteral("malformed JSON");
        return false;
    }
    const QJsonObject o = doc.object();
    bool ok = false;
    const quint64 proto = toU64(o.value(QStringLiteral("protocol")), ok);
    if (!ok || proto != (quint64)kProtocol) {
        rejectReason = QStringLiteral("protocol mismatch");
        return false;
    }
    out.type = o.value(QStringLiteral("type")).toString();
    if (out.type.isEmpty()) {
        rejectReason = QStringLiteral("missing type");
        return false;
    }
    out.requestId = toU64(o.value(QStringLiteral("requestId")), ok);
    if (!ok) {
        rejectReason = QStringLiteral("bad requestId");
        return false;
    }
    out.sequence = toU64(o.value(QStringLiteral("sequence")), ok);
    if (!ok) {
        rejectReason = QStringLiteral("bad sequence");
        return false;
    }
    out.nonce = o.value(QStringLiteral("nonce")).toString();
    const QJsonValue p = o.value(QStringLiteral("payload"));
    if (!p.isObject() && !p.isUndefined()) {
        rejectReason = QStringLiteral("bad payload");
        return false;
    }
    out.payload = p.toObject();
    return true;
}

} // namespace msf_ipc
