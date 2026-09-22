#include "similarity_engine.h"
#include "similarity.h"
#include <algorithm>
#include <future>
#include <thread>
#include <vector>
namespace msf {
SimilarityResult best_match(const std::uint64_t*a,std::size_t na,const std::uint64_t*b,std::size_t nb,const SimilarityOptions& options){
 SimilarityResult r; if(!a||!b||!na||!nb)return r;
 const double threshold=std::clamp(options.thresholdPercent,0.0,100.0);
 for(std::size_t i=0;i<na;i++) for(std::size_t j=0;j<nb;j++){
  const double p=hash_similarity(a[i],b[j]); if(p>=threshold && p>r.percent)r={i,j,p};
 }
 return r;
}
SimilarityResult best_match_parallel(const std::uint64_t*a,std::size_t na,const std::uint64_t*b,std::size_t nb,const SimilarityOptions& options,std::size_t workers){
 if(!a||!b||!na||!nb)return {};
 workers=workers?workers:std::max(1u,std::thread::hardware_concurrency()); workers=std::min(workers,na);
 if(workers<=1||na*nb<4096)return best_match(a,na,b,nb,options);
 std::vector<std::future<SimilarityResult>> jobs; jobs.reserve(workers);
 const std::size_t chunk=(na+workers-1)/workers;
 for(std::size_t begin=0;begin<na;begin+=chunk){
   const std::size_t end=std::min(na,begin+chunk);
   jobs.emplace_back(std::async(std::launch::async,[=,&options](){
     SimilarityResult r; const double threshold=std::clamp(options.thresholdPercent,0.0,100.0);
     for(std::size_t i=begin;i<end;++i) for(std::size_t j=0;j<nb;++j){const double p=hash_similarity(a[i],b[j]);if(p>=threshold&&p>r.percent)r={i,j,p};}
     return r;
   }));
 }
 SimilarityResult best; for(auto&j:jobs){auto r=j.get();if(r.percent>best.percent)best=r;} return best;
}
}
