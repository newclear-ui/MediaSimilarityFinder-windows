#pragma once
#include <cstdio>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vector>
#endif
namespace msf {
// Run cmd, capture stdout, and never flash a console window on Windows
// (CREATE_NO_WINDOW). Plain _popen/popen lets console-subsystem children
// (ffmpeg/ffprobe) pop a visible console on GUI apps — one flash + ~100ms
// stall per call, directly on the GUI thread in thumbnail paths.
// Binary-safe (NUL bytes preserved).
//
// Bounded lifecycle (no INFINITE wait). Every call carries an explicit
// timeoutMs chosen by the CALLER for its own class — there is intentionally
// no global default, so a telemetry probe can never inherit a decode-sized
// budget and a decode can never inherit a telemetry-sized one:
//   telemetry (nvidia-smi):            10000 ms (normally < 1 s)
//   metadata probe (ffprobe):          30000 ms (normally < 2 s)
//   single-frame decode (ffmpeg):     120000 ms (normally < 10 s)
// On timeout the child is terminated, handles are reclaimed, and false is
// returned; the parent never blocks longer than timeoutMs plus a bounded
// (~2 s) reclamation. A timeout is a failure like any other: same binary,
// same query, cached/fallback value kept by the caller.
// Returns false on spawn/read/timeout failure, non-zero exit, or empty output.
// NOTE (Windows only in this change): the POSIX path still uses popen/pclose
// and therefore still blocks until the child exits. POSIX is not a release
// target of this project (all builds are x64-windows); bounding it is a
// documented revisit item, not part of this change.
inline bool captureSilent(const std::string& cmd, std::string& out, unsigned timeoutMs) {
  out.clear();
#ifdef _WIN32
  if (timeoutMs == 0) return false;
  SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
  HANDLE rd = nullptr, wr = nullptr;
  if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
  SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOA si{}; si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = nullptr;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  std::string cmdline = cmd; // CreateProcess may modify the buffer
  BOOL ok = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, TRUE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
  CloseHandle(wr);
  if (!ok) { CloseHandle(rd); return false; }
  auto drainAvailable = [&]() {
    DWORD avail = 0;
    while (PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
      char buf[4096]; DWORD n = 0;
      DWORD want = avail > sizeof(buf) ? static_cast<DWORD>(sizeof(buf)) : avail;
      if (!ReadFile(rd, buf, want, &n, nullptr) || n == 0) break;
      out.append(buf, n);
    }
  };
  const ULONGLONG deadline = GetTickCount64() + timeoutMs;
  bool timedOut = false;
  for (;;) {
    drainAvailable();
    if (WaitForSingleObject(pi.hProcess, 20) == WAIT_OBJECT_0) break;
    if (GetTickCount64() >= deadline) { timedOut = true; break; }
  }
  if (timedOut) {
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, 2000); // bounded reclamation only
  }
  // The blocking final drain is safe only when the child is provably dead
  // (its pipe then EOFs promptly). If it somehow survived, skip the drain
  // rather than risk blocking past the timeout.
  if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
    char buf[4096]; DWORD n = 0;
    while (ReadFile(rd, buf, sizeof(buf), &n, nullptr) && n > 0) out.append(buf, n);
  }
  DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(rd);
  return !timedOut && code == 0 && !out.empty();
#else
  (void)timeoutMs; // POSIX path: see NOTE above; bounded wait is a revisit item.
  FILE* fp = popen(cmd.c_str(), "r");
  if (!fp) return false;
  char buf[4096];
  while (fgets(buf, sizeof(buf), fp)) out += buf;
  return pclose(fp) == 0 && !out.empty();
#endif
}
}
