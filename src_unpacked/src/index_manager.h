#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace msf {

struct IndexPaths {
    std::filesystem::path directory;
    std::filesystem::path metadata;
    std::filesystem::path database;
    std::filesystem::path videoCache;
};

class IndexManager {
public:
    // Resolves/creates a portable, application-owned index for rootPath.
    // scanned folders are never used as index storage.
    static bool resolve(const std::filesystem::path& applicationDirectory,
                        const std::filesystem::path& rootPath,
                        IndexPaths& out);

    static std::string canonicalRoot(const std::filesystem::path& rootPath);
    static std::string folderId(const std::string& canonicalRootPath);
    static bool updateLastScan(const IndexPaths& paths);

private:
    static bool readMetadataRoot(const std::filesystem::path& metadata, std::string& root);
    static std::string escapeJson(const std::string& value);
    static std::string nowUtc();
};

}
