// Cross-process QSettings round-trip probe for the main window UI state.
//
// --probe-write <dir> <W> <H>: build the window, resize it, destroy it
//    (the destructor persists geometry/splitter/headers), then confirm the
//    INI file landed on disk.
// --probe-verify <dir> <W> <H>: fresh process, build the window, confirm the
//    restored size matches. CTest runs write first, verify second (DEPENDS).
//
// The split across two processes is the point: same-process read-back would
// hit QSettings' in-memory cache and pass even if disk persistence is broken
// (e.g. missing organization/application names -> AccessError -> nothing
// persists, which is exactly the bug this guards against).
#include "mainwindow.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QMetaObject>
#include <QPushButton>
#include <QToolButton> // 0.9.4.149: merged Load/Re-verify slot
#include <QMenu>       // 0.9.4.149: merged Load/Re-verify slot
#include <QSpinBox>
#include <QStringList>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include "index_manager.h" // 0.9.4.152: persist-fail index resolve
#include "database.h"      // 0.9.4.152: persist-fail DB assertions
#include "path_utils.h"    // 0.9.4.152: persist-fail path conversion
#include "media_search_engine.h" // 0.9.4.153: cancel-consistency baseline stamp
#include <QLayout>
#include <QLabel>      // 0.9.4.152: persist-fail status assertion
#include <QListWidget> // 0.9.4.152: persist-fail group grid
#include <QTimer>      // 0.9.4.152: persist-fail modal closer
#include <QThread>     // 0.9.4.152: persist-fail pump
#include <QElapsedTimer> // 0.9.4.152: persist-fail pump
#include <QStandardPaths> // 0.9.4.153: cancel-consistency log tail
#include <unordered_map>  // 0.9.4.153: cancel-consistency DB group map
#include <string>         // 0.9.4.153: cancel-consistency DB group map
#include <atomic>         // 0.9.4.153: cancel-consistency progress flag
#include <QSettings>
#include <QDir>
#include <iostream>

// 0.9.4.29 legacy settings migration. Same cross-process discipline as the
// round-trip probe above, and for the same reason: a same-process read-back
// would be served from QSettings' in-memory cache and would pass even if the
// migration never touched disk.
//
// --migrate <dir>: a prior process planted a legacy newclear-ui INI with real
//    values. This process runs initAppSettings() and must end up with those
//    values readable from the NEW location, and the legacy INI gone.
// --migrate-none <dir>: no legacy file. initAppSettings() must not create one
//    and must not invent settings.
// --migrate-both <dir>: both locations already hold different values. The new
//    location must win and the legacy file must survive untouched.
namespace {
const char* const kNewOrg = "MediaSimilarityFinder-ui";
const char* const kLegacyOrg = "newclear-ui";
const char* const kApp = "MediaSimilarityFinder";

QString iniPath(const QString& root, const char* org) {
  return QDir(root).filePath(QString::fromLatin1(org) + QLatin1Char('/') + QLatin1String(kApp) +
                            QLatin1String(".ini"));
}

// Writes an INI directly, bypassing QSettings entirely, so the fixture is a
// real file on disk exactly like a pre-upgrade user's, and no in-process cache
// can exist. QSettings cannot be constructed without a QCoreApplication, which
// is the whole point: the plant phase must not have one.
bool plantIni(const QString& path, const QMap<QString, QString>& values) {
  const QFileInfo fi(path);
  if (!QDir().mkpath(fi.absolutePath())) return false;
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  QString currentGroup;
  for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
    const QString key = it.key();
    const int slash = key.lastIndexOf(QLatin1Char('/'));
    const QString group = slash < 0 ? QString() : key.left(slash);
    const QString leaf = slash < 0 ? key : key.mid(slash + 1);
    if (group != currentGroup) {
      currentGroup = group;
      if (!currentGroup.isEmpty()) {
        const QByteArray header = ("[" + currentGroup + "]\n").toUtf8();
        if (f.write(header) < 0) return false;
      }
    }
    // QByteArray values round-trip as base64 in INI; plain ASCII markers keep
    // the fixture readable and the assertions unambiguous.
    const QByteArray line = (leaf + "=" + it.value() + "\n").toUtf8();
    if (f.write(line) < 0) return false;
  }
  f.close();
  return QFileInfo::exists(path);
}
}  // namespace

int main(int argc, char** argv) {
  // User-visible CPU policy probe. The same 10-90 bounds must appear in the
  // widget and in the value handed to make_policy. QSpinBox enforces the range
  // for displayed values; the assertions below exercise the real slot path.
  if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "--cpu-policy") {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    // Isolated Ini settings (same init path as the app): without it the
    // settings backend has no identity and every read returns defaults.
    QTemporaryDir settingsDir;
    if (!settingsDir.isValid()) { std::cerr << "temp settings dir failed\n"; return 1; }
    QDir().mkpath(settingsDir.path() + "/settings");
    initAppSettings(settingsDir.path() + "/settings");
    MainWindow w;
    w.show();
    QApplication::processEvents();
    QSpinBox* cpu = w.findChild<QSpinBox*>("cpuSpin");
    if (!cpu) { std::cerr << "CPU spin box not found\n"; return 1; }
    if (cpu->minimum() != msf::kUserCpuPercentMin || cpu->maximum() != msf::kUserCpuPercentMax) {
      std::cerr << "CPU range is not 10-90\n";
      return 1;
    }
    struct CpuCase { int input; int expected; };
    const CpuCase cases[] = {
      {120, 90}, {100, 90}, {91, 90}, {90, 90}, {50, 50},
      {10, 10}, {9, 10}, {0, 10}, {-1, 10}, {-100, 10}
    };
    for (const CpuCase& c : cases) {
      cpu->setValue(c.input);
      QApplication::processEvents();
      if (cpu->value() != c.expected) {
        std::cerr << "CPU display mismatch for input " << c.input
                  << ": got " << cpu->value() << ", want " << c.expected << "\n";
        return 1;
      }
      // The corrective widget write must not leave the slot in a state that
      // refires on the next user input. Setting the already-normalized value
      // back must be a no-op for the displayed value.
      cpu->setValue(c.expected);
      QApplication::processEvents();
      if (cpu->value() != c.expected) {
        std::cerr << "CPU display changed after idempotent write\n";
        return 1;
      }
    }
    std::cout << "cpu_policy=ok checked=" << (int)(sizeof(cases) / sizeof(cases[0])) << "\n";
    // 0.9.4.96: preset combo is Maximum/High/Balanced/Minimum (Gaming renamed,
    // Custom removed); the CPU box is a read-only display, hidden by default;
    // the toolbar GPU checkbox is gone (Settings > General owns the switch).
    {
      QComboBox* preset = w.findChild<QComboBox*>("presetBox");
      if (!preset) { std::cerr << "preset combo not found\n"; return 1; }
      QStringList names;
      for (int i = 0; i < preset->count(); ++i) names << preset->itemText(i);
      if (names != QStringList({"Maximum", "High", "Balanced", "Minimum"})) {
        std::cerr << "preset items changed: " << names.join(",").toStdString() << "\n";
        return 1;
      }
      if (w.findChild<QCheckBox*>("gpuToggle")) {
        std::cerr << "toolbar GPU checkbox still present\n";
        return 1;
      }
      if (!w.testGpuOn()) { std::cerr << "GPU switch default is not ON\n"; return 1; }
      QSettings st;
      const QVariant prevShow = st.value("ui/showCpuPercent");
      st.setValue("ui/showCpuPercent", false);
      st.sync(); // flush so the fresh instance inside the apply slot reads it
      QMetaObject::invokeMethod(&w, "applyCpuPercentVisibility");
      QApplication::processEvents();
      if (cpu->isVisible()) {
        std::cerr << "CPU box visible with showCpuPercent=false\n";
        return 1;
      }
      st.setValue("ui/showCpuPercent", true);
      st.sync();
      const bool invoked = QMetaObject::invokeMethod(&w, "applyCpuPercentVisibility");
      if (!invoked) { std::cerr << "apply slot not invoked\n"; return 1; }
      QApplication::processEvents();
      if (!cpu->isVisible()) {
        std::cerr << "CPU box hidden with showCpuPercent=true\n";
        return 1;
      }
      if (prevShow.isValid()) st.setValue("ui/showCpuPercent", prevShow);
      else st.remove("ui/showCpuPercent");
      QMetaObject::invokeMethod(&w, "applyCpuPercentVisibility");
      QApplication::processEvents();
      // 0.9.4.98: Test-button visibility follows ui/showTestButton
      // (default ON); the toggle state itself is untouched.
      QPushButton* testBtn = w.findChild<QPushButton*>("testBtn");
      if (!testBtn) { std::cerr << "test button not found\n"; return 1; }
      const QVariant prevTest = st.value("ui/showTestButton");
      st.setValue("ui/showTestButton", false);
      st.sync();
      QMetaObject::invokeMethod(&w, "applyTestButtonVisibility");
      QApplication::processEvents();
      if (testBtn->isVisible()) {
        std::cerr << "test button visible with showTestButton=false\n";
        return 1;
      }
      st.setValue("ui/showTestButton", true);
      st.sync();
      QMetaObject::invokeMethod(&w, "applyTestButtonVisibility");
      QApplication::processEvents();
      if (!testBtn->isVisible()) {
        std::cerr << "test button hidden with showTestButton=true\n";
        return 1;
      }
      if (prevTest.isValid()) st.setValue("ui/showTestButton", prevTest);
      else st.remove("ui/showTestButton");
      QMetaObject::invokeMethod(&w, "applyTestButtonVisibility");
      QApplication::processEvents();
      // 0.9.4.149: merged Load/Re-verify slot (QToolButton + dropdown).
      // Pin layout membership and order: merged slot > Scan. The dropdown
      // must list Load on top and Re-verify below.
      QToolButton* loadSplit = w.findChild<QToolButton*>("loadSplitBtn");
      QPushButton* scanBtn = w.findChild<QPushButton*>("scan");
      if (!loadSplit || !scanBtn) { std::cerr << "toolbar buttons not found\n"; return 1; }
      QLayout* barLayout = loadSplit->parentWidget() ? loadSplit->parentWidget()->layout() : nullptr;
      if (!barLayout) { std::cerr << "toolbar has no layout\n"; return 1; }
      const int iLoad = barLayout->indexOf(loadSplit);
      const int iScan = barLayout->indexOf(scanBtn);
      if (iLoad < 0 || iScan < 0 || !(iLoad < iScan)) {
        std::cerr << "merged load slot not laid out before Scan\n"; return 1;
      }
      QMenu* splitMenu = loadSplit->menu();
      QAction* loadAct = w.findChild<QAction*>("loadSplitLoadAct");
      QAction* reverifyAct = w.findChild<QAction*>("loadSplitReverifyAct");
      if (!splitMenu || !loadAct || !reverifyAct ||
          splitMenu->actions().size() != 2 ||
          splitMenu->actions().at(0) != loadAct ||
          splitMenu->actions().at(1) != reverifyAct) {
        std::cerr << "load split menu must be [Load, Re-verify]\n"; return 1;
      }
      std::cout << "resource_ui=ok\n";
    }
    return 0;
  }
  // 0.9.4.151: Date-sort order regression gate (offscreen, synthetic rows).
  // Groups must order by newest member creation date: descending puts the
  // recently-added pair first, ascending reverses it. Birth times come from
  // real files (set via QFile::setFileTime); filesystems without birth-time
  // support SKIP instead of failing.
  if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "--date-sort") {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QTemporaryDir settingsDir;
    if (!settingsDir.isValid()) { std::cerr << "temp settings dir failed\n"; return 1; }
    QDir().mkpath(settingsDir.path() + "/settings");
    initAppSettings(settingsDir.path() + "/settings");
    QTemporaryDir td;
    if (!td.isValid()) { std::cerr << "temp fixture dir failed\n"; return 1; }
    const QString old1 = td.filePath("old_a.bmp"), old2 = td.filePath("old_b.bmp");
    const QString new1 = td.filePath("new_a.bmp"), new2 = td.filePath("new_b.bmp");
    for (const QString& p : {old1, old2, new1, new2}) {
      QFile f(p);
      if (!f.open(QIODevice::WriteOnly) || f.write("x") != 1) {
        std::cerr << "fixture write failed\n"; return 1;
      }
      f.close();
    }
    const QDateTime tOld = QDateTime::currentDateTime().addDays(-2);
    const QDateTime tNew = QDateTime::currentDateTime();
    bool timeOk = true;
    for (const QString& p : {old1, old2}) {
      QFile f(p);
      timeOk &= f.open(QIODevice::ReadWrite) &&
                f.setFileTime(tOld, QFileDevice::FileBirthTime);
      f.close();
    }
    for (const QString& p : {new1, new2}) {
      QFile f(p);
      timeOk &= f.open(QIODevice::ReadWrite) &&
                f.setFileTime(tNew, QFileDevice::FileBirthTime);
      f.close();
    }
    const qint64 bOld = QFileInfo(old1).birthTime().toMSecsSinceEpoch();
    const qint64 bNew = QFileInfo(new1).birthTime().toMSecsSinceEpoch();
    if (!timeOk || bOld <= 0 || bNew <= 0 || !(bOld < bNew)) {
      std::cout << "date_sort=skip (no birth-time support)\n";
      return 0;
    }
    MainWindow w2;
    QVector<BackendFile> files;
    for (const QString& p : {old1, old2, new1, new2}) {
      BackendFile f; f.path = p; f.size = 1; f.fpHex = QStringLiteral("1"); files << f;
    }
    QStringList rows;
    rows << old1 + "\t" + old2 + "\t100.0\t1" << new1 + "\t" + new2 + "\t100.0\t1";
    const bool invoked = QMetaObject::invokeMethod(
        &w2, "onResults", Qt::DirectConnection,
        Q_ARG(QVector<BackendFile>, files), Q_ARG(QStringList, rows));
    if (!invoked) { std::cerr << "onResults not invoked\n"; return 1; }
    QApplication::processEvents();
    auto* sortBox = w2.findChild<QComboBox*>("sortBox");
    auto* sortDir = w2.findChild<QComboBox*>("sortDirBox");
    if (!sortBox || !sortDir || sortBox->count() != 5) {
      std::cerr << "date sort combo missing\n"; return 1;
    }
    const auto firstPaths = [&]() {
      const QStringList roots = w2.testGroupRoots();
      if (roots.size() != 2) return QStringList{};
      const QStringList ps = w2.testGroupPathsFor(roots.at(0));
      return ps;
    };
    const QSet<QString> newSet({new1, new2}), oldSet({old1, old2});
    sortDir->setCurrentIndex(0); // descending
    sortBox->setCurrentIndex(4); // Date
    QApplication::processEvents();
    const QStringList descFirst = firstPaths();
    const bool descOk = descFirst.size() == 2 &&
        QSet<QString>(descFirst.begin(), descFirst.end()) == newSet;
    if (!descOk) {
      std::cerr << "date sort descending puts newest first\n"; return 1;
    }
    sortDir->setCurrentIndex(1); // ascending
    QApplication::processEvents();
    const QStringList ascFirst = firstPaths();
    const bool ascOk = ascFirst.size() == 2 &&
        QSet<QString>(ascFirst.begin(), ascFirst.end()) == oldSet;
    if (!ascOk) {
      std::cerr << "date sort ascending puts oldest first\n"; return 1;
    }
    std::cout << "date_sort=ok\n";
    return 0;
  }
  // 0.9.4.152: failed match persist must leave GUI == DB and recover on retry.
  // Loopback runs the real BackendSession in-process, so MSF_TEST_FAIL_PERSIST
  // reaches the worker (a real-backend e2e cannot set backend env vars).
  // Fast(A) targeted mode + invalidated baseline: the retry must rediscover
  // the pairs through the forced full verification, not report stale groups.
  if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "--persist-fail") {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QTemporaryDir settingsDir;
    if (!settingsDir.isValid()) { std::cerr << "temp settings dir failed\n"; return 1; }
    QDir().mkpath(settingsDir.path() + "/settings");
    initAppSettings(settingsDir.path() + "/settings");
    QTemporaryDir dataDir;
    if (!dataDir.isValid()) { std::cerr << "temp data dir failed\n"; return 1; }
    const auto writeBmp = [](const QString& p, unsigned seed) {
      QFile f(p);
      if (!f.open(QIODevice::WriteOnly)) return false;
      const int w = 8, h = 8, row = w * 3, img = row * h, fs = 54 + img;
      unsigned char hd[54] = {0};
      hd[0] = 'B'; hd[1] = 'M';
      hd[2] = (unsigned char)(fs & 0xFF); hd[3] = (unsigned char)((fs >> 8) & 0xFF);
      hd[10] = 54; hd[14] = 40; hd[18] = (unsigned char)w; hd[22] = (unsigned char)h;
      hd[26] = 1; hd[28] = 24;
      hd[34] = (unsigned char)(img & 0xFF); hd[35] = (unsigned char)((img >> 8) & 0xFF);
      if (f.write((const char*)hd, 54) != 54) return false;
      unsigned s = seed * 2654435761u + 1;
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          s = s * 1103515245u + 12345u;
          const char v = (char)((((s >> 16) & 0xFF) > 127) ? 255 : 0);
          if (f.write(&v, 1) != 1 || f.write(&v, 1) != 1 || f.write(&v, 1) != 1) return false;
        }
      return true;
    };
    const QString a1 = dataDir.filePath("a1.bmp"), a2 = dataDir.filePath("a2.bmp");
    const QString b1 = dataDir.filePath("b1.bmp"), b2 = dataDir.filePath("b2.bmp");
    if (!writeBmp(a1, 11) || !writeBmp(a2, 11) || !writeBmp(b1, 77) || !writeBmp(b2, 77)) {
      std::cerr << "fixture write failed\n"; return 1;
    }
    QSettings().setValue("ui/lastFolder", dataDir.path());
    QSettings().setValue("ui/analyzeMode", 1); // Fast(A): targeted verification
    QSettings().sync();
    MainWindow w2;
    w2.show();
    QApplication::processEvents();
    QTimer modalCloser;
    modalCloser.setInterval(250);
    QObject::connect(&modalCloser, &QTimer::timeout, [&]() {
      if (QWidget* m = QApplication::activeModalWidget()) m->close();
    });
    modalCloser.start();
    auto* scanBtn = w2.findChild<QPushButton*>("scan");
    auto* grid = w2.findChild<QListWidget*>("imgGrid");
    auto* splitBtn = w2.findChild<QToolButton*>("loadSplitBtn");
    if (!scanBtn || !grid || !splitBtn) { std::cerr << "scan controls not found\n"; return 1; }
    QElapsedTimer t;
    const auto pumpFor = [&](std::function<bool()> done, int ms, const char* what) {
      t.start();
      while (!done()) {
        QApplication::processEvents();
        QThread::msleep(50);
        if (t.elapsed() > ms) { std::cerr << "timeout: " << what << "\n"; return false; }
      }
      return true;
    };
    qputenv("MSF_TEST_FAIL_PERSIST", "1");
    scanBtn->click();
    QApplication::processEvents();
    if (!pumpFor([&] { return scanBtn->isEnabled(); }, 120000, "failed scan settled")) return 1;
    QApplication::processEvents();
    qunsetenv("MSF_TEST_FAIL_PERSIST");
    // Failure surfaced, never reported as success.
    bool failureShown = false;
    for (auto* lb : w2.findChildren<QLabel*>())
      if (lb->text().contains(QStringLiteral("match index save failed"))) failureShown = true;
    if (!failureShown) { std::cerr << "failure reason not delivered\n"; return 1; }
    // Reload finished (both buttons back): the screen must equal the DB.
    if (!pumpFor([&] { return splitBtn->isEnabled(); }, 30000, "post-failure reload")) return 1;
    QApplication::processEvents();
    msf::IndexPaths ip;
    if (!msf::IndexManager::resolve(
            msf::path_from_utf8(QApplication::applicationDirPath().toStdString()),
            msf::path_from_utf8(dataDir.path().toStdString()), ip)) {
      std::cerr << "index resolve failed\n"; return 1;
    }
    msf::Database db;
    const std::string dbFile = msf::path_to_utf8(ip.database);
    if (!db.open(dbFile) || !db.initialize()) { std::cerr << "index open failed\n"; return 1; }
    const auto dbPairs = db.loadMatches();
    const auto dbFiles = db.all();
    db.close();
    if (!dbPairs.empty()) { std::cerr << "unsaved matches persisted\n"; return 1; }
    if (dbFiles.size() != 4) { std::cerr << "file rows lost\n"; return 1; }
    if (grid->count() != 0) { std::cerr << "unsaved matches on screen\n"; return 1; }
    // Retry with a working persist: full verification rediscovers the pairs.
    scanBtn->click();
    QApplication::processEvents();
    if (!pumpFor([&] { return scanBtn->isEnabled(); }, 120000, "retry scan finished")) return 1;
    QApplication::processEvents();
    bool stillFailing = false;
    for (auto* lb : w2.findChildren<QLabel*>())
      if (lb->text().contains(QStringLiteral("match index save failed"))) stillFailing = true;
    if (stillFailing) { std::cerr << "failure text survives retry\n"; return 1; }
    if (grid->count() != 2) { std::cerr << "retry groups=" << grid->count() << "\n"; return 1; }
    std::cout << "persist_fail=ok\n";
    return 0;
  }
  // 0.9.4.153 P1-B: cancel-after-approval leaves the view consistent with
  // the stored set. Loopback runs the real session in-process: a real scan
  // with re-analyzed files is cancelled mid-analysis (large fixtures keep
  // analysis in flight past the fixed cancel delay with wide margin), then
  // the GUI must show exactly the DB's pruned set (no stale pairs), the
  // baseline must be invalidated, and the next scan must force full
  // verification and restamp. The worker-side CANCELLED|changed contract is
  // pinned in-process by crash_diagnostics_test §5; this pins the GUI side.
  if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "--cancel-consistency") {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QTemporaryDir settingsDir;
    if (!settingsDir.isValid()) { std::cerr << "temp settings dir failed\n"; return 1; }
    QDir().mkpath(settingsDir.path() + "/settings");
    initAppSettings(settingsDir.path() + "/settings");
    QTemporaryDir dataDir;
    if (!dataDir.isValid()) { std::cerr << "temp data dir failed\n"; return 1; }
    // 30 identical pairs of 512px noise BMPs. Every file is re-written
    // (fresh mtime => every file re-approved) and five spread members get
    // unique pixels. Analysis of the whole set takes seconds while the first
    // re-analyses are approved in ~100 ms, so a fixed-delay cancel always
    // lands mid-analysis with a non-empty changed set.
    const int kFiles = 30, kDim = 1024;
    auto writeNoise = [&](const QString& p, unsigned seed) {
      QFile f(p);
      if (!f.open(QIODevice::WriteOnly)) return false;
      const int row = kDim * 3, img = row * kDim, fs = 54 + img;
      unsigned char hd[54] = {0};
      hd[0] = 'B'; hd[1] = 'M';
      hd[2] = (unsigned char)(fs & 0xFF); hd[3] = (unsigned char)((fs >> 8) & 0xFF);
      hd[4] = (unsigned char)((fs >> 16) & 0xFF); hd[5] = (unsigned char)((fs >> 24) & 0xFF);
      hd[10] = 54; hd[14] = 40; hd[18] = (unsigned char)(kDim & 0xFF); hd[19] = (unsigned char)((kDim >> 8) & 0xFF);
      hd[22] = (unsigned char)(kDim & 0xFF); hd[23] = (unsigned char)((kDim >> 8) & 0xFF);
      hd[26] = 1; hd[28] = 24;
      hd[34] = (unsigned char)(img & 0xFF); hd[35] = (unsigned char)((img >> 8) & 0xFF);
      hd[36] = (unsigned char)((img >> 16) & 0xFF); hd[37] = (unsigned char)((img >> 24) & 0xFF);
      if (f.write((const char*)hd, 54) != 54) return false;
      unsigned s = seed * 2654435761u + 1;
      QByteArray line(row, 0);
      for (int y = 0; y < kDim; ++y) {
        char* d = line.data();
        for (int x = 0; x < row; ++x) {
          s = s * 1103515245u + 12345u;
          d[x] = (char)((((s >> 16) & 0xFF) > 127) ? 255 : 0);
        }
        if (f.write(line) != row) return false;
      }
      return true;
    };
    QStringList members;
    for (int i = 0; i < kFiles; ++i) {
      const QString a = dataDir.filePath(QString("w%1_a.bmp").arg(i));
      const QString b = dataDir.filePath(QString("w%1_b.bmp").arg(i));
      if (!writeNoise(a, 1000 + (unsigned)i) || !writeNoise(b, 1000 + (unsigned)i)) {
        std::cerr << "fixture write failed\n"; return 1;
      }
      members << a << b;
    }
    QSettings().setValue("ui/lastFolder", dataDir.path());
    QSettings().setValue("ui/analyzeMode", 1); // Live (A)
    QSettings().sync();
    MainWindow w2;
    w2.show();
    QApplication::processEvents();
    QTimer modalCloser;
    modalCloser.setInterval(250);
    QObject::connect(&modalCloser, &QTimer::timeout, [&]() {
      if (QWidget* m = QApplication::activeModalWidget()) m->close();
    });
    modalCloser.start();
    auto* scanBtn = w2.findChild<QPushButton*>("scan");
    auto* grid = w2.findChild<QListWidget*>("imgGrid");
    auto* splitBtn = w2.findChild<QToolButton*>("loadSplitBtn");
    if (!scanBtn || !grid || !splitBtn) { std::cerr << "scan controls not found\n"; return 1; }
    QElapsedTimer t;
    const auto pumpFor = [&](std::function<bool()> done, int ms, const char* what) {
      t.start();
      while (!done()) {
        QApplication::processEvents();
        QThread::msleep(50);
        if (t.elapsed() > ms) { std::cerr << "timeout: " << what << "\n"; return false; }
      }
      return true;
    };
    const auto dbFile = [&]() {
      msf::IndexPaths ipx;
      if (!msf::IndexManager::resolve(
              msf::path_from_utf8(QApplication::applicationDirPath().toStdString()),
              msf::path_from_utf8(dataDir.path().toStdString()), ipx))
        return QString();
      return QString::fromStdString(msf::path_to_utf8(ipx.database));
    }();
    if (dbFile.isEmpty()) { std::cerr << "index resolve failed\n"; return 1; }
    // Baseline scan: completes and stamps.
    scanBtn->click();
    QApplication::processEvents();
    if (!pumpFor([&] { return scanBtn->isEnabled(); }, 300000, "baseline scan finished")) return 1;
    QApplication::processEvents();
    if (grid->count() != kFiles) { std::cerr << "baseline groups=" << grid->count() << "\n"; return 1; }
    // Rewrite every _a file with unique pixels (fresh mtime re-approves
    // everything; unique pixels drop every pair). Analysis of the whole set
    // takes seconds while detection lands in ~100 ms, so the fixed-delay
    // cancel always lands mid-analysis with a non-empty changed set.
    QStringList changed;
    for (int i = 0; i < kFiles; ++i) {
      const QString p = dataDir.filePath(QString("w%1_a.bmp").arg(i));
      if (!writeNoise(p, 9000 + (unsigned)i)) { std::cerr << "change write failed\n"; return 1; }
      changed << p;
    }
    QSettings().sync();
    auto* backend = w2.findChild<BackendClient*>();
    if (!backend) { std::cerr << "backend not found\n"; return 1; }
    // Cancel on the first analyzed file: progress proves analysis completed,
    // and every file changed this scan, so the changed set is non-empty from
    // here on. No wall-clock race in either direction.
    std::atomic<bool> progressed{false};
    QObject::connect(backend, &BackendClient::progress, &app,
                     [&](int p, QString) { if (p > 0) progressed = true; },
                     Qt::DirectConnection);
    QObject::connect(backend, &BackendClient::progressCount, &app,
                     [&](qulonglong d, qulonglong) { if (d > 0) progressed = true; },
                     Qt::DirectConnection);
    scanBtn->click();
    QApplication::processEvents();
    if (!pumpFor([&] { return !scanBtn->isEnabled(); }, 30000, "cancel scan started")) return 1;
    if (!pumpFor([&] { return progressed.load(); }, 120000, "analysis in progress")) return 1;
    if (scanBtn->isEnabled()) { std::cerr << "scan finished before cancel\n"; return 1; }
    if (!QMetaObject::invokeMethod(&w2, "cancelScan", Qt::DirectConnection)) {
      std::cerr << "cancel not invoked\n"; return 1;
    }
    QApplication::processEvents();
    if (!pumpFor([&] { return scanBtn->isEnabled(); }, 180000, "cancelled scan settled")) return 1;
    QApplication::processEvents();
    if (!pumpFor([&] { return splitBtn->isEnabled(); }, 30000, "post-cancel reload")) return 1;
    QApplication::processEvents();
    // GUI must equal the pruned stored set exactly (any stale pair fails).
    // Group maps (path -> sorted members) from both sides must be identical.
    const auto guiGroups = [&]() {
      QHash<QString, QStringList> out;
      for (const QString& r : w2.testGroupRoots()) {
        QStringList ps = w2.testGroupPathsFor(r);
        std::sort(ps.begin(), ps.end());
        for (const QString& p : ps) out[p] = ps;
      }
      return out;
    };
    const auto dbGroups = [&]() {
      QHash<QString, QStringList> out;
      msf::Database b;
      if (!b.open(dbFile.toStdString()) || !b.initialize()) return out;
      std::unordered_map<std::string, std::string> parent;
      const auto find = [&](const std::string& p, auto&& self) -> std::string {
        auto it = parent.find(p);
        if (it == parent.end() || it->second == p) return p;
        return it->second = self(it->second, self);
      };
      for (const auto& m : b.loadMatches()) {
        parent.emplace(m.left, m.left);
        parent.emplace(m.right, m.right);
        const std::string a = find(m.left, find), c = find(m.right, find);
        if (a != c) parent[a] = c;
      }
      b.close();
      QHash<QString, QSet<QString>> acc;
      for (const auto& kv : parent) {
        const QString p = QString::fromStdString(kv.first);
        const QString r = QString::fromStdString(find(kv.first, find));
        acc[r].insert(p);
      }
      for (auto it = acc.begin(); it != acc.end(); ++it) {
        const QSet<QString> members = it.value(); // named local: never range-construct from a temporary
        QStringList ps(members.begin(), members.end());
        std::sort(ps.begin(), ps.end());
        for (const QString& p : ps) out[p] = ps;
      }
      return out;
    };
    if (guiGroups() != dbGroups()) { std::cerr << "gui/db group mismatch\n"; return 1; }
    if (grid->count() != 0) { std::cerr << "stale view not reloaded\n"; return 1; }
    // Every pair touched a changed file, so the stored set is empty: all
    // changed members are singletons on screen (a missed reload would still
    // show 30 groups here).
    for (const QString& p : changed) {
      if (!w2.testGroupPathsFor(p).isEmpty()) { std::cerr << "stale pair on screen\n"; return 1; }
    }
    // Baseline invalidated by the cancel.
    {
      msf::Database b;
      if (!b.open(dbFile.toStdString()) || !b.initialize()) { std::cerr << "index reopen failed\n"; return 1; }
      const bool empty = b.matchBaseline().empty();
      b.close();
      if (!empty) { std::cerr << "baseline not invalidated on cancel\n"; return 1; }
    }
    // Next scan must force full verification and restamp.
    const QString logPath =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/msf_scan.log");
    const qint64 logBefore = QFileInfo(logPath).size();
    scanBtn->click();
    QApplication::processEvents();
    if (!pumpFor([&] { return scanBtn->isEnabled(); }, 300000, "post-cancel scan finished")) return 1;
    QApplication::processEvents();
    bool fullPass = false;
    {
      QFile lf(logPath);
      if (lf.open(QIODevice::ReadOnly)) {
        if (logBefore > 0) lf.seek(logBefore);
        fullPass = QString::fromUtf8(lf.readAll()).contains(QStringLiteral("matchBaseline=forceFullPass"));
      }
    }
    if (!fullPass) { std::cerr << "no forced full verification\n"; return 1; }
    {
      msf::Database b;
      if (!b.open(dbFile.toStdString()) || !b.initialize()) { std::cerr << "restamp reopen failed\n"; return 1; }
      const bool stamped = b.matchBaseline() == msf::MediaSearchEngine::kEngineVersion;
      b.close();
      if (!stamped) { std::cerr << "baseline not restamped\n"; return 1; }
    }
    if (guiGroups() != dbGroups()) { std::cerr << "post-rescan gui/db mismatch\n"; return 1; }
    for (const QString& p : changed) {
      if (!w2.testGroupPathsFor(p).isEmpty()) { std::cerr << "changed pair resurrected\n"; return 1; }
    }
    std::cout << "cancel_consistency=ok\n";
    return 0;
  }
  // Translation-table regression gate.
  //
  // trStr() ends with `return QString::fromUtf8(key)`, so any key used in the UI
  // but missing from the table renders as its own camelCase identifier in front
  // of the user. That is exactly what happened: colFile, colDate and viewGrid
  // were referenced by the file-list headers and the grid toggle tooltip, never
  // defined, and the header showed the literal text "colFile" / "colDate".
  //
  // Detection uses the Korean value, not both languages: a key is an ASCII
  // camelCase identifier, so a genuine Korean translation can never equal it,
  // while the fallback returns the key unchanged in both languages. The English
  // value is only checked for emptiness, because a legitimate entry can
  // coincide with its own key -- "files" is a real column header whose English
  // label is "files". Comparing English against the key would flag that as a
  // false positive, and "fixing" it by capitalizing would be a cosmetic change
  // to working UI.
  if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "--tr-keys") {
    static const char* const kKeys[] = {
      // file list view: headers and the view-mode toggles
      "colFile", "colDate", "viewGrid", "viewList",
      "similarity", "resolution", "format", "fileSize",
      // detail form rows
      "fileName", "fullPath", "modified", "created",
      // group tree headers
      "group", "files", "pairs",
      // view-mode combo entries
      "viewXL", "viewL", "viewM", "viewS", "viewDetails", "viewTiles",
      // S4 detailed logging + execution strategy (renamed off the bench* keys;
      // a missing entry would render the raw key in the toolbar/dialog)
      "detailLogToggle", "detailLogTip", "detailLogTitle", "detailLogSaved",
      "detailLogSaveFail", "detailLogParseFail", "detailLogSave", "detailLogClose", "detailLogState",
      "detailLogDone", "detailLogStopped", "detailLogOff",
      "strategyModeAuto", "strategyModeCpu", "strategyModeGpu", "strategyModeTip",
      "searchLog",
      // settings dialog (Show Detailed Logs visibility option)
      "showDetailLog",
      // settings dialog (toolbar CPU display + GPU acceleration switches)
      "showCpuPercent", "monitorGpu",
      // media scope dropdown labels
      "kindImages", "kindVideos", "kindAll",
      // settings dialog (Test button visibility switch)
      "showTestButton",
      // detailed-log result dialog rows
      "detailLogWall", "detailLogFiles", "detailLogImages", "detailLogVideos",
      "detailLogCpuUse", "detailLogCpuProc", "detailLogCpuSys",
      "detailLogMemMax", "detailLogGpuDuty", "detailLogIo",
      "detailLogMatches", "detailLogSlow", "detailLogFailed", "detailLogFailedShort",
      // user-facing search summary (per-kind scanned/analyzed/throughput plus
      // the duplicate groups/files/pairs split)
      "detailLogSummary", "detailLogDuration", "detailLogDupGroups",
      "detailLogDupFiles", "detailLogDupPairs", "detailLogThroughput",
      // 0.9.4.119: file-line rate bases (overall = scanned/wall, new =
      // analyzed/wall) so the two numbers can never be confused again
      "detailLogRateOverall", "detailLogRateNew",
      "detailLogTotal", "detailLogTimeSplit", "detailLogMatching",
      // fingerprint-phase live progress (indeterminate bar + file/byte text)
      "fpProgress",
      // backend availability guard (P3 supervisor states)
      "backendDown",
      // left summary panel rows: total / read-complete / index-complete split
      "readDone", "indexDone",
      // left summary pair-verify row (total/confirmed)
      "sumVerify",
      // 0.9.4.106: 100.0% badge meaning (identical content vs perceptual)
      "identicalContent", "perceptualMatch",
      // 0.9.4.108: detailed-log summary header rows (mode + scanned folder)
      "summaryMode", "summaryFolder",
      // 0.9.4.132: Load-only completion message
      "loadDone",
      // 0.9.4.134: fast-rescan setting + forced duplicate re-verification
      "fastRescan", "fastRescanTip",
      "reverify", "reverifyTip", "reverifyRunning", "reverifyWait",
      "reverifyWaitTitle", "reverifyDone",
      // 0.9.4.135: Test Mode difference report (Yes/No adoption)
      "testDiffTitle", "testDiffBody", "testNoDiff", "testNotApplied",
      "testApplying", "testApplied", "testApplyFailed",
      // 0.9.4.136: preview-size gauge (1.0x marker/drag ratio)
      "zoomTip",
      // 0.9.4.139: split sort (type + direction) with the new group-size order
      "sortSize", "sortDesc", "sortAsc",
      // 0.9.4.150: date sort (creation date, newest first by default)
      "sortDate",
      // 0.9.4.143: configurable recent-folder count (Favorites)
      "recentLimitLabel", "recentLimitTip",
    };
    int missing = 0;
    for (const char* k : kKeys) {
      const QString ko = trStr(UiLang::Ko, k);
      const QString en = trStr(UiLang::En, k);
      if (ko == QString::fromUtf8(k)) {
        std::cerr << "trStr(ko) has no entry for key '" << k << "'\n";
        ++missing;
      }
      if (en.trimmed().isEmpty()) {
        std::cerr << "trStr(en) returned empty text for key '" << k << "'\n";
        ++missing;
      }
      if (ko == en && ko.trimmed().isEmpty()) {
        std::cerr << "key '" << k << "' resolved to empty text in both languages\n";
        ++missing;
      }
    }
    if (missing) { std::cout << "tr_keys=failed " << missing << "\n"; return 1; }
    std::cout << "tr_keys=ok checked=" << (int)(sizeof(kKeys) / sizeof(kKeys[0])) << "\n";
    return 0;
  }
  if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "--ref-order") {
    QStringList paths({"a", "b", "c", "d"});
    QHash<QString,qulonglong> pix, siz;
    pix["a"] = 100; siz["a"] = 10;
    pix["b"] = 400; siz["b"] = 5;
    pix["c"] = 400; siz["c"] = 9;
    pix["d"] = 0; siz["d"] = 99;
    MainWindow::sortTiedReferencePaths(paths, pix, siz);
    if (paths != QStringList({"c", "b", "a", "d"})) { std::cerr << "bad reference order\n"; return 1; }
    std::cout << "reference_order=ok\n"; return 0;
  }
  // Migration probes run before the MainWindow probe argument check because
  // they never build a window: they only exercise the settings bootstrap.
  if (argc == 3) {
    const QString mode = QString::fromLocal8Bit(argv[1]);
    const QString dir = QString::fromLocal8Bit(argv[2]);
    const QString legacyIni = iniPath(dir, kLegacyOrg);
    const QString newIni = iniPath(dir, kNewOrg);
    qputenv("QT_QPA_PLATFORM", "offscreen");

    if (mode == "--migrate-plant" || mode == "--migrate-plant-new-only") {
      // A plant phase must start from a known-clean slate: a leftover file
      // from an earlier run would otherwise make the fixture assert something
      // that is no longer true. Clearing here is what makes the phase
      // repeatable instead of single-use.
      QDir root(dir);
      if (root.exists()) root.removeRecursively();
      if (QFileInfo::exists(newIni)) { std::cerr << "new ini must not exist yet\n"; return 1; }
      if (mode == "--migrate-plant") {
        // No QApplication: this process only plants a file on disk, so nothing
        // can be served from an in-process QSettings cache later.
        if (!plantIni(legacyIni, {{"ui/mainGeom", "planted-geom"},
                                   {"ui/splitter", "planted-splitter"},
                                   {"ui/language", "en"},
                                   {"monitor/thresholdPercent", "42"},
                                   {"monitor/stableSeconds", "7"}})) {
          std::cerr << "plant failed\n"; return 1;
        }
        std::cout << "migrate_plant=ok\n"; return 0;
      }
      if (!plantIni(newIni, {{"ui/mainGeom", "new-value"},
                             {"ui/splitter", "new-splitter"}})) {
        std::cerr << "plant new failed\n"; return 1;
      }
      if (!plantIni(legacyIni, {{"ui/mainGeom", "legacy-value"},
                                 {"ui/splitter", "legacy-splitter"}})) {
        std::cerr << "plant legacy failed\n"; return 1;
      }
      std::cout << "migrate_plant_both=ok\n"; return 0;
    }
    QApplication app(argc, argv);
    // Every verify phase goes through the real bootstrap, which is what runs
    // the migration. The plant phases deliberately do not.
    initAppSettings(dir);
    if (mode == "--migrate-verify") {
      if (QFileInfo::exists(legacyIni)) { std::cerr << "legacy ini not cleaned up\n"; return 1; }
      if (!QFileInfo::exists(newIni)) { std::cerr << "new ini not created\n"; return 1; }
      // The active identity must be the product name, and the settings must
      // be readable from the new location with the values intact.
      if (QCoreApplication::organizationName() != QLatin1String(kNewOrg)) {
        std::cerr << "wrong organization: " << QCoreApplication::organizationName().toStdString() << "\n"; return 1;
      }
      QSettings st;
      st.sync();
      if (st.status() != QSettings::NoError) { std::cerr << "migrated settings unreadable\n"; return 1; }
      if (st.value("ui/mainGeom").toString() != "planted-geom") {
        std::cerr << "mainGeom not migrated\n"; return 1;
      }
      if (st.value("ui/splitter").toString() != "planted-splitter") {
        std::cerr << "splitter not migrated\n"; return 1;
      }
      if (st.value("ui/language").toString() != "en") { std::cerr << "language not migrated\n"; return 1; }
      if (st.value("monitor/thresholdPercent").toInt() != 42) { std::cerr << "threshold not migrated\n"; return 1; }
      if (st.value("monitor/stableSeconds").toInt() != 7) { std::cerr << "stableSeconds not migrated\n"; return 1; }
      if (st.fileName() != QDir::toNativeSeparators(newIni) &&
          QFileInfo(st.fileName()) != QFileInfo(newIni)) {
        std::cerr << "active settings file is not the new location: " << st.fileName().toStdString() << "\n"; return 1;
      }
      // Re-running the bootstrap must be a no-op, not a second overwrite.
      initAppSettings(dir);
      if (QFileInfo::exists(legacyIni)) { std::cerr << "idempotence broke: legacy reappeared\n"; return 1; }
      std::cout << "migrate_verify=ok\n"; return 0;
    }
    if (mode == "--migrate-none-verify") {
      // Same repeatable-start discipline as the plant phases: a leftover file
      // would make "no legacy ini" true by accident rather than by design.
      // The QApplication already created above is reused; creating a second one
      // here would tear down Qt state that the outer instance still owns.
      {
        QDir root(dir);
        if (root.exists()) root.removeRecursively();
        QDir().mkpath(dir);
      }
      initAppSettings(dir);
      if (QFileInfo::exists(legacyIni)) { std::cerr << "legacy ini must not be created\n"; return 1; }
      if (QCoreApplication::organizationName() != QLatin1String(kNewOrg)) {
        std::cerr << "wrong organization\n"; return 1;
      }
      // A fresh QSettings in the new location must be writable, which is what
      // proves the new identity resolves to a real path.
      QSettings st;
      st.setValue("ui/language", "ko");
      st.sync();
      if (st.status() != QSettings::NoError) { std::cerr << "new ini not writable\n"; return 1; }
      if (!QFileInfo::exists(newIni)) { std::cerr << "new ini not created\n"; return 1; }
      if (QFileInfo::exists(legacyIni)) { std::cerr << "legacy dir must not be created\n"; return 1; }
      std::cout << "migrate_none_verify=ok\n"; return 0;
    }
    if (mode == "--migrate-both-verify") {
      // The new location must win and the legacy file must be preserved.
      if (!QFileInfo::exists(legacyIni)) { std::cerr << "legacy ini was destroyed\n"; return 1; }
      QSettings st;
      st.sync();
      if (st.value("ui/mainGeom").toString() != "new-value") {
        std::cerr << "new location did not win\n"; return 1;
      }
      std::cout << "migrate_both_verify=ok\n"; return 0;
    }
    std::cerr << "unknown migration mode\n"; return 2;
  }
  if (argc != 5) { std::cerr << "usage: ui_settings_test --probe-write|--probe-verify <dir> <W> <H>\n"; return 2; }
  const QString mode = QString::fromLocal8Bit(argv[1]);
  const QString dir = QString::fromLocal8Bit(argv[2]);
  const int W = QString::fromLocal8Bit(argv[3]).toInt();
  const int H = QString::fromLocal8Bit(argv[4]).toInt();
  if (W <= 0 || H <= 0) { std::cerr << "bad size\n"; return 2; }
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QDir().mkpath(dir);
  QApplication app(argc, argv);
  initAppSettings(dir);
  if (mode == "--probe-write") {
    { MainWindow w; w.show(); w.resize(W, H); QApplication::processEvents(); }
    QSettings st; st.sync();
    st.sync();
    if (st.status() != QSettings::NoError) { std::cerr << "settings status error\n"; return 1; }
    const QString ini = st.fileName();
    if (ini.isEmpty() || !QFileInfo::exists(ini)) { std::cerr << "no settings file on disk\n"; return 1; }
    if (!st.contains("ui/mainGeom") || !st.contains("ui/splitter")) { std::cerr << "settings keys missing\n"; return 1; }
    std::cout << "ui_settings_write=ok\n"; return 0;
  }
  if (mode == "--probe-verify") {
    (void)W; // width intentionally unchecked, see below
    MainWindow w;
    w.show(); QApplication::processEvents();
    const QSize sz = w.size();
    QSettings st;
    if (st.status() != QSettings::NoError || !st.contains("ui/mainGeom")) { std::cerr << "settings unreadable\n"; return 1; }
    // Height is asserted exactly. Width is deliberately NOT asserted: the
    // offscreen test screen is 800x800 while the window's layout minimum is
    // wider, so Qt legitimately forces the width up (and would clamp a wider
    // window down) ??a test-environment artifact, not an app bug. On a real
    // screen both dimensions round-trip via restoreGeometry (verified: a
    // no-op restore would leave the 880 default height).
    if (sz.height() != H) {
      std::cerr << "geometry not restored: got height " << sz.height()
                << " want " << H << "\n"; return 1;
    }
    std::cout << "ui_settings_verify=ok\n"; return 0;
  }
  std::cerr << "unknown mode\n"; return 2;
}





