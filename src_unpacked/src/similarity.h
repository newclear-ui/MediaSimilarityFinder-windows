#pragma once
#include <bit>
#include <cstdint>
namespace msf {
// Degenerate-hash guard: a pHash with almost no bits set carries no visual
// identity (flat/letterbox crops). Comparing two such hashes yields 85-100%
// "similarity" between unrelated files, so verdicts must skip them.
// 0 keeps its "missing" meaning; a healthy median-split pHash sets ~32 bits.
inline bool hash_usable(std::uint64_t h) {
  return h != 0 && std::popcount(h) >= 8;
}
double hash_similarity(std::uint64_t a, std::uint64_t b);
double hash_similarity_clamped(std::uint64_t a, std::uint64_t b, double thresholdPercent);
bool passes_similarity_threshold(std::uint64_t a, std::uint64_t b, double thresholdPercent);
}
