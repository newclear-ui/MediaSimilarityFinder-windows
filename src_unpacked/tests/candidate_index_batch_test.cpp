#include "candidate_index.h"
#include <iostream>
#include <random>
#include <algorithm>
int main(){
 msf::CandidateIndex idx; std::vector<std::uint64_t> h(5000); std::mt19937_64 rng(42); for(auto&x:h)x=rng(); idx.addAll(h);
 auto one=idx.query(h[1234],0); if(one.size()!=1||one[0].index!=1234||one[0].distance!=0)return 1;
 auto many=idx.queryAll({h[0],h[1],h[2]},8); if(many.size()!=3)return 2;
 for(auto&r:many)for(auto c:r)if(c.distance>8)return 3;
 std::cout<<"candidate_batch=ok\nnodes="<<idx.nodeCount()<<"\n";return 0;
}
