// Bisect probe: which half of the Tiles -> Icons round-trip breaks grid items?
// Case GRID: setGridSize(320,76) then setGridSize(QSize()), no delegate calls.
// Case DELEGATE: setItemDelegate(custom) then setItemDelegate(nullptr), no grid calls.
// Exit 0 only if both round-trips keep items visible.
#include <QApplication>
#include <QListWidget>
#include <QStyledItemDelegate>
#include <iostream>
static void fill(QListWidget& grid) {
  grid.setViewMode(QListView::IconMode);
  grid.setResizeMode(QListView::Adjust);
  grid.setIconSize(QSize(128, 128));
  for (int i = 0; i < 3; ++i) {
    QPixmap pm(64, 64); pm.fill(Qt::red);
    grid.addItem(new QListWidgetItem(QIcon(pm), QString("group %1\n3 files").arg(i + 1)));
  }
  grid.show();
  QApplication::processEvents();
}
static QRect cell(QListWidget& grid) {
  QApplication::processEvents();
  return grid.visualItemRect(grid.item(0));
}
int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  int rc = 0;
  { // GRID-only round-trip
    QListWidget grid; fill(grid);
    const QRect r0 = cell(grid);
    grid.setGridSize(QSize(320, 76)); QApplication::processEvents();
    grid.setGridSize(QSize()); QApplication::processEvents();
    const QRect r1 = cell(grid);
    std::cerr << "GRID: base=" << r0.width() << "x" << r0.height()
              << " after=" << r1.width() << "x" << r1.height()
              << " valid=" << r1.isValid() << "\n";
    if (!r0.isValid() || !r1.isValid() || r1.height() <= 0) { std::cerr << "GRID BROKEN\n"; rc |= 1; }
  }
  { // DELEGATE-only round-trip (fixed: restore a real default instance, never nullptr)
    QListWidget grid; fill(grid);
    QStyledItemDelegate custom, plain;
    const QRect r0 = cell(grid);
    grid.setItemDelegate(&custom); QApplication::processEvents();
    grid.setItemDelegate(&plain); QApplication::processEvents();
    const QRect r1 = cell(grid);
    std::cerr << "DELEGATE: base=" << r0.width() << "x" << r0.height()
              << " after=" << r1.width() << "x" << r1.height()
              << " valid=" << r1.isValid() << "\n";
    if (!r0.isValid() || !r1.isValid() || r1.height() <= 0) { std::cerr << "DELEGATE BROKEN\n"; rc |= 2; }
  }
  if (rc == 0) std::cout << "viewmode_roundtrip=ok\n";
  return rc;
}
