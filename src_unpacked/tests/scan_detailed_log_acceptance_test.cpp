// S4 functional Detailed Logs acceptance test.
//
// Drives the REAL MainWindow path offscreen — window, [Detailed Logs] toggle
// at its default ON, strategy AUTO default, Search/Update start, ScanWorker,
// MediaSearchEngine::scan(), TelemetryRecorder(UserDiagnostic) — and asserts
// the produced telemetry, not just that a dialog appeared:
//
// - telemetry JSON exists and meta.completed is true
// - TelemetryPurpose is UserDiagnostic (never Benchmark)
// - summary scanned/analyzed/unchanged agree with the real scan
// - images.count agrees with the fixture; GUI shows exactly 1 group
// - dataset fingerprint equals the fingerprint of the scanned root
// - cancelled/failed are false (no zero-masked states)
// - the Detailed Logs dialog actually opened (result path reached)
// - 0.9.4.112 (S4 save/exit acceptance): the dialog's REAL Save button is
//   clicked, the file picker is driven, the record is written to disk and
//   byte-parses equal to the telemetry record; the status shows the path;
//   an unwritable path must report failure (never a false "saved").
//
// What this does NOT prove (recorded, not claimed): on-screen visual
// rendering of the dialog (headless environment).

#include "mainwindow.h"
#include "scan_worker.h"
#include "dataset_fingerprint.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTextBrowser>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>

#include <filesystem>
#include <fstream>
#include <iostream>

// Minimal 8x8 24-bit BMP. Identical files hash identically -> distance 0,
// so 4 copies form exactly 1 group.
static void bmp(const std::filesystem::path& p) {
  std::ofstream f(p, std::ios::binary);
  const int w = 8, h = 8, row = w * 3, img = row * h, fs = 54 + img;
  unsigned char hd[54] = {0};
  hd[0] = 'B'; hd[1] = 'M';
  hd[2] = (unsigned char)(fs & 0xFF); hd[3] = (unsigned char)((fs >> 8) & 0xFF);
  hd[10] = 54; hd[14] = 40; hd[18] = (unsigned char)w; hd[22] = (unsigned char)h;
  hd[26] = 1; hd[28] = 24;
  hd[34] = (unsigned char)(img & 0xFF); hd[35] = (unsigned char)((img >> 8) & 0xFF);
  f.write((const char*)hd, 54);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const unsigned char v = (x >= 4) ? 255 : 0;
      f.put((char)v); f.put((char)v); f.put((char)v);
    }
}

static int fail(const char* what) { std::cerr << "FAIL: " << what << "\n"; return 1; }

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  namespace fs = std::filesystem;
  std::error_code ec;
  auto d = fs::temp_directory_path() / "msf_detailed_log_accept";
  fs::remove_all(d, ec);
  fs::create_directories(d / "media", ec);
  fs::create_directories(d / "settings", ec);
  for (int i = 0; i < 4; ++i)
    bmp(d / "media" / ("dup" + std::to_string(i) + ".bmp"));
  const std::string root = (d / "media").string();
  initAppSettings(QString::fromStdString((d / "settings").string()));
  QSettings().setValue("ui/lastFolder", QString::fromStdString(root));
  QSettings().sync();

  MainWindow w;
  w.show();
  QApplication::processEvents();

  // [Detailed Logs] must be ON by default; strategy must be AUTO.
  auto* logTgl = w.findChild<QCheckBox*>("logTgl");
  if (!logTgl || !logTgl->isChecked()) return fail("logTgl missing or off");
  auto* strategy = w.findChild<QComboBox*>("strategyBox");
  if (!strategy || strategy->currentIndex() != 0) return fail("strategy not AUTO");

  // The end-of-scan detailed-log dialog is modal: dismiss it on a timer.
  // Seeing it proves the GUI result path was reached. 0.9.4.112: the REAL
  // Save button is clicked once; the file picker it opens is driven by
  // selecting the target path and accepting, so the production save path
  // (button -> picker -> file write -> status) runs end to end.
  const auto savePath = d / "saved-log.json";
  bool dialogSeen = false;
  bool saveClicked = false;
  bool fileDialogHandled = false;
  bool statusShown = false;
  // 0.9.4.119: the dialog's rendered text is captured so the clarified
  // throughput bases (analysis-stage label + two named file-line rates) are
  // pinned by the acceptance path, not just by code review.
  QString dialogText;
  int fileDialogTicks = 0;
  QTimer closer;
  closer.setInterval(250);
  QObject::connect(&closer, &QTimer::timeout, [&]() {
    QWidget* m = QApplication::activeModalWidget();
    if (!m) return;
    if (auto* fd = qobject_cast<QFileDialog*>(m)) {
      // selectFile() changes the directory but SKIPS the name when the name
      // edit has focus (the save dialog's default state), and the dialog
      // reports paths with forward slashes. Set the name edit directly and
      // accept only once the dialog itself reports the target path.
      const QString want =
          QDir::fromNativeSeparators(QString::fromStdString(savePath.string()));
      fd->selectFile(want);
      if (auto* le = fd->findChild<QLineEdit*>()) le->setText(want);
      const QString got =
          QDir::fromNativeSeparators(fd->selectedFiles().value(0));
      if (QString::compare(got, want, Qt::CaseInsensitive) != 0) {
        if (++fileDialogTicks > 20) { // safety valve: 5 s of re-applying
          std::cerr << "file dialog stuck: selected=" << got.toStdString()
                    << " want=" << want.toStdString() << " (forcing accept)\n";
          fileDialogHandled = true;
          QMetaObject::invokeMethod(fd, "accept");
        }
        return;
      }
      fileDialogHandled = true;
      QMetaObject::invokeMethod(fd, "accept");
      return;
    }
    dialogSeen = true;
    if (dialogText.isEmpty()) {
      if (auto* tb = m->findChild<QTextBrowser*>()) dialogText = tb->toPlainText();
    }
    if (!saveClicked) {
      for (auto* b : m->findChildren<QPushButton*>()) {
        if (b->text().contains(QStringLiteral("JSON"))) {
          saveClicked = true;
          // The click opens the file picker's nested exec. It must run OUTSIDE
          // this timer's emission: Qt does not re-enter the same timer while
          // its handler is on the stack, so the closer could never handle the
          // file dialog from inside the click. A zero-delay singleShot runs
          // from the event loop instead; when click() returns, the save
          // routine has just run, so the status text is captured before any
          // later scan-finish message can overwrite it.
          QTimer::singleShot(0, b, [&, b]() {
            b->click();
            // The status shows the path the file dialog returned (forward
            // slashes); normalize both sides before searching.
            const QString want =
                QDir::fromNativeSeparators(QString::fromStdString(savePath.string()));
            for (auto* lb : w.findChildren<QLabel*>())
              if (QDir::fromNativeSeparators(lb->text()).contains(want, Qt::CaseInsensitive))
                statusShown = true;
          });
          return;
        }
      }
    }
    m->close();
  });
  closer.start();

  auto* scan = w.findChild<QPushButton*>("scan");
  if (!scan) return fail("no scan button");
  scan->click();
  QApplication::processEvents();
  if (scan->isEnabled()) return fail("scan did not start");
  QElapsedTimer t;
  t.start();
  while (!scan->isEnabled()) {
    QApplication::processEvents();
    QThread::msleep(50);
    if (t.elapsed() > 120000) return fail("scan did not finish");
  }
  QApplication::processEvents();
  if (!dialogSeen) return fail("detailed-log dialog never opened");
  // The save flow (button -> file picker -> write) runs inside the modal
  // exec; wait boundedly for it to complete.
  {
    QElapsedTimer saveWait;
    saveWait.start();
    while (!saveClicked && saveWait.elapsed() < 30000) {
      QApplication::processEvents();
      QThread::msleep(50);
    }
  }

  // GUI result consistency: exactly 1 group rendered.
  auto* tree = w.findChild<QTreeWidget*>("imgTree");
  if (!tree || tree->topLevelItemCount() != 1) return fail("gui groups != 1");

  // Telemetry produced by the real scan behind that dialog.
  const std::string js = w.telemetryJsonForTest();
  if (js.empty()) return fail("no telemetry json");
  if (js.find("\"purpose\":\"UserDiagnostic\"") == std::string::npos)
    return fail("purpose is not UserDiagnostic");
  if (js.find("\"purpose\":\"Benchmark\"") != std::string::npos)
    return fail("purpose is Benchmark");
  if (js.find("\"completed\":true") == std::string::npos)
    return fail("meta.completed is not true");
  if (js.find("\"cancelled\":false") == std::string::npos ||
      js.find("\"failed\":false") == std::string::npos)
    return fail("cancelled/failed states unclear");

  const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(js));
  if (!doc.isObject()) return fail("telemetry is not a JSON object");
  const QJsonObject root_o = doc.object();
  const QJsonObject summary = root_o["summary"].toObject();
  // 4 identical BMPs: scanned 4, analyzed 4, unchanged 0 — must match the
  // real scan, never a masked zero.
  if (summary["scanned"].toInt(-1) != 4) return fail("summary.scanned != 4");
  if (summary["analyzed"].toInt(-1) != 4) return fail("summary.analyzed != 4");
  if (summary["unchanged"].toInt(-1) != 0) return fail("summary.unchanged != 0");
  if (root_o["images"].toObject()["count"].toInt(-1) != 4)
    return fail("images.count != 4");
  if (root_o["matches"].toObject()["groups"].toInt(-1) != 1)
    return fail("matches.groups != 1 (GUI shows 1)");

  // Dataset identity must be the scanned root's, not a placeholder.
  const QJsonObject dataset = root_o["meta"].toObject()["dataset"].toObject();
  const msf::DatasetFingerprint fp = msf::computeDatasetFingerprint(root);
  if (fp.state != "measured") return fail("fixture fingerprint not measured");
  if (dataset["state"].toString() != QStringLiteral("measured"))
    return fail("telemetry dataset.state is not measured");
  if (dataset["fingerprint"].toString().toStdString() != fp.fingerprint)
    return fail("telemetry fingerprint != root fingerprint");

  // ---- 0.9.4.112 (S4 save/exit acceptance) ----
  if (!saveClicked) return fail("detailed-log Save button never appeared");
  if (!fileDialogHandled) return fail("save file dialog never appeared");
  std::error_code sec;
  if (!fs::exists(savePath, sec)) return fail("saved detailed log file missing");
  std::ifstream in(savePath, std::ios::binary);
  const std::string saved((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
  const QJsonDocument savedDoc = QJsonDocument::fromJson(QByteArray::fromStdString(saved));
  if (!savedDoc.isObject() || savedDoc != doc)
    return fail("saved record != telemetry record");
  if (!statusShown) return fail("status does not show the saved path");
  // ---- 0.9.4.119: clarified throughput bases ----
  // The field report could not tell why a 177,781-file scan printed
  // "0.0 files/s". The dialog now names the analysis-stage throughput and
  // labels both file-line rates. Accept either UI language.
  if (dialogText.isEmpty()) return fail("detailed-log dialog text not captured");
  if (!dialogText.contains(QStringLiteral("처리량(분석 단계)")) &&
      !dialogText.contains(QStringLiteral("throughput (analysis stage)")))
    return fail("throughput basis label missing from the summary");
  const bool rateOverall = dialogText.contains(QStringLiteral("전체")) ||
                           dialogText.contains(QStringLiteral("overall"));
  const bool rateNew = dialogText.contains(QStringLiteral("신규")) ||
                       dialogText.contains(QStringLiteral("new "));
  if (!rateOverall || !rateNew) return fail("file-line rate labels missing");
  // Images/videos lines say "files/sec" and each of those already contains
  // "files/s" as a prefix, so the old dialog had 2 matches; the new file line
  // adds two of its own, making 4 the minimum for a correct render.
  const int rateCount = dialogText.count(QStringLiteral("files/s"));
  if (rateCount < 4) return fail("file line does not show both rates");
  // Failure branch: an unwritable path must report failure, never success.
  if (w.testSaveDetailedLog(QString::fromStdString((d / "no_such_dir" / "x.json").string())))
    return fail("unwritable save path reported success");

  fs::remove_all(d, ec);
  std::cout << "detailed_log_acceptance=ok groups=1 purpose=UserDiagnostic save=ok\n";
  return 0;
}
