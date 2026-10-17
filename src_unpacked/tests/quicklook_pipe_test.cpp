// QuickLook wire-protocol test: spin a local named-pipe server, send through
// quickLookSendMessage(), and assert the exact bytes QuickLook expects:
// "QuickLook.App.PipeMessages.Toggle|<path>|\n" in UTF-8 (Korean + space in
// the path proves encoding). No QuickLook installation needed. Windows-only.
#ifdef _WIN32
#include "mainwindow.h"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <atomic>
#include <future>
#include <thread>
#include <chrono>
#include <iostream>
static void server(std::wstring name, std::promise<QByteArray> prom, std::promise<void> ready) {
  HANDLE h = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND,
                              PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                              1, 65536, 65536, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) { prom.set_value(QByteArray()); ready.set_value(); return; }
  ready.set_value(); // instance exists: clients may now connect
  if (!ConnectNamedPipe(h, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
    CloseHandle(h); prom.set_value(QByteArray()); return;
  }
  QByteArray out; char buf[4096]; DWORD n = 0;
  while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0) out.append(buf, (int)n);
  DisconnectNamedPipe(h); CloseHandle(h);
  prom.set_value(out);
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const std::wstring name = L"\\\\.\\pipe\\msf-ql-probe-" + std::to_wstring(GetCurrentProcessId());
  std::promise<QByteArray> prom; auto fut = prom.get_future();
  std::promise<void> ready; auto readyFut = ready.get_future();
  std::thread srv(server, name, std::move(prom), std::move(ready)); srv.detach();
  if (readyFut.wait_for(std::chrono::seconds(10)) != std::future_status::ready) { std::cerr << "server setup timeout\n"; return 5; }
  const QString file = QString::fromWCharArray(L"C:\\\ud55c\uae00 \ud3f4\ub354\\a b.jpg");
  const QByteArray payload = quickLookToggleMessage(file).toUtf8();
  const QString pipe = QString::fromWCharArray(name.c_str());
  if (!quickLookSendMessage(pipe, payload)) { std::cerr << "send failed\n"; return 2; }
  if (fut.wait_for(std::chrono::seconds(10)) != std::future_status::ready) { std::cerr << "server timeout\n"; return 3; }
  const QByteArray expect = QByteArray("QuickLook.App.PipeMessages.Toggle|") + file.toUtf8() + "|\n";
  const QByteArray got = fut.get();
  if (got != expect) {
    std::cerr << "mismatch got=" << got.size() << " want=" << expect.size() << "\n"; return 4;
  }
  std::cout << "quicklook_pipe=ok bytes=" << got.size() << "\n";

  // 0.9.4.125: wedged server. The instance is connected but never reads, and
  // the payload (4 MB) far exceeds any pipe buffer, so a synchronous
  // WriteFile would block forever on the GUI thread. The bounded send must
  // fail fast instead. (Pre-fix this check hangs the test binary; the
  // FAIL-first reproduction is a run killed by an external timeout.)
  const std::wstring wname = L"\\\\.\\pipe\\msf-ql-probe-wedge-" + std::to_wstring(GetCurrentProcessId());
  HANDLE wh = CreateNamedPipeW(wname.c_str(), PIPE_ACCESS_INBOUND,
                               PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                               1, 65536, 64, 0, nullptr);
  if (wh == INVALID_HANDLE_VALUE) { std::cerr << "wedge setup failed\n"; return 6; }
  std::atomic<bool> wedgeDone{false};
  std::atomic<bool> wedgeListening{false};
  std::thread wedge([&] {
    wedgeListening.store(true); // about to listen; the client rendezvous below
    if (ConnectNamedPipe(wh, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) {
      // Connected but deliberately never reads: the client's write must time
      // out instead of blocking forever. Sleep in slices so the process can
      // always exit (a detached thread never blocks the suite's teardown).
      for (int i = 0; i < 600 && !wedgeDone.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    DisconnectNamedPipe(wh);
    CloseHandle(wh);
  });
  wedge.detach();
  // Wait until the server is listening (deterministic setup). Note this flag
  // intentionally precedes ConnectNamedPipe: a "connected" flag could only be
  // set after the client acts, which would deadlock this wait.
  bool up = false;
  for (int i = 0; i < 100 && !(up = wedgeListening.load()); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  if (!up) { wedgeDone.store(true); std::cerr << "wedge setup: server never listened\n"; return 6; }
  const std::wstring wpipe = wname;
  const QString wqpipe = QString::fromStdWString(wpipe);
  const QByteArray big = payload + QByteArray(4 * 1024 * 1024, 'x'); // far over any pipe buffer
  QElapsedTimer wt;
  wt.start();
  const bool wedgeOk = quickLookSendMessage(wqpipe, big);
  const qint64 wedgeMs = wt.elapsed();
  wedgeDone.store(true);
  std::cout << "  [info] wedge send ok=" << wedgeOk << " ms=" << wedgeMs << "\n";
  if (wedgeOk) { std::cerr << "wedged pipe unexpectedly accepted\n"; return 7; }
  if (wedgeMs > 15000) { std::cerr << "wedged pipe did not fail fast\n"; return 8; }
  if (wedgeMs < 4000) { std::cerr << "wedge send failed without waiting out the timeout\n"; return 9; }
  std::cout << "quicklook_pipe_wedge=ok\n";
  return 0;
}
#else
#include <iostream>
int main() { std::cout << "quicklook_pipe=skip_no_win32\n"; return 0; }
#endif
