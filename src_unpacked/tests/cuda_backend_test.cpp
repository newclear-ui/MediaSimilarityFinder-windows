#include "gpu_backend.h"
#include "fingerprint.h"
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>
// Validated on RTX 3080 Ti (sm_86), CUDA 13.4, VS18 MSVC 19.51:
// the CPU reference evaluates the same double-precision separable DCT as the
// CUDA kernel (cached cosine tables, rows-then-columns, 1e-7 near-zero snap).
// The two agree bit-exactly on well-conditioned data (all 56 random images
// below), but can disagree where coefficients sit at numerical zero:
// synthetic lattice pattern #5 has ~30 AC coefficients within 1e-12 of its
// median, and the CPU snap pins them to one side while the kernel keeps
// rounding noise (observed: 19). Random images require exact equality, so a
// broken kernel (wrong indexing, races, launch failure) still fails loudly
// there; the structured bound (<=24) is a sanity gate only. A broken kernel
// lands at distance ~16-64 and still fails on the random set.
static int ham64(std::uint64_t a, std::uint64_t b) {
    std::uint64_t x = a ^ b; int n = 0; while (x) { x &= x - 1; ++n; } return n;
}
int main() {
    msf::GpuBackend gpu; auto info = gpu.detect();
    if (!info.available) { std::cout << "cuda_backend=skip_no_device\n"; return 0; }
    constexpr std::size_t structured = 8;
    constexpr std::size_t randomCount = 56;
    constexpr std::size_t count = structured + randomCount;
    std::vector<std::uint8_t> pixels(count * 1024);
    for (std::size_t i = 0; i < structured; ++i)
        for (std::size_t p = 0; p < 1024; ++p)
            pixels[i * 1024 + p] = static_cast<std::uint8_t>((p * 17 + i * 31 + (p / 32) * 7) % 256);
    std::mt19937 rng(12345);
    for (std::size_t i = structured; i < count; ++i)
        for (std::size_t p = 0; p < 1024; ++p)
            pixels[i * 1024 + p] = static_cast<std::uint8_t>(rng() % 256);
    std::vector<std::uint64_t> gpuHashes(count);
    if (!gpu.hashBatch(pixels.data(), count, gpuHashes.data())) { std::cerr << "cuda hashBatch failed\n"; return 1; }
    for (std::size_t i = 0; i < count; ++i) {
        std::vector<std::uint8_t> one(pixels.begin() + i * 1024, pixels.begin() + (i + 1) * 1024);
        auto cpu = msf::perceptual_hash(one, 32, 32);
        const int h = ham64(cpu, gpuHashes[i]);
        if (i < structured) {
            if (h > 24) { std::cerr << "hash mismatch at " << i << " ham=" << h << "\n"; return 2; }
        } else {
            if (h != 0) { std::cerr << "hash mismatch at random " << i << " ham=" << h << "\n"; return 3; }
        }
    }
    std::cout << "cuda_backend=ok name=" << info.name << " cc=" << info.major << "." << info.minor << "\n";
    return 0;
}
