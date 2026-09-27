#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace msf {

// D8a: reproducible dataset identity.
//
// The fingerprint answers exactly one question: "is this the same input
// data?" It is deliberately NOT a location identity, so two copies of the
// same content under different absolute roots produce the same value.
//
// Design constraints that came out of the D8a investigation:
//  - msf_core links no Qt and no crypto library, so SHA-256 is implemented
//    here rather than borrowed from QCryptographicHash.
//  - Paths cross every boundary through path_from_utf8/path_to_utf8 because
//    narrow UTF-8 -> fs::path throws on Windows for non-ANSI names.
//  - The whole file is read for the content hash. The scanner's existing
//    quick() only reads the first 64 KiB, but that serves file identity,
//    not dataset identity, so it is not reused here.

// Raised whenever the algorithm's definition changes, independently of the
// dataset content. Precedent: PerformanceProfile::kProfileVersion.
inline constexpr int kDatasetFingerprintVersion = 1;

struct DatasetFingerprint {
    // "measured"     - fingerprint computed
    // "not_available"- root missing / not a directory / nothing to describe
    // "failed"       - the root exists but a file could not be read
    std::string state = "not_available";
    std::string fingerprint;  // 64 lowercase hex chars, empty unless measured
    std::uint64_t fileCount = 0;
    std::uint64_t totalBytes = 0;
};

// Stable lowercase hex SHA-256 of a byte range. Exposed because the same
// primitive is the per-file content hash inside the manifest.
std::string sha256Hex(const unsigned char* data, std::size_t len);

// Canonical relative path form used inside the manifest: 'a/b/c.png' with
// forward slashes, no leading or trailing separator, no case folding.
// Case folding is omitted on purpose: it would merge genuinely distinct
// files on case-sensitive filesystems.
std::string canonicalRelativePath(const std::string& root, const std::string& file);

// Walks the root recursively and builds the manifest. Only regular files
// below the root participate; ordering of the walk does not matter because
// the manifest is sorted before hashing. Never throws: unreadable entries
// move the result to "failed" and stop the walk.
DatasetFingerprint computeDatasetFingerprint(const std::string& root);

}
