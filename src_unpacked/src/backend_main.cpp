// MediaSimilarityFinderBackend entry point (P1: 0.9.4.67).
//
// BackendCore process boundary, phase P1: the BackendCore library (the
// msf_core target: engine, database, monitor, scanner, decoders) builds and
// links standalone with Qt6::Core only — never Qt6::Widgets/Gui. The GUI
// still runs everything in-process in P1; spawning and the IPC endpoint
// arrive in P3, so --backend refuses with an explicit message (covered by
// CTest as an expected failure until P3). No scan/search runs here yet.
#include <QCoreApplication>
#include <QStringList>
#include <QTextStream>

#include "msf_build_version.h"

namespace {

void printUsage(QTextStream& out) {
    out << "Usage:\n"
        << "  MediaSimilarityFinderBackend --help\n"
        << "  MediaSimilarityFinderBackend --version\n"
        << "  MediaSimilarityFinderBackend --backend\n"
        << "\n"
        << "Backend process for MediaSimilarityFinder (P1: entry point only).\n"
        << "--backend starts the GUI-spawned IPC endpoint (arrives in P3).\n";
}

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
    if (args.contains(QStringLiteral("--backend"))) {
        err << "MediaSimilarityFinderBackend: IPC endpoint arrives in P3; "
               "the GUI still runs in-process in P1.\n";
        return 2;
    }
    printUsage(err);
    return 2;
}
