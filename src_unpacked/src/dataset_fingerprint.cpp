#include "dataset_fingerprint.h"
#include "path_utils.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <system_error>

namespace msf {
namespace {

// --- SHA-256 (FIPS 180-4). Self-contained: msf_core links neither Qt nor a
// crypto library, so borrowing QCryptographicHash was not an option. ---

struct Sha256 {
    std::uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                          0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    unsigned char buf[64] = {};
    std::size_t bufLen = 0;
    std::uint64_t totalBits = 0;

    static std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void block(const unsigned char* p) {
        static const std::uint32_t k[64] = {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(p[i * 4]) << 24) | (std::uint32_t(p[i * 4 + 1]) << 16) |
                   (std::uint32_t(p[i * 4 + 2]) << 8) | std::uint32_t(p[i * 4 + 3]);
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void update(const unsigned char* p, std::size_t n) {
        totalBits += static_cast<std::uint64_t>(n) * 8u;
        while (n > 0) {
            const std::size_t take = std::min(n, std::size_t(64) - bufLen);
            std::memcpy(buf + bufLen, p, take);
            bufLen += take; p += take; n -= take;
            if (bufLen == 64) { block(buf); bufLen = 0; }
        }
    }

    std::string finish() {
        unsigned char pad[72] = {0x80};
        const std::size_t padLen = (bufLen < 56) ? (56 - bufLen) : (120 - bufLen);
        const std::uint64_t bits = totalBits;
        update(pad, padLen);
        unsigned char lenBe[8];
        for (int i = 0; i < 8; ++i) lenBe[i] = static_cast<unsigned char>(bits >> (56 - i * 8));
        update(lenBe, 8);
        static const char* hex = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (int i = 0; i < 8; ++i)
            for (int b = 3; b >= 0; --b) {
                const unsigned char v = static_cast<unsigned char>(h[i] >> (b * 8));
                out.push_back(hex[v >> 4]);
                out.push_back(hex[v & 0x0F]);
            }
        return out;
    }
};

// Reads the entire file. Partial reads are treated as a failure rather than
// silently hashing a prefix: a dataset identity must cover all the bytes.
bool hashWholeFile(const fs::path& p, std::string& outHex, std::uint64_t& sizeOut) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    Sha256 s;
    std::vector<unsigned char> buf(1 << 16);
    std::uint64_t total = 0;
    while (f) {
        f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
        const std::streamsize got = f.gcount();
        if (got > 0) { s.update(buf.data(), static_cast<std::size_t>(got)); total += static_cast<std::uint64_t>(got); }
    }
    if (f.bad()) return false;
    outHex = s.finish();
    sizeOut = total;
    return true;
}

}

std::string sha256Hex(const unsigned char* data, std::size_t len) {
    Sha256 s;
    s.update(data, len);
    return s.finish();
}

std::string canonicalRelativePath(const std::string& root, const std::string& file) {
    std::error_code ec;
    fs::path rel = fs::relative(fs::path(file), fs::path(root), ec);
    if (ec || rel.empty()) return std::string();
    std::string s = path_to_utf8(rel.lexically_normal());
    for (char& c : s) { if (c == '\\') c = '/'; if (c == '/') c = '/'; }
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    if (!s.empty() && s.front() == '/') s.erase(s.begin());
    return s;
}

DatasetFingerprint computeDatasetFingerprint(const std::string& root) {
    DatasetFingerprint out;
    std::error_code ec;
    const fs::path rootPath = path_from_utf8(root);
    if (!fs::is_directory(rootPath, ec)) return out;   // stays not_available

    struct Entry { std::string rel; std::uint64_t size; std::string hash; };
    std::vector<Entry> entries;

    fs::recursive_directory_iterator it(rootPath, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    for (; it != end && !ec; it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec)) continue;
        const std::string file = path_to_utf8(it->path());
        const std::string rel = canonicalRelativePath(root, file);
        if (rel.empty()) continue;                     // defensive: never hash a nameless entry
        Entry e;
        e.rel = rel;
        if (!hashWholeFile(it->path(), e.hash, e.size)) { out.state = "failed"; return out; }
        entries.push_back(std::move(e));
    }
    if (ec) { out.state = "failed"; return out; }

    // Byte-order sort on the canonical path: locale independent, and it is
    // what makes the fingerprint independent of walk order.
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.rel < b.rel; });

    out.fileCount = static_cast<std::uint64_t>(entries.size());
    for (const Entry& e : entries) out.totalBytes += e.size;

    if (entries.empty()) return out;                   // empty dataset stays not_available

    Sha256 manifest;
    for (const Entry& e : entries) {
        const std::string line = e.rel + ";" + std::to_string(e.size) + ";" + e.hash;
        manifest.update(reinterpret_cast<const unsigned char*>(line.data()), line.size());
        manifest.update(reinterpret_cast<const unsigned char*>("|"), 1);
    }
    out.fingerprint = manifest.finish();
    out.state = "measured";
    return out;
}

}
