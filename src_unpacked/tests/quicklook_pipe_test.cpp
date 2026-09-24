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
  return 0;
}
#else
#include <iostream>
int main() { std::cout << "quicklook_pipe=skip_no_win32\n"; return 0; }
#endif
