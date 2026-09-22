#include "candidate_index.h"
#include <bit>
#include <algorithm>
#include <limits>
namespace msf {
unsigned CandidateIndex::popcount64(std::uint64_t x){return std::popcount(x);}
unsigned CandidateIndex::partitionBits(unsigned part){ return part<1?8:7; }
unsigned CandidateIndex::partitionShift(unsigned part){ return part==0?0:8+(part-1)*7; }
CandidateIndex::BucketKey CandidateIndex::key(std::uint64_t h,unsigned part){
 const unsigned bits=partitionBits(part), shift=partitionShift(part);
 return BucketKey{static_cast<std::uint8_t>(part),static_cast<std::uint8_t>((h>>shift)&((1u<<bits)-1u))};
}
void CandidateIndex::clear(){entries_.clear();buckets_.clear();indexGroups_.clear();}
void CandidateIndex::reserve(std::size_t expected){entries_.reserve(expected);buckets_.reserve(expected*kPartitions/2+1);indexGroups_.reserve(expected);}
void CandidateIndex::add(std::size_t i,std::uint64_t h){
 const std::size_t pos=entries_.size();
 auto [git,inserted]=indexGroups_.try_emplace(i,indexGroups_.size());
 entries_.push_back({i,h,git->second});
 for(unsigned p=0;p<kPartitions;++p) buckets_[key(h,p)].push_back(pos);
}
void CandidateIndex::addAll(const std::vector<std::uint64_t>& hashes,std::size_t base){reserve(entries_.size()+hashes.size());for(std::size_t i=0;i<hashes.size();++i)add(base+i,hashes[i]);}
void CandidateIndex::queryBKFallback(std::uint64_t h,unsigned maxDistance,std::vector<Candidate>& out) const{
 // For maxDistance > 8 the 9-part pigeonhole guarantee no longer applies;
 // exact verification falls back to scanning stored hashes. This path is
 // intentionally conservative and preserves correctness for arbitrary D.
 for(const auto&e:entries_){const unsigned d=popcount64(e.hash^h);if(d<=maxDistance)out.push_back({e.index,d});}
}
std::vector<Candidate> CandidateIndex::query(std::uint64_t h,unsigned maxDistance) const{
 maxDistance=std::min<unsigned>(64,maxDistance); std::vector<Candidate> r;
 if(entries_.empty()) return r;
 if(maxDistance>8){queryBKFallback(h,maxDistance,r);} else {
   std::vector<std::size_t> candidates;
   if(maxDistance==0){
     auto it=buckets_.find(key(h,0));
     if(it!=buckets_.end()) candidates=it->second;
   } else {
     std::size_t reserveCount=0; for(unsigned p=0;p<kPartitions;++p){auto it=buckets_.find(key(h,p));if(it!=buckets_.end())reserveCount+=it->second.size();}
     candidates.reserve(reserveCount);
     for(unsigned p=0;p<kPartitions;++p){auto it=buckets_.find(key(h,p));if(it!=buckets_.end())candidates.insert(candidates.end(),it->second.begin(),it->second.end());}
   }
   std::sort(candidates.begin(),candidates.end()); candidates.erase(std::unique(candidates.begin(),candidates.end()),candidates.end());
   for(const auto pos:candidates){const auto&e=entries_[pos];const unsigned d=popcount64(e.hash^h);if(d<=maxDistance)r.push_back({e.index,d});}
 }
 std::sort(r.begin(),r.end(),[](const Candidate&a,const Candidate&b){return a.distance==b.distance?a.index<b.index:a.distance<b.distance;});
 return r;
}
std::vector<std::vector<Candidate>> CandidateIndex::queryAll(const std::vector<std::uint64_t>& hashes,unsigned maxDistance) const{std::vector<std::vector<Candidate>>out;out.reserve(hashes.size());for(auto h:hashes)out.push_back(query(h,maxDistance));return out;}
void CandidateIndex::forEachCandidatePair(unsigned maxDistance, const std::function<void(std::size_t,const Candidate&)>& visitor) const{
 if(entries_.empty() || !visitor) return;
 maxDistance=std::min<unsigned>(64,maxDistance);
 if(maxDistance<=8){
   std::vector<std::size_t> seen(entries_.size(),std::numeric_limits<std::size_t>::max());
   std::size_t generation=0;
   std::size_t lastGroup=std::numeric_limits<std::size_t>::max();
   std::vector<std::size_t> seenGroups(indexGroups_.size(),std::numeric_limits<std::size_t>::max());
   for(std::size_t pos=0;pos<entries_.size();++pos){
     const auto&e=entries_[pos];
     // Multiple fingerprint variants (for example normal + mirror) can belong
     // to the same media index. Deduplicate partner media indices for the
     // current source media without adding an N-sized hash set per entry.
     if(e.group!=lastGroup){
       ++generation;
       lastGroup=e.group;
     }
     if(generation==std::numeric_limits<std::size_t>::max()){
       std::fill(seen.begin(),seen.end(),std::numeric_limits<std::size_t>::max());
       std::fill(seenGroups.begin(),seenGroups.end(),std::numeric_limits<std::size_t>::max());
       generation=1;
     }
     const unsigned partitionCount=(maxDistance==0)?1:kPartitions;
     for(unsigned part=0;part<partitionCount;++part){
       const auto it=buckets_.find(key(e.hash,part));
       if(it==buckets_.end()) continue;
       for(const auto otherPos:it->second){
         if(seen[otherPos]==generation) continue;
         seen[otherPos]=generation;
         const auto&other=entries_[otherPos];
         if(other.index<=e.index) continue;
         if(seenGroups[other.group]==generation) continue;
         seenGroups[other.group]=generation;
         const unsigned d=popcount64(e.hash^other.hash);
         if(d<=maxDistance) visitor(e.index,Candidate{other.index,d});
       }
     }
   }
 } else {
   for(const auto&e:entries_){
     auto c=query(e.hash,maxDistance);
     for(const auto&x:c) if(x.index>e.index) visitor(e.index,x);
   }
 }
}

std::vector<std::pair<std::size_t,Candidate>> CandidateIndex::candidatePairs(unsigned maxDistance) const{
 std::vector<std::pair<std::size_t,Candidate>> out;
 forEachCandidatePair(maxDistance,[&](std::size_t i,const Candidate& c){out.emplace_back(i,c);});
 std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){
   return a.first==b.first
     ? (a.second.distance==b.second.distance?a.second.index<b.second.index:a.second.distance<b.second.distance)
     : a.first<b.first;
 });
 return out;
}
CandidateIndex::Stats CandidateIndex::stats() const{
 Stats s; s.nodeCount=entries_.size(); s.bucketCount=buckets_.size(); for(const auto&kv:buckets_)s.storedRefs+=kv.second.size(); s.edgeCount=s.storedRefs; s.maxDepth=1; return s;
}
}
