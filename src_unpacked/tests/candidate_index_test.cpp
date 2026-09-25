#include "candidate_index.h"
#include <bit>
#include <set>
#include <vector>
#include <iostream>
int main(){
 msf::CandidateIndex x;x.add(10,0xFFFF);x.add(20,0xFFF0);x.add(30,0);
 auto r=x.query(0xFFFF,4);
 if(x.size()!=3||r.size()!=2||r[0].index!=10||r[1].index!=20)return 1;
  // Pigeonhole regression: eight changed bits spread so every 16-bit slice
  // carries at most two must still be found by the radius-2 candidate stage.
 const std::uint64_t base=0;
 std::uint64_t eight=0;
 for(unsigned bit: {0u,8u,15u,22u,29u,36u,43u,50u}) eight|=(1ULL<<bit);
 x.clear(); x.add(1,base); x.add(2,eight);
 auto exact=x.query(base,8); if(exact.size()!=2||exact[0].index!=1||exact[1].index!=2||exact[1].distance!=8)return 2;
 // Distances above the optimized D<=8 range use the exact fallback path.
  auto fallback=x.query(base,9); if(fallback.size()!=2)return 3;
  // Deterministic brute-force recall check for every supported optimized
  // threshold. This validates the candidate filter, not only its boundary case.
  std::vector<std::uint64_t> hashes;
  for(unsigned i=0;i<256;++i) hashes.push_back(0x9e3779b97f4a7c15ULL*i^(std::uint64_t(i)<<17));
  x.clear(); x.addAll(hashes);
  for(unsigned d: {2u,4u,6u,8u}) for(std::size_t q=0;q<hashes.size();++q){
    std::set<std::size_t> indexed, brute;
    for(const auto& c:x.query(hashes[q],d)) indexed.insert(c.index);
    for(std::size_t i=0;i<hashes.size();++i) if(std::popcount(hashes[q]^hashes[i])<=d) brute.insert(i);
    if(indexed!=brute){ std::cerr<<"candidate recall mismatch D="<<d<<"\n"; return 4; }
  }
  std::cout<<"candidate_index=ok\n";return 0;
}
