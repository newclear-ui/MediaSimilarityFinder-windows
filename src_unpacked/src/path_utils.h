#pragma once
#include <filesystem>
#include <string>

namespace msf {
namespace fs = std::filesystem;

// MediaSimilarityFinder stores paths as UTF-8 strings. On Windows, convert
// explicitly between UTF-8 and the native wide filesystem representation so
// Korean/Unicode paths do not depend on the process ANSI code page.
inline fs::path path_from_utf8(const std::string& value) {
#ifdef _WIN32
    return fs::u8path(value);
#else
    return fs::path(value);
#endif
}

inline std::string path_to_utf8(const fs::path& value) {
#ifdef _WIN32
    const auto u = value.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
#else
    return value.string();
#endif
}
}
