// Backend file log (P3a: 0.9.4.69). The worker-side scanLog lines used to go
// through MainWindow::scanLog; that static now belongs to the Backend (the
// GUI keeps its own GUI-side lines through MainWindow::scanLog). Same file,
// same timestamp shape, so existing log parsers keep working.
#pragma once
#include <string>

namespace msf {
void backendLogLine(const std::string& line);
} // namespace msf
