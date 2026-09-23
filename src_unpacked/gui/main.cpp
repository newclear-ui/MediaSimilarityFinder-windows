#include "mainwindow.h"
#include <QApplication>
#include <QString>
#include <QTimer>
#include <iostream>

namespace {
constexpr const char* kVersion = "0.9.2.39";
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--version" || arg == "-v") {
            std::cout << "Media Similarity Finder " << kVersion << " (CUDA/CPU)\n";
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            std::cout << "Media Similarity Finder " << kVersion << "\n"
                      << "Usage: MediaSimilarityFinder.exe [--version|--help|--smoke]\n";
            return 0;
        }
        if (arg == "--smoke") {
            // Headless CI smoke hook: build the full main window offscreen,
            // run the event loop briefly, then quit. Verifies GUI startup
            // without a display (QT_QPA_PLATFORM=offscreen).
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
