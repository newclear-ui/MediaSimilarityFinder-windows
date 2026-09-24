#include "index_manager.h"
#include "path_utils.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <cctype>
#include <cstdio>

namespace msf {
namespace fs = std::filesystem;
static constexpr const char* kApplicationVersion = "0.9.2.65";

static std::uint64_t fnv1a64(const std::string& s, std::uint64_t seed) {
    std::uint64_t h = 14695981039346656037ull ^ seed;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

std::string IndexManager::canonicalRoot(const fs::path& rootPath) {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(rootPath, ec);
    if (ec) p = fs::absolute(rootPath, ec);
    p = p.lexically_normal();
    std::string s = path_to_utf8(p);
    for (char& c : s) if (c == '\\') c = '/';
#ifdef _WIN32
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#endif
    while (s.size() > 1 && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    return s;
}

std::string IndexManager::folderId(const std::string& canonicalRootPath) {
    const auto a = fnv1a64(canonicalRootPath, 0x9e3779b97f4a7c15ull);
    const auto b = fnv1a64(canonicalRootPath, 0xd1b54a32d192ed03ull);
    std::ostringstream os;
    os << std::hex << std::setfill('0') << std::setw(16) << a << std::setw(16) << b;
    return os.str();
}

std::string IndexManager::escapeJson(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 16);
    for (char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::string IndexManager::nowUtc() {
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return os.str();
}

bool IndexManager::readMetadataRoot(const fs::path& metadata, std::string& root) {
    std::ifstream in(metadata, std::ios::binary);
    if (!in) return false;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string key = "\"rootPath\"";
    const auto kp = text.find(key);
    if (kp == std::string::npos) return false;
    auto colon = text.find(':', kp + key.size());
    if (colon == std::string::npos) return false;
    auto q = text.find('"', colon + 1);
    if (q == std::string::npos) return false;
    std::string out;
    bool esc = false;
    for (std::size_t i = q + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (esc) {
            switch (c) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            default: out += c; break;
            }
            esc = false;
        } else if (c == '\\') {
            esc = true;
        } else if (c == '"') {
            root = out;
            return true;
        } else {
            out += c;
        }
    }
    return false;
}

bool IndexManager::resolve(const fs::path& applicationDirectory,
                           const fs::path& rootPath,
                           IndexPaths& out) {
    const std::string canonical = canonicalRoot(rootPath);
    if (canonical.empty()) return false;

    const fs::path indexRoot = applicationDirectory / "Index";
    std::error_code ec;
    fs::create_directories(indexRoot, ec);
    if (ec) return false;

    // Fast path: the stable folder ID is deterministic for a canonical root.
    // The metadata is still verified, so hash collisions cannot silently reuse
    // another root. The directory scan below remains as a legacy/collision
    // fallback.
    const std::string id = folderId(canonical);
    {
        const fs::path candidate = indexRoot / id;
        const fs::path metadata = candidate / "metadata.json";
        std::string stored;
        if (fs::is_directory(candidate, ec) && readMetadataRoot(metadata, stored) && stored == canonical) {
            out.directory = candidate;
            out.metadata = metadata;
            out.database = candidate / "index.sqlite";
            out.videoCache = candidate / "video_cache.sqlite";
            return true;
        }
    }

    // First locate an existing metadata record. This also protects against
    // legacy indexes and path-hash collisions while keeping folder names opaque.
    for (const auto& entry : fs::directory_iterator(indexRoot, ec)) {
        if (ec) return false;
        if (!entry.is_directory()) continue;
        const fs::path metadata = entry.path() / "metadata.json";
        std::string stored;
        if (readMetadataRoot(metadata, stored) && stored == canonical) {
            out.directory = entry.path();
            out.metadata = metadata;
            out.database = entry.path() / "index.sqlite";
            out.videoCache = entry.path() / "video_cache.sqlite";
            return true;
        }
    }

    fs::path dir = indexRoot / id;
    int suffix = 0;
    while (fs::exists(dir, ec)) {
        std::string stored;
        const fs::path metadata = dir / "metadata.json";
        if (readMetadataRoot(metadata, stored) && stored == canonical) break;
        dir = indexRoot / (id + "_" + std::to_string(++suffix));
    }
    fs::create_directories(dir, ec);
    if (ec) return false;

    out.directory = dir;
    out.metadata = dir / "metadata.json";
    out.database = dir / "index.sqlite";
    out.videoCache = dir / "video_cache.sqlite";

    if (!fs::exists(out.metadata)) {
        const auto now = nowUtc();
        fs::path tmp = out.metadata; tmp += ".tmp";
        {
            std::ofstream meta(tmp, std::ios::binary | std::ios::trunc);
            if (!meta) return false;
            meta << "{\n"
                 << "  \"schemaVersion\": 1,\n"
                 << "  \"applicationVersion\": \"" << kApplicationVersion << "\",\n"
                 << "  \"rootPath\": \"" << escapeJson(canonical) << "\",\n"
                 << "  \"created\": \"" << now << "\",\n"
                 << "  \"lastScan\": \"" << now << "\"\n"
                 << "}\n";
            if (!meta) return false;
        }
        fs::rename(tmp, out.metadata, ec);
        if (ec) { fs::remove(tmp); return false; }
    }
    return true;
}

bool IndexManager::updateLastScan(const IndexPaths& paths) {
    std::string old;
    {
        // NOTE: the reader must be closed before the atomic-replace rename
        // below - Windows cannot replace a file that is still open.
        std::ifstream in(paths.metadata, std::ios::binary);
        if (!in) return false;
        old.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::string root;
    if (!readMetadataRoot(paths.metadata, root)) return false;
    std::string created = "unknown";
    const std::string key = "\"created\"";
    const auto kp = old.find(key);
    if (kp != std::string::npos) {
        const auto colon = old.find(':', kp + key.size());
        const auto q1 = old.find('\"', colon + 1);
        const auto q2 = old.find('\"', q1 + 1);
        if (q1 != std::string::npos && q2 != std::string::npos) created = old.substr(q1 + 1, q2 - q1 - 1);
    }
    fs::path tmp = paths.metadata; tmp += ".tmp";
    {
        std::ofstream meta(tmp, std::ios::binary | std::ios::trunc);
        if (!meta) return false;
        const auto now = nowUtc();
        meta << "{\n"
             << "  \"schemaVersion\": 1,\n"
             << "  \"applicationVersion\": \"" << kApplicationVersion << "\",\n"
             << "  \"rootPath\": \"" << escapeJson(root) << "\",\n"
             << "  \"created\": \"" << created << "\",\n"
             << "  \"lastScan\": \"" << now << "\"\n"
             << "}\n";
        if (!meta) return false;
    }
    std::error_code ec;
    fs::rename(tmp, paths.metadata, ec);
    if (ec) { fs::remove(tmp); return false; }
    return true;
}

}
