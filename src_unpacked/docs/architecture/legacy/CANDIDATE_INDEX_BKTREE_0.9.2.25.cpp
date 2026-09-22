#include "candidate_index.h"
#include <bit>
#include <algorithm>
namespace msf {
unsigned CandidateIndex::popcount64(std::uint64_t x){ return std::popcount(x); }
void CandidateIndex::clear(){ root_.reset(); size_=0; }
void CandidateIndex::reserve(std::size_t expected){ if(expected>size_){ /* BK-tree nodes own their children; reserve is an API hint for callers. */ } }
void CandidateIndex::add(std::size_t i,std::uint64_t h){
  if(!root_){ root_=std::make_unique<Node>(Node{i,h,{}}); ++size_; return; }
  Node* cur=root_.get();
  for(;;){
    const unsigned d=popcount64(cur->hash^h);
    auto it=cur->children.find(d);
    if(it==cur->children.end()){ cur->children.emplace(d,std::make_unique<Node>(Node{i,h,{}})); ++size_; return; }
    cur=it->second.get();
  }
}
void CandidateIndex::addAll(const std::vector<std::uint64_t>& hashes,std::size_t base){
  reserve(hashes.size()+size_); for(std::size_t i=0;i<hashes.size();++i) add(base+i,hashes[i]);
}
void CandidateIndex::queryNode(const Node* node,std::uint64_t hash,unsigned maxDistance,std::vector<Candidate>& out) const{
  if(!node) return;
  const unsigned d=popcount64(node->hash^hash);
  if(d<=maxDistance) out.push_back({node->index,d});
  const unsigned lo=(d>maxDistance?d-maxDistance:0), hi=std::min<unsigned>(64,d+maxDistance);
  for(const auto& kv:node->children) if(kv.first>=lo && kv.first<=hi) queryNode(kv.second.get(),hash,maxDistance,out);
}
std::vector<Candidate> CandidateIndex::query(std::uint64_t h,unsigned maxDistance) const{
  maxDistance=std::min<unsigned>(64,maxDistance); std::vector<Candidate> r;
  if(root_) queryNode(root_.get(),h,maxDistance,r);
  std::sort(r.begin(),r.end(),[](const Candidate&a,const Candidate&b){return a.distance==b.distance?a.index<b.index:a.distance<b.distance;});
  return r;
}
std::vector<std::vector<Candidate>> CandidateIndex::queryAll(const std::vector<std::uint64_t>& hashes,unsigned maxDistance) const{
  std::vector<std::vector<Candidate>> out; out.reserve(hashes.size()); for(auto h:hashes) out.push_back(query(h,maxDistance)); return out;
}
std::vector<std::pair<std::size_t,Candidate>> CandidateIndex::candidatePairs(unsigned maxDistance) const{
  maxDistance=std::min<unsigned>(64,maxDistance); std::vector<std::pair<std::size_t,Candidate>> out;
  if(!root_) return out;
  std::vector<const Node*> nodes; nodes.reserve(size_);
  std::vector<const Node*> stack{root_.get()};
  while(!stack.empty()){const Node* n=stack.back();stack.pop_back();nodes.push_back(n);for(const auto& kv:n->children)stack.push_back(kv.second.get());}
  for(const Node* n:nodes){auto c=query(n->hash,maxDistance);for(const auto& x:c)if(x.index>n->index)out.emplace_back(n->index,x);}
  std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){return a.first==b.first?(a.second.distance==b.second.distance?a.second.index<b.second.index:a.second.distance<b.second.distance):a.first<b.first;});
  return out;
}
std::size_t CandidateIndex::size() const{return size_;}
}
