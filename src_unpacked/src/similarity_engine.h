#pragma once
#include <cstddef>
#include <cstdint>
namespace msf {
struct SimilarityResult { std::size_t left=0,right=0; double percent=0; };
struct SimilarityOptions { double thresholdPercent=0.0; };
SimilarityResult best_match(const std::uint64_t* a,std::size_t na,const std::uint64_t* b,std::size_t nb,const SimilarityOptions& options={});
SimilarityResult best_match_parallel(const std::uint64_t* a,std::size_t na,const std::uint64_t* b,std::size_t nb,const SimilarityOptions& options={},std::size_t workers=0);
}
