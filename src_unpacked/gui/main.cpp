#include "mainwindow.h"
#include <QApplication>
#include <QString>
#include <iostream>

namespace {
constexpr const char* kVersion = "0.9.2.10";
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
                      << "Usage: MediaSimilarityFinder.exe [--version|--help]\n";
            return 0;
        }
    }

    MainWindow w;
    w.show();
    return app.exec();
}
