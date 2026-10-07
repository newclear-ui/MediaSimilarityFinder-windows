// See file_meta.h.
#include "file_meta.h"

#include <QStringList>

#include "proc_capture.h"

namespace msf {
bool ffprobeSize(const std::string& path, int& w, int& h) {
    // Local 8-bit (not UTF-8): console children parse non-ASCII paths in the
    // system code page, so Korean filenames keep working as before.
    const QString qpath = QString::fromUtf8(path.c_str());
    const QByteArray cmd =
        (QStringLiteral("ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 \"") +
         qpath + '"')
            .toLocal8Bit();
    std::string out;
    // Metadata class: 30 s budget (local probe, normally < 2 s; cached per path).
    if (!msf::captureSilent(std::string(cmd.constData(), (std::size_t)cmd.size()), out, 30000))
        return false;
    const QStringList parts = QString::fromLocal8Bit(out.c_str()).trimmed().split(',');
    if (parts.size() != 2) return false;
    bool okW = false, okH = false;
    const int pw = parts[0].trimmed().toInt(&okW), ph = parts[1].trimmed().toInt(&okH);
    if (!okW || !okH || pw <= 0 || ph <= 0) return false;
    w = pw;
    h = ph;
    return true;
}
} // namespace msf
