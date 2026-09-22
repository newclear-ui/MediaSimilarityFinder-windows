#pragma once
#include <cstdint>
#include <vector>
namespace msf {
// 64-bit perceptual hash. Input is a normalized grayscale image, preferably 32x32.
std::uint64_t average_hash(const std::vector<std::uint8_t>& pixels);
std::uint64_t perceptual_hash(const std::vector<std::uint8_t>& pixels, int width, int height);
// Perceptual hash after a horizontal mirror transform.
std::uint64_t perceptual_hash_mirrored(const std::vector<std::uint8_t>& pixels, int width, int height);
}
