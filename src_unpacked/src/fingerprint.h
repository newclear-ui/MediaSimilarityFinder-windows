#pragma once
#include <cstdint>
#include <vector>
namespace msf {
// 64-bit perceptual hash. Input is a normalized grayscale image, preferably 32x32.
std::uint64_t average_hash(const std::vector<std::uint8_t>& pixels);
std::uint64_t perceptual_hash(const std::vector<std::uint8_t>& pixels, int width, int height);
// Perceptual hash after a horizontal mirror transform.
std::uint64_t perceptual_hash_mirrored(const std::vector<std::uint8_t>& pixels, int width, int height);

// Normal + horizontally mirrored hash from ONE 8x8 DCT. The mirrored image's
// coefficients equal the original's with the sign of every odd horizontal
// frequency flipped, so the second hash costs a re-sort of 63 values, not a
// second transform.
struct PerceptualHashPair { std::uint64_t normal=0, mirrored=0; };
PerceptualHashPair perceptual_hash_pair(const std::vector<std::uint8_t>& pixels, int width, int height);
// Raw-buffer form for exactly 32x32 (1024 bytes) grayscale; no allocation, thread-safe.
PerceptualHashPair perceptual_hash_pair_32(const std::uint8_t* pixels1024);
}
