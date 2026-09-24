#include "mainwindow.h"
#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTimer>
#include <iostream>
#include <string>
#ifdef _WIN32
#include <windows.h>
#include <cstdio>
// With WIN32_EXECUTABLE there is no console on launch. For CLI flags,
// attach to the caller's console so output is visible. Returns false when
// double-clicked (no console) ??output is then silently dropped.
static bool attachParentConsole() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return false;
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    return true;
}
#else
static bool attachParentConsole() { return true; }
#endif

namespace {
constexpr const char* kVersion = "0.9.2.89";
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

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string earlyArg = argv[i];
        if (earlyArg == "--version" || earlyArg == "-v") {
            attachParentConsole();
            std::cout << "Media Similarity Finder " << kVersion << " (CUDA/CPU)\n";
            return 0;
        }
        if (earlyArg == "--help" || earlyArg == "-h") {
            attachParentConsole();
            std::cout << "Media Similarity Finder " << kVersion << "\n"
                      << "Usage: MediaSimilarityFinder.exe [--version|--help|--smoke]\n";
            return 0;
        }
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
    initAppSettings(QCoreApplication::applicationDirPath());

    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--smoke") {
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
    }

    MainWindow w;
    w.show();
    return app.exec();
}
