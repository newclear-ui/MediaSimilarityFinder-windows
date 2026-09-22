#include "candidate_index.h"
#include <iostream>
int main(){
 msf::CandidateIndex x;x.add(10,0xFFFF);x.add(20,0xFFF0);x.add(30,0);
 auto r=x.query(0xFFFF,4);
 if(x.size()!=3||r.size()!=2||r[0].index!=10||r[1].index!=20)return 1;
 // Pigeonhole regression: eight changed bits distributed across eight of
 // nine partitions must still be found by the exact multi-index candidate stage.
 const std::uint64_t base=0;
 std::uint64_t eight=0;
 for(unsigned bit: {0u,8u,15u,22u,29u,36u,43u,50u}) eight|=(1ULL<<bit);
 x.clear(); x.add(1,base); x.add(2,eight);
 auto exact=x.query(base,8); if(exact.size()!=2||exact[0].index!=1||exact[1].index!=2||exact[1].distance!=8)return 2;
 // Distances above the optimized D<=8 range use the exact fallback path.
 auto fallback=x.query(base,9); if(fallback.size()!=2)return 3;
 std::cout<<"candidate_index=ok\n";return 0;
}