// Uniform-cell guard: every display icon entering a grid must be normalized
// to the exact requested rect (see squareFittedPixmap), otherwise cells vary
// with source aspect/size (probed 90x91..262x283 in one view) and previews
// look inconsistent. Asserts exact output size, centered content, transparent
// padding, and no distortion for both square and rect targets.
#include "mainwindow.h"
#include <QApplication>
#include <QImage>
#include <iostream>
static int check(const QPixmap& src, const QSize& target, const char* tag) {
  const QPixmap out = squareFittedPixmap(src, target);
  if (out.size() != target) { std::cerr << tag << ": size " << out.width() << "x" << out.height() << "\n"; return 1; }
  const QImage im = out.toImage().convertToFormat(QImage::Format_ARGB32);
  // Center pixel must carry source color (content preserved, centered).
  const QRgb c = im.pixel(target.width() / 2, target.height() / 2);
  if (qRed(c) < 200 || qGreen(c) > 80 || qBlue(c) > 80) { std::cerr << tag << ": center not red\n"; return 2; }
  // Corner must stay transparent padding (letterbox, not stretch).
  if (qAlpha(im.pixel(0, 0)) != 0) { std::cerr << tag << ": corner not padded\n"; return 3; }
  return 0;
}
int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QPixmap wide(200, 100); wide.fill(Qt::red);
  if (int r = check(wide, QSize(64, 64), "wide-square")) return 10 + r;
  QPixmap tall(100, 200); tall.fill(Qt::red);
  if (int r = check(tall, QSize(64, 64), "tall-square")) return 20 + r;
  if (int r = check(wide, QSize(220, 190), "wide-rect")) return 30 + r;
  QPixmap big(300, 150); big.fill(Qt::red);
  if (int r = check(big, QSize(48, 48), "big-small")) return 40 + r;
  std::cout << "thumb_layout=ok\n";
  return 0;
}
