#pragma once
#include "database.h"
#include <atomic>
#include <functional>
#include <string>
#include <vector>
namespace msf {
// Streaming walk controls. onFile receives each media file as it is found so
// analysis can overlap the directory walk; the returned vector still holds
// every walked file when onFile is empty (classic batch mode).
class Scanner {
public:
  // Streaming walk controls. onFile receives each media file as it is found
  // so analysis can overlap the directory walk; the returned vector still
  // holds every walked file when onFile is empty (classic batch mode).
  struct ScanCallbacks {
    std::function<void(std::size_t)> onProgress;
    std::function<void(FileState&&)> onFile;
    const std::atomic_bool* cancel = nullptr;
    const std::atomic_bool* pause = nullptr;
  };
  std::vector<FileState> scan(const std::string& root, const std::string& excludedDirectory = {}, const std::function<void(std::size_t)>& onProgress = {}) const;
  std::vector<FileState> scan_stream(const std::string& root, const std::string& excludedDirectory, const ScanCallbacks& cb) const;
}; }
