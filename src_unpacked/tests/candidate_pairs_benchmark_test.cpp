#include "candidate_index.h"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>
#include <cstdlib>

static void run(std::size_t n, unsigned d, std::uint64_t seed, bool heavy=false){
  std::mt19937_64 rng(seed);
  std::vector<std::uint64_t> h(n);
  if(heavy){
    const std::uint64_t base=0x123456789abcdef0ULL;
    for(auto&v:h) v=base ^ (rng() & 0xFFULL);
  } else {
    for(auto&v:h) v=rng();
  }
  msf::CandidateIndex idx;
  idx.addAll(h);
  auto t0=std::chrono::steady_clock::now();
  const auto pairs=idx.candidatePairs(d);
  auto t1=std::chrono::steady_clock::now();
  const double ms=std::chrono::duration<double,std::milli>(t1-t0).count();
  std::cout<<"N="<<n<<" D="<<d<<" heavy="<<(heavy?1:0)<<" candidate_pairs_ms="<<ms<<" pairs="<<pairs.size()<<"\n";
}
int main(){
  run(10000,8,0x5EED28ULL);
  run(25000,8,0x5EED29ULL);
  if(std::getenv("MSF_LARGE_BENCH")) run(50000,8,0x5EED2AULL);
  if(std::getenv("MSF_HEAVY_BENCH")) run(5000,8,0x5EED2BULL,true);
  std::cout<<"candidate_pairs_benchmark=ok\n";
  return 0;
}
