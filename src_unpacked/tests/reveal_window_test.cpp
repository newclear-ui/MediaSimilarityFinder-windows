// End-to-end probe for MainWindow::revealPath against real Explorer windows:
// case A opens a fresh folder (fallback must spawn explorer.exe /select and the
// file must end up selected), case B re-invokes with the window already open
// (the same window must be reused: no new window, same HWND, still selected).
#include "mainwindow.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QProcess>
#include <QThread>
#include <QMetaObject>
#include <QDir>
#include <QUrl>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#include <exdisp.h>
#include <shlobj.h>
#include <shellapi.h>
#pragma comment(lib, "uuid.lib")
#endif

struct Found { bool ok = false; HWND hwnd = nullptr; IShellView* view = nullptr; };

static QString localFolderOf(IDispatch* disp) {
    IWebBrowser2* browser = nullptr;
    if (FAILED(disp->QueryInterface(IID_PPV_ARGS(&browser))) || !browser) return QString();
    BSTR raw = nullptr;
    QString out;
    if (SUCCEEDED(browser->get_LocationURL(&raw)) && raw) {
        const QUrl url(QString::fromWCharArray(raw));
        if (url.isLocalFile()) out = QDir::cleanPath(url.toLocalFile());
    }
    if (raw) SysFreeString(raw);
    browser->Release();
    return out;
}

static Found findExplorerWindow(const QString& folder) {
    Found f;
    IShellWindows* wins = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(ShellWindows), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&wins))) || !wins) return f;
    long count = 0;
    wins->get_Count(&count);
    const QString want = QDir::cleanPath(folder);
    for (long i = 0; i < count && !f.ok; ++i) {
        VARIANT index; VariantInit(&index); index.vt = VT_I4; index.lVal = i;
        IDispatch* disp = nullptr;
        const HRESULT hrItem = wins->Item(index, &disp);
        VariantClear(&index);
        if (FAILED(hrItem) || !disp) continue;
        if (QString::compare(localFolderOf(disp), want, Qt::CaseInsensitive) == 0) {
            IWebBrowser2* browser = nullptr;
            if (SUCCEEDED(disp->QueryInterface(IID_PPV_ARGS(&browser))) && browser) {
                SHANDLE_PTR rawHwnd = 0;
                if (SUCCEEDED(browser->get_HWND(&rawHwnd)) && rawHwnd)
                    f.hwnd = reinterpret_cast<HWND>(rawHwnd);
                IServiceProvider* provider = nullptr;
                if (SUCCEEDED(browser->QueryInterface(IID_PPV_ARGS(&provider))) && provider) {
                    IShellBrowser* sb = nullptr;
                    if (SUCCEEDED(provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&sb))) && sb) {
                        IShellView* sv = nullptr;
                        if (SUCCEEDED(sb->QueryActiveShellView(&sv)) && sv) {
                            f.view = sv;
                            f.ok = true;
                        }
                        sb->Release();
                    }
                    provider->Release();
                }
                browser->Release();
            }
        }
        disp->Release();
    }
    wins->Release();
    return f;
}

static int windowCount() {
    IShellWindows* wins = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(ShellWindows), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&wins))) || !wins) return -1;
    long count = 0;
    wins->get_Count(&count);
    wins->Release();
    return (int)count;
}

static QStringList selectedFilesOf(IShellView* sv) {
    QStringList out;
    IDataObject* dobj = nullptr;
    if (FAILED(sv->GetItemObject(SVGIO_SELECTION, IID_PPV_ARGS(&dobj))) || !dobj) return out;
    FORMATETC fmt;
    fmt.cfFormat = CF_HDROP; fmt.ptd = nullptr; fmt.dwAspect = DVASPECT_CONTENT;
    fmt.lindex = -1; fmt.tymed = TYMED_HGLOBAL;
    STGMEDIUM stg;
    ZeroMemory(&stg, sizeof(stg));
    if (SUCCEEDED(dobj->GetData(&fmt, &stg))) {
        const HDROP h = static_cast<HDROP>(stg.hGlobal);
        const UINT n = DragQueryFileW(h, 0xFFFFFFFF, nullptr, 0);
        wchar_t buf[32768];
        for (UINT i = 0; i < n; ++i) {
            if (DragQueryFileW(h, i, buf, 32768))
                out << QDir::cleanPath(QString::fromWCharArray(buf));
        }
        ReleaseStgMedium(&stg);
    }
    dobj->Release();
    return out;
}

static bool selectedIs(IShellView* view, const QString& file) {
    const QStringList sel = selectedFilesOf(view);
    return sel.size() == 1 && QString::compare(sel[0], file, Qt::CaseInsensitive) == 0;
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    QApplication app(argc, argv);
    initAppSettings(QDir::tempPath());
    QTemporaryDir dir;
    if (!dir.isValid()) { std::cerr << "no temp dir\n"; return 2; }
    const QString file = QDir::cleanPath(dir.path() + "/reveal_target.txt");
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly)) { std::cerr << "no temp file\n"; return 2; }
    f.write("reveal");
    f.close();
    MainWindow w;

    bool okA = false;
    HWND hwndA = nullptr;
    if (!QMetaObject::invokeMethod(&w, "revealPath", Qt::DirectConnection, Q_ARG(QString, file))) {
        std::cerr << "invoke failed\n"; return 2;
    }
    for (int i = 0; i < 150 && !okA; ++i) {
        Found fd = findExplorerWindow(dir.path());
        if (fd.ok) {
            if (selectedIs(fd.view, file)) { okA = true; hwndA = fd.hwnd; }
            fd.view->Release();
        }
        if (!okA) QThread::msleep(100);
    }
    std::cout << "fallback_open=" << (okA ? "ok" : "FAIL") << "\n";

    bool okB = false;
    const int countBefore = windowCount();
    if (!QMetaObject::invokeMethod(&w, "revealPath", Qt::DirectConnection, Q_ARG(QString, file))) {
        std::cerr << "invoke failed\n"; return 2;
    }
    QThread::msleep(1500);
    const int countAfter = windowCount();
    Found fd2 = findExplorerWindow(dir.path());
    if (fd2.ok) {
        okB = (countAfter == countBefore) && (fd2.hwnd == hwndA) && selectedIs(fd2.view, file);
        fd2.view->Release();
    }
    std::cout << "reuse_window=" << (okB ? "ok" : "FAIL")
              << " windows=" << countBefore << "->" << countAfter << "\n";

    if (hwndA) PostMessageW(hwndA, WM_CLOSE, 0, 0);
    QThread::msleep(500);
    CoUninitialize();
    const bool ok = okA && okB;
    std::cout << (ok ? "reveal_window=ok\n" : "reveal_window=FAIL\n");
    return ok ? 0 : 1;
}
