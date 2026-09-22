#include "similarity.h"
#include <bit>
#include <algorithm>
namespace msf {
double hash_similarity(std::uint64_t a,std::uint64_t b) {
    return 100.0*(64-std::popcount(a^b))/64.0;
}
double hash_similarity_clamped(std::uint64_t a,std::uint64_t b,double thresholdPercent){
    return hash_similarity(a,b)>=thresholdPercent?hash_similarity(a,b):0.0;
}
bool passes_similarity_threshold(std::uint64_t a,std::uint64_t b,double thresholdPercent){
    return hash_similarity(a,b)>=std::clamp(thresholdPercent,0.0,100.0);
}
}
