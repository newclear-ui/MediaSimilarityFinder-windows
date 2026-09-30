#include "mainwindow.h"
#include "command_line.h"
#include "media_search_engine.h"
#include "msf_build_version.h"
#include "path_utils.h"
#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTimer>
#include <iostream>
#include <string>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#include <io.h>
// The executable is built as WIN32_EXECUTABLE, so a console launch has no
// console of its own and a console-mode launch still inherits the caller's.
//
// Only reconfigure the standard streams when they are actually unusable.
// The previous implementation called freopen("CONOUT$") unconditionally, which
// destroyed any existing stdout/stderr redirection: piping or redirecting the
// output produced zero bytes. That silently broke scripted use of --version and
// --help, so redirection is now detected and left untouched.
//
// Returns false only when there is no console at all (double-click), where
// output is intentionally dropped and the GUI reports problems via MessageBox.
static bool attachParentConsole() {
    // Already running in a console: the inherited streams are already correct.
    if (GetConsoleWindow() != nullptr) return true;

    const auto streamIsRedirected = [](FILE* f) {
        const int fd = _fileno(f);
        if (fd < 0) return true;  // no descriptor: unusable
        const intptr_t raw = _get_osfhandle(fd);
        if (raw == -1 || raw == 0) return true;  // no usable OS handle
        const DWORD t = GetFileType(reinterpret_cast<HANDLE>(raw));
        return t == FILE_TYPE_DISK || t == FILE_TYPE_PIPE;
    };

    // A redirected stdout/stderr is a deliberate caller choice. Reopening it to
    // CONOUT$ would throw that away, so report success and leave it alone.
    const bool outRedirected = streamIsRedirected(stdout);
    const bool errRedirected = streamIsRedirected(stderr);
    if (outRedirected && errRedirected) return true;

    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        // No parent console. If nothing is redirected there is nowhere to write,
        // which is the double-click case and is handled by the GUI path.
        return outRedirected && errRedirected;
    }
    FILE* f = nullptr;
    if (!outRedirected) freopen_s(&f, "CONOUT$", "w", stdout);
    if (!errRedirected) freopen_s(&f, "CONOUT$", "w", stderr);
    return true;
}
#else
static bool attachParentConsole() { return true; }
#endif

// Single source of truth for the reported version. Previously this was a
// hardcoded literal that had drifted to 0.9.4.35 while the build was 0.9.4.43,
// so --version, --help and the window title disagreed with the real build.
namespace {
constexpr const char* kVersion = MSF_BUILD_VERSION;
}

// Only implemented options are listed. --benchmark, --mode, --suite,
// --resource, --cpu-percent, --log-dir and --log belong to later stages and are
// intentionally absent, so help text can never advertise something unimplemented.
static void printUsage() {
    std::cout
        << "Media Similarity Finder " << kVersion << "\n"
        << "Usage:\n"
        << "  MediaSimilarityFinder.exe\n"
        << "  MediaSimilarityFinder.exe --help\n"
        << "  MediaSimilarityFinder.exe --version\n"
        << "  MediaSimilarityFinder.exe --smoke\n"
        << "  MediaSimilarityFinder.exe --scan <folder> [--media images|videos|all]\n";
}

#ifdef _WIN32
static bool platformPluginPresent() {
    wchar_t imagePath[32768];
    const DWORD imageLen = GetModuleFileNameW(nullptr, imagePath, 32768);
    if (imageLen == 0 || imageLen >= 32768) return false;
    const QString dir = QFileInfo(QString::fromWCharArray(imagePath, int(imageLen))).absolutePath();
    return QFile::exists(dir + QStringLiteral("/platforms/qwindows.dll"))
        || QFile::exists(dir + QStringLiteral("/Qt6/plugins/platforms/qwindows.dll"));
}
#endif

// S1 headless scan. Reuses the production engine, index, video cache and
// resource policy paths unchanged; no separate search engine is introduced, and
// no MainWindow is constructed on this path.
static int runHeadlessScan(const msf::CommandLineOptions& opt, int argc, char** argv) {
    attachParentConsole();

    // QCoreApplication is enough here: this path never creates a widget, and it
    // must not require the platform plugin.
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationVersion(kVersion);
    initAppSettings(QCoreApplication::applicationDirPath());

    const std::string rootUtf8 = msf::path_to_utf8(msf::path_from_utf8(opt.scanRoot));
    std::error_code ec;
    if (!std::filesystem::is_directory(msf::path_from_utf8(opt.scanRoot), ec)) {
        std::cerr << "Error: target folder does not exist: " << opt.scanRoot << "\n";
        return 1;
    }

    const std::string appDir = QCoreApplication::applicationDirPath().toStdString();

    msf::MediaSearchEngine engine;
    if (!engine.openIndexForRoot(rootUtf8, appDir)) {
        std::cerr << "Error: could not open the index for: " << opt.scanRoot << "\n";
        return 1;
    }

    msf::ScanControl control;
    control.scanImages = (opt.scope != msf::MediaScope::Videos);
    control.scanVideos = (opt.scope != msf::MediaScope::Images);
    control.buildVersion = kVersion;

    std::cout << "Target   : " << opt.scanRoot
              << " | Scope : " << msf::mediaScopeName(opt.scope) << "\n"
              << "Build    : " << kVersion << "\n"
              << "Scanning ...\n";

    const msf::SearchReport rep = engine.scan(rootUtf8, 8, &control);

    std::cout << "scanned=" << rep.scanned
              << " added=" << rep.added
              << " modified=" << rep.modified
              << " unchanged=" << rep.unchanged
              << " removed=" << rep.removed
              << " analyzed=" << rep.analyzed
              << " candidates=" << rep.candidates
              << " groups=" << rep.groups
              << " indexedVideos=" << rep.indexedVideos
              << " videoCandidatePairs=" << rep.videoCandidatePairs
              << "\n";
    return 0;
}

int main(int argc, char** argv) {
    const msf::CommandLineOptions opt = msf::parseCommandLine(argc, argv);

    switch (opt.mode) {
        case msf::CommandMode::Help:
            attachParentConsole();
            printUsage();
            return 0;
        case msf::CommandMode::Version:
            attachParentConsole();
            std::cout << "Media Similarity Finder " << kVersion << " (CUDA/CPU)\n";
            return 0;
        case msf::CommandMode::Error:
            // Errors go to stderr and always produce a non-zero exit code, so a
            // bad option is never silently ignored.
            attachParentConsole();
            printUsage();
            std::cerr << "Error: " << opt.errorMessage << "\n";
            return opt.exitCode;
        case msf::CommandMode::Scan:
            return runHeadlessScan(opt, argc, argv);
        case msf::CommandMode::Smoke:
        case msf::CommandMode::Gui:
            break;
    }

#ifdef _WIN32
    if (!platformPluginPresent()) {
        if (attachParentConsole())
            std::cerr << "missing-platform-plugin\n";
        MessageBoxW(nullptr,
            L"Qt 플랫폼 플러그인(platforms/qwindows.dll)을 찾을 수 없습니다.\n"
            L"압축 파일은 끝까지 풀고, 폴더째로 복사한 뒤 실행하십시오.\n"
            L"zip 안에서 직접 실행하거나 exe만 복사한 경우에도 이 오류가 발생합니다.\n\n"
            L"Cannot find the Qt platform plugin (platforms/qwindows.dll).\n"
            L"Extract the entire package and run it from the extracted folder.",
            L"MediaSimilarityFinder", MB_ICONERROR | MB_OK);
        return 1;
    }
#endif
    QApplication app(argc, argv);
    QCoreApplication::setApplicationVersion(kVersion);
    initAppSettings(QCoreApplication::applicationDirPath());

    if (opt.mode == msf::CommandMode::Smoke) {
        // Headless CI smoke hook: build the full main window offscreen,
        // run the event loop briefly, then quit. Verifies GUI startup
        // without a display (QT_QPA_PLATFORM=offscreen).
        attachParentConsole();
        MainWindow w;
        w.show();
        QTimer::singleShot(3000, &app, &QApplication::quit);
        std::cout << "smoke: window created\n";
        const int rc = app.exec();
        std::cout << "smoke: event loop ok\n";
        return rc;
    }

    MainWindow w;
    w.show();
    return app.exec();
}
