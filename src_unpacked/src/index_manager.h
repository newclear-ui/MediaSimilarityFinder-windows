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

    // The directory that holds the per-root index folders. Pure path arithmetic:
    // it creates nothing. resolve() uses it, and read-only callers that only need
    // to know WHERE indexes live must use it too, because resolve() also creates
    // the directory and a metadata record as a side effect.
    static std::filesystem::path indexRootFor(const std::filesystem::path& applicationDirectory) {
        return applicationDirectory / "Index";
    }

    static std::string canonicalRoot(const std::filesystem::path& rootPath);
    static std::string folderId(const std::string& canonicalRootPath);
    static bool updateLastScan(const IndexPaths& paths);

private:
    static bool readMetadataRoot(const std::filesystem::path& metadata, std::string& root);
    static std::string escapeJson(const std::string& value);
    static std::string nowUtc();
};

}
