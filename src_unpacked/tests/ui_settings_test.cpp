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
#include <QSpinBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
      "detailLogSaveFail", "detailLogSave", "detailLogClose", "detailLogState",
      "detailLogDone", "detailLogStopped", "detailLogOff",
      "strategyModeAuto", "strategyModeCpu", "strategyModeGpu", "strategyModeTip",
      "searchLog",
      // settings dialog (Show Detailed Logs visibility option)
      "showDetailLog",
      // detailed-log result dialog rows
      "detailLogWall", "detailLogFiles", "detailLogImages", "detailLogVideos",
      "detailLogCpuUse", "detailLogMemMax", "detailLogGpuDuty", "detailLogIo",
      "detailLogMatches", "detailLogSlow",
      // user-facing search summary (per-kind scanned/analyzed/throughput plus
      // the duplicate groups/files/pairs split)
      "detailLogSummary", "detailLogDuration", "detailLogDupGroups",
      "detailLogDupFiles", "detailLogDupPairs", "detailLogThroughput",
      "detailLogTotal", "detailLogTimeSplit", "detailLogMatching",
      // fingerprint-phase live progress (indeterminate bar + file/byte text)
      "fpProgress",
      // backend availability guard (P3 supervisor states)
      "backendDown",
      // left summary panel rows: total / read-complete / index-complete split
      "readDone", "indexDone",
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
