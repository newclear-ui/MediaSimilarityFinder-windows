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
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
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
  // Seeing it proves the GUI result path was reached.
  bool dialogSeen = false;
  QTimer closer;
  closer.setInterval(250);
  QObject::connect(&closer, &QTimer::timeout, [&]() {
    if (QWidget* m = QApplication::activeModalWidget()) {
      dialogSeen = true;
      m->close();
    }
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

  fs::remove_all(d, ec);
  std::cout << "detailed_log_acceptance=ok groups=1 purpose=UserDiagnostic\n";
  return 0;
}
