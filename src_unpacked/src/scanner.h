#pragma once
#include "database.h"
#include <functional>
#include <string>
#include <vector>
namespace msf { class Scanner { public: std::vector<FileState> scan(const std::string& root, const std::string& excludedDirectory = {}, const std::function<void(std::size_t)>& onProgress = {}) const; }; }
