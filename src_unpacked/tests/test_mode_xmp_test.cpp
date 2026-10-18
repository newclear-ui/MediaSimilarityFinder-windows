// Test Mode XMP orientation regression (0.9.4.127).
//
// The decode-level fixtures in xmp_orientation_test prove the transform, but
// not that the fallback changes verdicts through the real pipeline. This test
// drives the production ScanWorker/MediaSearchEngine path twice over five
// synthetic JPEGs — once as a production scan, once as a Test Mode scan — and
// asserts the same pair verdicts in both, plus production-index isolation.
//
// Fixtures (one WIC-encoded base, APP1 spliced per file, never the dataset):
//   a1/a2: no EXIF, XMP=1            -> identical -> must pair (control)
//   b1:    no EXIF, XMP=6            -> 90CW applied -> must NOT pair with a
//   c1:    EXIF=6, XMP=1             -> EXIF wins -> must NOT pair with a
//   d1:    no EXIF, XMP=banana       -> invalid, identity -> must pair with a
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#endif
#include "backend_session.h"
#include "index_manager.h"
#include "path_utils.h"
#include "scan_worker.h"  // LiveMatch: pair verdicts without string parsing

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int gChecks = 0;
bool gOk = true;

void chk(bool cond, const char* what) {
    ++gChecks;
    if (!cond) {
        gOk = false;
        std::cerr << "  [FAIL] " << what << std::endl;
    } else {
        std::cout << "  [ok] " << what << std::endl;
    }
}

#ifdef _WIN32
using Microsoft::WRL::ComPtr;

std::wstring toWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? (size_t)(n - 1) : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

// 16x8 BMP with four distinct quadrants (same pattern as xmp_orientation_test:
// the orientation is observable, and identical sources stay byte-identical).
bool writeBmp(const std::string& path) {
    const int w = 16, h = 8, img = w * h * 3, fsz = 54 + img;
    unsigned char hd[54] = {0};
    hd[0] = 'B'; hd[1] = 'M';
    hd[2] = (unsigned char)(fsz & 0xFF); hd[3] = (unsigned char)((fsz >> 8) & 0xFF);
    hd[10] = 54; hd[14] = 40; hd[18] = (unsigned char)w; hd[22] = (unsigned char)h;
    hd[26] = 1; hd[28] = 24;
    hd[34] = (unsigned char)(img & 0xFF); hd[35] = (unsigned char)((img >> 8) & 0xFF);
    std::FILE* f = nullptr;
    if (_wfopen_s(&f, toWide(path).c_str(), L"wb") != 0 || !f) return false;
    bool ok = fwrite(hd, 1, 54, f) == 54;
    for (int y = 0; ok && y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const unsigned char v = (y < 4) ? ((x < 8) ? 0 : 85) : ((x < 8) ? 170 : 255);
            ok = fputc(v, f) != EOF && fputc(v, f) != EOF && fputc(v, f) != EOF;
        }
    fclose(f);
    return ok;
}

bool encodeJpeg(const std::string& bmpPath, std::vector<unsigned char>& out) {
    HRESULT cohr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(cohr)) return false;
    bool ok = false;
    {
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICBitmapDecoder> dec;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> encFrame;
        ComPtr<IStream> stream;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&factory));
        if (FAILED(hr)) hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                              IID_PPV_ARGS(&factory));
        if (FAILED(hr) || !factory) goto done;
        hr = factory->CreateDecoderFromFilename(toWide(bmpPath).c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnLoad, &dec);
        if (FAILED(hr)) goto done;
        hr = dec->GetFrame(0, &frame);
        if (FAILED(hr)) goto done;
        hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
        if (FAILED(hr)) goto done;
        hr = factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder);
        if (FAILED(hr)) goto done;
        hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
        if (FAILED(hr)) goto done;
        hr = encoder->CreateNewFrame(&encFrame, nullptr);
        if (FAILED(hr)) goto done;
        hr = encFrame->Initialize(nullptr);
        if (FAILED(hr)) goto done;
        hr = encFrame->WriteSource(frame.Get(), nullptr);
        if (FAILED(hr)) goto done;
        hr = encFrame->Commit();
        if (FAILED(hr)) goto done;
        hr = encoder->Commit();
        if (FAILED(hr)) goto done;
        HGLOBAL hglob = nullptr;
        if (FAILED(GetHGlobalFromStream(stream.Get(), &hglob)) || !hglob) goto done;
        {
            const SIZE_T size = GlobalSize(hglob);
            const void* base = GlobalLock(hglob);
            if (!base || size < 4) { if (base) GlobalUnlock(hglob); goto done; }
            const unsigned char* b = (const unsigned char*)base;
            if (b[0] != 0xFF || b[1] != 0xD8) { GlobalUnlock(hglob); goto done; }
            out.assign(b, b + size);
            GlobalUnlock(hglob);
            ok = true;
        }
    }
done:
    CoUninitialize();
    return ok;
}

// Minimal little-endian EXIF APP1 carrying Orientation (type 3 = SHORT).
std::vector<unsigned char> exifApp1(unsigned short orientation) {
    std::vector<unsigned char> tiff;
    tiff.push_back('I'); tiff.push_back('I');
    tiff.push_back(0x2A); tiff.push_back(0x00);
    tiff.push_back(0x08); tiff.push_back(0x00); tiff.push_back(0x00); tiff.push_back(0x00);
    tiff.push_back(0x01); tiff.push_back(0x00);
    tiff.push_back(0x12); tiff.push_back(0x01);
    tiff.push_back(0x03); tiff.push_back(0x00);
    tiff.push_back(0x01); tiff.push_back(0x00); tiff.push_back(0x00); tiff.push_back(0x00);
    tiff.push_back((unsigned char)(orientation & 0xFF));
    tiff.push_back((unsigned char)((orientation >> 8) & 0xFF));
    tiff.push_back(0x00); tiff.push_back(0x00);
    tiff.push_back(0x00); tiff.push_back(0x00); tiff.push_back(0x00); tiff.push_back(0x00);
    std::vector<unsigned char> seg;
    seg.push_back(0xFF); seg.push_back(0xE1);
    const unsigned len = 2 + 6 + (unsigned)tiff.size();
    seg.push_back((unsigned char)((len >> 8) & 0xFF));
    seg.push_back((unsigned char)(len & 0xFF));
    const char* id = "Exif";
    for (int i = 0; i < 5; ++i) seg.push_back((unsigned char)id[i]);
    seg.push_back(0x00);
    seg.insert(seg.end(), tiff.begin(), tiff.end());
    return seg;
}

// XMP APP1 carrying tiff:Orientation="<value>" verbatim (invalid text allowed).
std::vector<unsigned char> xmpApp1(const std::string& value) {
    const std::string packet =
        "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">"
        "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
        "<rdf:Description rdf:about=\"\" xmlns:tiff=\"http://ns.adobe.com/tiff/1.0/\" "
        "tiff:Orientation=\"" + value + "\"/>"
        "</rdf:RDF></x:xmpmeta>";
    const char* head = "http://ns.adobe.com/xap/1.0/";
    std::vector<unsigned char> seg;
    seg.push_back(0xFF); seg.push_back(0xE1);
    const unsigned len = 2 + (unsigned)std::strlen(head) + 1 + (unsigned)packet.size();
    seg.push_back((unsigned char)((len >> 8) & 0xFF));
    seg.push_back((unsigned char)(len & 0xFF));
    for (const char* p = head; *p; ++p) seg.push_back((unsigned char)*p);
    seg.push_back(0x00);
    seg.insert(seg.end(), packet.begin(), packet.end());
    return seg;
}

bool writeJpegWithSegments(const std::vector<unsigned char>& base,
                           const std::vector<std::vector<unsigned char>>& segs,
                           const std::string& dst) {
    if (base.size() < 4 || base[0] != 0xFF || base[1] != 0xD8) return false;
    // Camera order: APP1 segments go after SOI + a leading APP0 (a bare-SOI
    // splice leaves EXIF/XMP where WIC does not resolve it, silently forcing
    // identity — the same layout the shared-WIC probe uses).
    std::size_t at = 2;
    if (base.size() > 4 && base[2] == 0xFF && base[3] == 0xE0) {
        const std::size_t segLen = ((std::size_t)base[4] << 8) | base[5];
        at = 4 + segLen;
        if (at > base.size()) return false;
    }
    std::vector<unsigned char> out;
    out.reserve(base.size() + 4096);
    out.insert(out.end(), base.begin(), base.begin() + (long)at);
    for (const auto& s : segs) out.insert(out.end(), s.begin(), s.end());
    out.insert(out.end(), base.begin() + (long)at, base.end());
    std::FILE* g = nullptr;
    if (_wfopen_s(&g, toWide(dst).c_str(), L"wb") != 0 || !g) return false;
    const bool ok = fwrite(out.data(), 1, out.size(), g) == out.size();
    fclose(g);
    return ok;
}

QByteArray fileSha256(const std::filesystem::path& path) {
    QFile file(QString::fromStdString(path.string()));
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return hash.result().toHex();
}

struct ProductionSnapshot {
    bool valid = false;
    QByteArray database;
    QByteArray videoCache;
    QByteArray metadata;
};

ProductionSnapshot snapshotProduction(const QString& appDir, const QString& root) {
    ProductionSnapshot snapshot;
    msf::IndexPaths paths;
    if (!msf::IndexManager::resolve(msf::path_from_utf8(appDir.toStdString()),
                                    msf::path_from_utf8(root.toStdString()), paths)) {
        return snapshot;
    }
    QDir dir;
    if (!dir.exists(QString::fromStdString(paths.database.string()))) return snapshot;
    snapshot.database = fileSha256(paths.database);
    snapshot.videoCache = fileSha256(paths.videoCache);
    snapshot.metadata = fileSha256(paths.metadata);
    snapshot.valid = !snapshot.database.isEmpty() && !snapshot.metadata.isEmpty();
    return snapshot;
}

struct ScanOutcome {
    bool finished = false;
    bool failed = false;
    QSet<QString> pairs;  // "leftBase|rightBase", basenames sorted
};

ScanOutcome runSessionScan(BackendSession& session, const QString& root, const QString& appDir,
                           bool testMode) {
    ScanOutcome out;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(180000);
    QObject::connect(&session, &BackendSession::matchesBatch, &loop,
                     [&](const QVector<LiveMatch>& batch) {
                         for (const auto& m : batch) {
                             const QString l = QFileInfo(m.left).fileName();
                             const QString r = QFileInfo(m.right).fileName();
                             out.pairs.insert(l < r ? l + "|" + r : r + "|" + l);
                         }
                     });
    QObject::connect(&session, &BackendSession::finished, &loop,
                     [&](const QString&) { out.finished = true; loop.quit(); });
    QObject::connect(&session, &BackendSession::failed, &loop,
                     [&](const QString&) { out.failed = true; loop.quit(); });
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] { loop.quit(); });
    session.startScan(root, appDir, 8, 55, 60, false, true, false, {}, true, 3, 0, testMode);
    timeout.start();
    loop.exec();
    return out;
}

bool hasPairWith(const QSet<QString>& pairs, const QString& name) {
    for (const auto& p : pairs) {
        if (p.startsWith(name + "|") || p.endsWith("|" + name)) return true;
    }
    return false;
}
#endif  // _WIN32

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    QCoreApplication app(argc, argv);
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto work = fs::temp_directory_path() / "msf_test_mode_xmp";
    fs::remove_all(work, ec);
    fs::create_directories(work / "media", ec);
    fs::create_directories(work / "app", ec);
    const std::string bmp = (work / "media" / "base.bmp").string();
    if (!writeBmp(bmp)) { std::cout << "bmp write failed\n"; return 2; }
    std::vector<unsigned char> jpeg;
    if (!encodeJpeg(bmp, jpeg)) { std::cout << "jpeg encode failed\n"; return 2; }
    auto make = [&](const std::string& name,
                    const std::vector<std::vector<unsigned char>>& segs) {
        const std::string p = (work / "media" / name).string();
        if (!writeJpegWithSegments(jpeg, segs, p)) { std::cout << "splice failed\n"; std::exit(2); }
        return p;
    };
    make("a1.jpg", {xmpApp1("1")});
    make("a2.jpg", {xmpApp1("1")});
    make("b1.jpg", {xmpApp1("6")});
    make("c1.jpg", {exifApp1(6), xmpApp1("1")});
    make("d1.jpg", {xmpApp1("banana")});
    fs::remove(work / "media" / "base.bmp", ec);
    const QString root = QString::fromStdString((work / "media").string());
    const QString appDir = QString::fromStdString((work / "app").string());

    // ---- production scan: the verdict contract ----------------------------------
    BackendSession baseline(&app);
    const ScanOutcome first = runSessionScan(baseline, root, appDir, false);
    baseline.shutdown();
    chk(first.finished && !first.failed, "production scan finished");
    if (!gOk) return 1;
    chk(first.pairs.contains("a1.jpg|a2.jpg"), "identical XMP=1 files pair (control)");
    // b1 (XMP=6) and c1 (EXIF=6) are the same 90CW rotation through different
    // sources, so they must pair with each other — and with nothing unrotated.
    // That pair is the end-to-end proof that both paths applied the transform.
    chk(first.pairs.contains("b1.jpg|c1.jpg"), "XMP=6 and EXIF=6 pair (same rotation)");
    {
        bool crossPair = false;
        for (const auto& p : first.pairs) {
            const bool touchesRotated = p.contains("b1.jpg") || p.contains("c1.jpg");
            const bool touchesFlat =
                p.contains("a1.jpg") || p.contains("a2.jpg") || p.contains("d1.jpg");
            if (touchesRotated && touchesFlat) crossPair = true;
        }
        chk(!crossPair, "no pair mixes rotated and unrotated files");
    }
    chk(hasPairWith(first.pairs, "d1.jpg"), "invalid XMP keeps identity (pairs with a)");
    if (!gOk) {
        for (const auto& p : first.pairs) std::cout << "  pair: " << p.toStdString() << "\n";
        return 1;
    }

    const ProductionSnapshot before = snapshotProduction(appDir, root);
    chk(before.valid, "production index snapshot readable");
    if (!gOk) return 2;

    // ---- Test Mode scan: same verdicts, isolated storage -------------------------
    BackendSession probe(&app);
    const ScanOutcome test = runSessionScan(probe, root, appDir, true);
    probe.shutdown();
    chk(test.finished && !test.failed, "Test Mode scan finished");
    chk(test.pairs == first.pairs, "Test Mode verdicts equal production verdicts");
    const ProductionSnapshot after = snapshotProduction(appDir, root);
    chk(after.valid, "production index snapshot readable after Test Mode");
    chk(after.database == before.database, "Test Mode left production index.sqlite unchanged");
    chk(after.videoCache == before.videoCache, "Test Mode left production video_cache.sqlite unchanged");
    chk(after.metadata == before.metadata, "Test Mode left production metadata.json unchanged");

    const QString scratchRoot = QDir(appDir).filePath(QStringLiteral("TestMode"));
    QDir scratch(scratchRoot);
    chk(scratch.exists(), "Test Mode created an isolated scratch directory");
    bool scratchIndexFound = false;
    if (scratch.exists()) {
        QDirIterator it(scratchRoot, QStringList() << QStringLiteral("index.sqlite"), QDir::Files,
                        QDirIterator::Subdirectories);
        scratchIndexFound = it.hasNext();
    }
    chk(scratchIndexFound, "Test Mode wrote its index inside scratch, not production");

    fs::remove_all(work, ec);
    std::cout << "test_mode_xmp_selfcheck=" << (gOk ? "ok" : "FAIL") << "\n";
    return gOk ? 0 : 1;
#else
    (void)argc;
    (void)argv;
    std::cout << "test_mode_xmp_selfcheck=SKIP non-Windows\n";
    return 0;
#endif
}
