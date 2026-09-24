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
// Binary-safe (NUL bytes preserved). Returns false on spawn/read failure or
// non-zero exit.
inline bool captureSilent(const std::string& cmd, std::string& out) {
  out.clear();
#ifdef _WIN32
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
  char buf[4096]; DWORD n = 0;
  while (ReadFile(rd, buf, sizeof(buf), &n, nullptr) && n > 0) out.append(buf, n);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(rd);
  return code == 0 && !out.empty();
#else
  FILE* fp = popen(cmd.c_str(), "r");
  if (!fp) return false;
  char buf[4096];
  while (fgets(buf, sizeof(buf), fp)) out += buf;
  return pclose(fp) == 0 && !out.empty();
#endif
}
}
