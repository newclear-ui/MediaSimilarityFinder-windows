// Backend IPC codec test (P3b). Round-trip plus rejection paths. QtCore
// only (no GUI): the same codec both ends of the channel share.
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <iostream>

#include "backend_ipc.h"

namespace {

int gChecks = 0;
bool gOk = true;

void check(bool cond, const char* what) {
    ++gChecks;
    if (!cond) { gOk = false; std::cerr << "  [FAIL] " << what << "\n"; }
    else       { std::cout << "  [ok] " << what << "\n"; }
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    using msf_ipc::Message;
    // Round-trip with UTF-8 payload (non-ASCII path: 0.9.4.47 lesson).
    {
        Message m;
        m.type = "STATE";
        m.requestId = 7;
        m.sequence = 42;
        m.nonce = "n-1";
        m.payload[QStringLiteral("path")] = QString::fromUtf8("C:/exa/\xed\x95\x9c\xea\xb8\x80/a.jpg");
        const QByteArray line = msf_ipc::encodeLine(m);
        check(!line.contains('\n'), "encoded line has no newline");
        Message back;
        QString reject;
        check(msf_ipc::decodeLine(line, back, reject), "round-trip decodes");
        check(back.type == "STATE", "type survives");
        check(back.requestId == 7 && back.sequence == 42, "ids survive");
        check(back.nonce == "n-1", "nonce survives");
        check(back.payload.value("path").toString().endsWith("a.jpg"), "utf8 path survives");
    }
    // Rejections never throw and always name a reason.
    {
        Message back;
        QString reject;
        check(!msf_ipc::decodeLine("{nope", back, reject) && !reject.isEmpty(), "malformed rejected");
        check(!msf_ipc::decodeLine("{\"protocol\":2,\"type\":\"X\",\"requestId\":0,\"sequence\":0,\"payload\":{}}",
                                   back, reject),
              "protocol mismatch rejected");
        check(!msf_ipc::decodeLine("{\"protocol\":1,\"requestId\":0,\"sequence\":0,\"payload\":{}}",
                                   back, reject),
              "missing type rejected");
        QByteArray big(msf_ipc::kMaxLineBytes + 1, 'x');
        check(!msf_ipc::decodeLine(big, back, reject), "oversize rejected");
    }
    std::cout << "backend_ipc_selfcheck=" << (gOk ? "ok" : "FAILED")
              << " checks=" << gChecks << "\n";
    return gOk ? 0 : 1;
}
