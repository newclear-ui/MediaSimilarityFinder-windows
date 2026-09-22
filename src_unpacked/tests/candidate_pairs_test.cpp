#include "candidate_index.h"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <iostream>
#include <random>
#include <utility>
#include <vector>

static unsigned pop64(std::uint64_t x){return static_cast<unsigned>(std::popcount(x));}

int main(){
  // Exact D=0 semantics, including duplicate hashes.
  {
    msf::CandidateIndex x;
    x.add(10,0xAA);
    x.add(20,0xAA);
    x.add(30,0xAA);
    x.add(40,0xAB);
    auto p=x.candidatePairs(0);
    if(p.size()!=3) return 1;
    if(p[0].first!=10 || p[0].second.index!=20 || p[1].second.index!=30 || p[2].second.index!=30) return 2;
    for(const auto&q:p) if(q.second.distance!=0 || q.first>=q.second.index) return 3;
  }

  // Compare indexed candidatePairs with exhaustive linear semantics on a
  // deterministic set containing planted near-neighbors at D=8.
  const std::size_t N=1200;
  std::mt19937_64 rng(0xCA1D1D28ULL);
  std::vector<std::uint64_t> h(N);
  for(auto&v:h) v=rng();
  for(std::size_t i=0;i<100;i+=2){
    std::uint64_t mask=0;
    for(unsigned b=0;b<8;++b) mask|=(1ULL<<((i+b*7)%64));
    h[i+1]=h[i]^mask;
  }
  msf::CandidateIndex x; x.addAll(h);
  auto indexed=x.candidatePairs(8);
  std::vector<std::pair<std::size_t,msf::Candidate>> streamed;
  x.forEachCandidatePair(8,[&](std::size_t i,const msf::Candidate& c){streamed.emplace_back(i,c);});
  std::sort(streamed.begin(),streamed.end(),[](const auto&a,const auto&b){
    return a.first==b.first
      ? (a.second.distance==b.second.distance?a.second.index<b.second.index:a.second.distance<b.second.distance)
      : a.first<b.first;
  });
  if(streamed.size()!=indexed.size() || !std::equal(streamed.begin(),streamed.end(),indexed.begin(),[](const auto&a,const auto&b){return a.first==b.first&&a.second.index==b.second.index&&a.second.distance==b.second.distance;})) return 6;
  std::vector<std::pair<std::size_t,msf::Candidate>> linear;
  for(std::size_t i=0;i<N;++i) for(std::size_t j=i+1;j<N;++j){
    const unsigned d=pop64(h[i]^h[j]);
    if(d<=8) linear.push_back({i,{j,d}});
  }
  if(indexed.size()!=linear.size() || !std::equal(indexed.begin(),indexed.end(),linear.begin(),[](const auto&a,const auto&b){
      return a.first==b.first && a.second.index==b.second.index && a.second.distance==b.second.distance;
    })){
    std::cerr<<"candidate pair mismatch indexed="<<indexed.size()<<" linear="<<linear.size()<<"\n";
    return 4;
  }

  // D>8 remains exact through the fallback path.
  auto fallback=x.candidatePairs(9);
  std::vector<std::pair<std::size_t,msf::Candidate>> linear9;
  for(std::size_t i=0;i<N;++i) for(std::size_t j=i+1;j<N;++j){
    const unsigned d=pop64(h[i]^h[j]);
    if(d<=9) linear9.push_back({i,{j,d}});
  }
  if(fallback.size()!=linear9.size() || !std::equal(fallback.begin(),fallback.end(),linear9.begin(),[](const auto&a,const auto&b){
      return a.first==b.first && a.second.index==b.second.index && a.second.distance==b.second.distance;
    })) return 5;

  std::cout<<"candidate_pairs=ok\n"<<"pairs="<<indexed.size()<<"\n";
  return 0;
}
