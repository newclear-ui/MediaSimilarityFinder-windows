#pragma once
#include <cstdint>
namespace msf {
double hash_similarity(std::uint64_t a, std::uint64_t b);
double hash_similarity_clamped(std::uint64_t a, std::uint64_t b, double thresholdPercent);
bool passes_similarity_threshold(std::uint64_t a, std::uint64_t b, double thresholdPercent);
}
