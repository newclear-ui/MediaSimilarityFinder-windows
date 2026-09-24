// Guards the fast pHash (cached cosine table, separable DCT, mirror by sign
// flip) against the direct-sum definition it replaced. The reference below is
// that legacy definition, kept here as the test oracle only.
#include "fingerprint.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <random>
#include <vector>
using Img = std::vector<std::uint8_t>;
namespace {
std::uint64_t refHash(const Img& px, int width, int height) {
    constexpr int N = 32, K = 8;
    double d[K][K]{};
    for (int u = 0; u < K; ++u) for (int v = 0; v < K; ++v) {
        double sum = 0;
        for (int y = 0; y < N; ++y) { const int sy = std::min(height - 1, y * height / N);
            for (int x = 0; x < N; ++x) { const int sx = std::min(width - 1, x * width / N);
                sum += px[static_cast<std::size_t>(sy) * width + sx]
                     * std::cos((2 * x + 1) * u * std::numbers::pi / (2 * N))
                     * std::cos((2 * y + 1) * v * std::numbers::pi / (2 * N)); } }
        const double au = u == 0 ? std::sqrt(1.0 / N) : std::sqrt(2.0 / N);
        const double av = v == 0 ? std::sqrt(1.0 / N) : std::sqrt(2.0 / N);
        d[u][v] = sum * au * av;
    }
    double vals[63]; int n = 0;
    for (int u = 0; u < K; ++u) for (int v = 0; v < K; ++v) if (u || v) vals[n++] = std::abs(d[u][v]) < 1e-7 ? 0.0 : d[u][v];
    double s[63]; std::copy(vals, vals + n, s); std::sort(s, s + n);
    const double med = s[n / 2]; std::uint64_t h = 0;
    for (int i = 0; i < n; ++i) if (vals[i] >= med) h |= 1ULL << i;
    return h;
}
Img mirror(const Img& p, int w, int h) {
    Img m(p.size());
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) m[y * w + x] = p[y * w + (w - 1 - x)];
    return m;
}
int popc(std::uint64_t v) { int c = 0; while (v) { v &= v - 1; ++c; } return c; }
}
int main() {
    std::mt19937 rng(20260925);
    auto clampb = [](double v) { return static_cast<std::uint8_t>(std::clamp(v, 0.0, 255.0)); };
    int fail = 0;
    // 1) natural-like content: must be bit-identical to the direct-sum reference,
    //    for normal, physically mirrored, pair(), and non-32 inputs.
    for (int i = 0; i < 1500; ++i) {
        const int w = (i % 5 == 0) ? 64 : 32, h = (i % 5 == 0) ? 48 : 32;
        const double a = 1 + rng() % 90 / 10.0, b = 1 + rng() % 90 / 10.0, c = rng() % 255;
        Img im(w * h);
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
            im[y * w + x] = (i % 3 == 0) ? static_cast<std::uint8_t>(rng() % 256)
                                         : clampb(c * .5 + 60 * std::sin(x * 32.0 / w / a + 1) + 60 * std::cos(y * 32.0 / h / b) + rng() % 12);
        const auto n = msf::perceptual_hash(im, w, h), m = msf::perceptual_hash_mirrored(im, w, h);
        if (n != refHash(im, w, h)) { std::printf("normal mismatch i=%d\n", i); ++fail; }
        if (m != refHash(mirror(im, w, h), w, h)) { std::printf("mirror mismatch i=%d\n", i); ++fail; }
        const auto p = msf::perceptual_hash_pair(im, w, h);
        if (p.normal != n || p.mirrored != m) { std::printf("pair mismatch i=%d\n", i); ++fail; }
        if (w == 32 && h == 32) {
            const auto r = msf::perceptual_hash_pair_32(im.data());
            if (r.normal != n || r.mirrored != m) { std::printf("pair_32 mismatch i=%d\n", i); ++fail; }
        }
    }
    // 2) degenerate content is deterministic: a flat image is all ties, and
    //    mirroring must not change its hash (near-zero coefficients snap to 0).
    for (int v : {0, 1, 37, 128, 255}) {
        Img flat(32 * 32, static_cast<std::uint8_t>(v));
        const auto n = msf::perceptual_hash(flat, 32, 32), m = msf::perceptual_hash_mirrored(flat, 32, 32);
        if (n != 0x7fffffffffffffffULL || m != n) { std::printf("flat %d not deterministic\n", v); ++fail; }
    }
    // 3) mirroring a horizontally symmetric image is the identity.
    {
        Img sym(32 * 32);
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 16; ++x) sym[y * 32 + x] = sym[y * 32 + 31 - x] = static_cast<std::uint8_t>((x * 13 + y * 7) & 0xff);
        if (popc(msf::perceptual_hash(sym, 32, 32) ^ msf::perceptual_hash_mirrored(sym, 32, 32)) > 2) { std::printf("symmetric mirror differs\n"); ++fail; }
    }
    // 4) invalid input.
    if (msf::perceptual_hash(Img{}, 32, 32) != 0 || msf::perceptual_hash_pair(Img{}, 32, 32).normal != 0 || msf::perceptual_hash_pair_32(nullptr).normal != 0) { std::printf("invalid input\n"); ++fail; }
    std::printf(fail ? "fingerprint_fast=FAIL (%d)\n" : "fingerprint_fast=ok\n", fail);
    return fail ? 1 : 0;
}
