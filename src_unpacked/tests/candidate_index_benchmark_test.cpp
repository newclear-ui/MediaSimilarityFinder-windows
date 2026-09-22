#include "candidate_index.h"
#include <bit>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>
#include <cstdlib>
static unsigned pop64(std::uint64_t x){return static_cast<unsigned>(std::popcount(x));}
static void run(std::size_t N,unsigned D,std::mt19937_64& rng,bool linear){
 std::vector<std::uint64_t> h(N); for(auto&x:h)x=rng(); msf::CandidateIndex idx;
 auto t0=std::chrono::steady_clock::now(); idx.addAll(h); auto t1=std::chrono::steady_clock::now();
 std::size_t indexedHits=0; auto q0=std::chrono::steady_clock::now(); for(auto x:h) indexedHits+=idx.query(x,D).size(); auto q1=std::chrono::steady_clock::now();
 std::size_t linearHits=0; double linearMs=0;
 if(linear){auto l0=std::chrono::steady_clock::now(); for(auto a:h)for(auto b:h)if(pop64(a^b)<=D)++linearHits; auto l1=std::chrono::steady_clock::now(); linearMs=std::chrono::duration<double,std::milli>(l1-l0).count(); if(indexedHits!=linearHits){std::cerr<<"mismatch N="<<N<<" indexed="<<indexedHits<<" linear="<<linearHits<<"\n"; std::exit(1);}}
 const auto st=idx.stats(); const double indexedMs=std::chrono::duration<double,std::milli>(q1-q0).count();
 std::cout<<"N="<<N<<" index_build_ms="<<std::chrono::duration<double,std::milli>(t1-t0).count()<<" indexed_query_ms="<<indexedMs;
 if(linear) std::cout<<" linear_query_ms="<<linearMs<<" speedup="<<(linearMs/std::max(0.001,indexedMs))<<"x";
 std::cout<<" hits="<<indexedHits<<" nodes="<<st.nodeCount<<" edges="<<st.edgeCount<<" max_depth="<<st.maxDepth<<"\n";
}
int main(){
 std::mt19937_64 rng(0x5EED1234ULL); const unsigned D=8;
 run(6000,D,rng,true); run(25000,D,rng,false); run(50000,D,rng,false); if(std::getenv("MSF_LARGE_BENCH")) run(100000,D,rng,false);
 std::cout<<"candidate_index_benchmark=ok\n"; return 0;
}
