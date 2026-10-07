// See backend_log.h.
#include "backend_log.h"

#include <QDateTime>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

namespace msf {
void backendLogLine(const std::string& line) {
    const QString p = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                      + QStringLiteral("/msf_scan.log");
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
    QTextStream out(&f);
    out << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << " "
        << QString::fromStdString(line) << "\n";
}
} // namespace msf
