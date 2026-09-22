#include "scan_pipeline.h"
#include "candidate_index.h"
#include "similarity.h"
#include "video_fingerprint.h"
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
namespace msf {
void ScanPipeline::add(const MediaFile& f){files_.push_back(f);}
ScanStats ScanPipeline::analyze(unsigned maxDistance){
 return analyze(maxDistance, {});
}
ScanStats ScanPipeline::analyze(unsigned maxDistance, const MatchCallback& onMatch){
 ScanStats s; s.files=files_.size(); CandidateIndex imageIdx,videoIdx,vc4,vc1,vc916,c4,c1,c916; std::vector<std::size_t> imageMap,videoMap; imageMap.reserve(files_.size());videoMap.reserve(files_.size());
 for(std::size_t i=0;i<files_.size();++i){const auto&f=files_[i];if(!f.fingerprint)continue; if(f.kind==MediaKind::Image){imageIdx.add(i,f.fingerprint);if(f.mirrorFingerprint)imageIdx.add(i,f.mirrorFingerprint);if(f.crop4x3)c4.add(i,f.crop4x3);if(f.crop1x1)c1.add(i,f.crop1x1);if(f.crop9x16)c916.add(i,f.crop9x16);imageMap.push_back(i);}else if(f.kind==MediaKind::Video){videoIdx.add(i,f.fingerprint);if(f.mirrorFingerprint)videoIdx.add(i,f.mirrorFingerprint);if(f.crop4x3)vc4.add(i,f.crop4x3);if(f.crop1x1)vc1.add(i,f.crop1x1);if(f.crop9x16)vc916.add(i,f.crop9x16);videoMap.push_back(i);}++s.indexed;}
 const auto possible=[](std::size_t n){return n>1?n*(n-1)/2:0;};s.possiblePairs=possible(imageMap.size())+possible(videoMap.size());
 // Stream candidate pairs instead of materializing the output of all eight indexes.
 // This is important for bucket-heavy datasets where the pair count can be millions.
 const double threshold=100.0-100.0*std::min<unsigned>(64,maxDistance)/64.0;
 auto best=[&](const MediaFile&a,const MediaFile&b){
  double z=0; const std::uint64_t xFull[]={a.fingerprint,a.mirrorFingerprint}; const std::uint64_t yFull[]={b.fingerprint,b.mirrorFingerprint};
  for(auto u:xFull) if(u) for(auto v:yFull) if(v) z=std::max(z,hash_similarity(u,v));
  if(a.kind==MediaKind::Image){
   const std::uint64_t xCrop[][2]={{a.crop4x3,a.mirrorCrop4x3},{a.crop1x1,a.mirrorCrop1x1},{a.crop9x16,a.mirrorCrop9x16}};
   const std::uint64_t yCrop[][2]={{b.crop4x3,b.mirrorCrop4x3},{b.crop1x1,b.mirrorCrop1x1},{b.crop9x16,b.mirrorCrop9x16}};
   for(int r=0;r<3;++r){for(auto u:xCrop[r])if(u)for(auto v:yFull)if(v)z=std::max(z,hash_similarity(u,v));for(auto u:xFull)if(u)for(auto v:yCrop[r])if(v)z=std::max(z,hash_similarity(u,v));for(auto u:xCrop[r])if(u)for(auto v:yCrop[r])if(v)z=std::max(z,hash_similarity(u,v));}
  } else if(a.kind==MediaKind::Video){
   const std::uint64_t xCrop[][2]={{a.crop4x3,a.mirrorCrop4x3},{a.crop1x1,a.mirrorCrop1x1},{a.crop9x16,a.mirrorCrop9x16}};
   const std::uint64_t yCrop[][2]={{b.crop4x3,b.mirrorCrop4x3},{b.crop1x1,b.mirrorCrop1x1},{b.crop9x16,b.mirrorCrop9x16}};
   for(int r=0;r<3;++r){for(auto u:xCrop[r])if(u)for(auto v:yFull)if(v)z=std::max(z,hash_similarity(u,v));for(auto u:xFull)if(u)for(auto v:yCrop[r])if(v)z=std::max(z,hash_similarity(u,v));for(auto u:xCrop[r])if(u)for(auto v:yCrop[r])if(v)z=std::max(z,hash_similarity(u,v));}
  } return z; };
 VideoFingerprintEngine temporalEngine;
 std::unordered_map<std::string,VideoFingerprint> baseCache; std::unordered_map<std::string,VideoCropFingerprint> cropCache;
 auto temporal=[&](const MediaFile& f, VideoFingerprint& vf, VideoCropFingerprint& cf)->bool{
   auto it=baseCache.find(f.path); if(it==baseCache.end()){VideoFingerprint b;if(!temporalEngine.build(f.path,b))return false;it=baseCache.emplace(f.path,std::move(b)).first;} vf=it->second;
   auto ic=cropCache.find(f.path); if(ic==cropCache.end()){VideoCropFingerprint c;if(!temporalEngine.buildCropAware(f.path,vf,c,96))return false;ic=cropCache.emplace(f.path,std::move(c)).first;} cf=ic->second; return true;
 };
 std::unordered_set<std::uint64_t> seen;
 std::size_t imageFullCandidates=0,videoFullCandidates=0;
 bool dedupCrop=false;
 const auto process=[&](std::size_t rawA,const Candidate& c){
   auto i=rawA,j=c.index;if(i==j)return;if(i>j)std::swap(i,j);
   if(dedupCrop){
     const std::uint64_t key=(static_cast<std::uint64_t>(i)<<32)^static_cast<std::uint64_t>(j);
     if(!seen.insert(key).second)return;
   }
   ++s.candidates;
   if(i>=files_.size()||j>=files_.size()||files_[i].kind!=files_[j].kind)return;
   double sim=best(files_[i],files_[j]);
   if(sim>=threshold){MediaMatch match{i,j,sim}; if(onMatch) onMatch(match); else s.matches.push_back(match); ++s.groups;return;}
   if(files_[i].kind==MediaKind::Video){
     const double trigger=std::max(0.0,threshold-12.0);if(sim>=trigger){
       VideoFingerprint ai,bi;VideoCropFingerprint ac,bc;
       if(temporal(files_[i],ai,ac)&&temporal(files_[j],bi,bc)){double ts=video_crop_similarity(ai,ac,bi,bc,{threshold,8,2});if(ts>=threshold){MediaMatch match{i,j,ts}; if(onMatch) onMatch(match); else s.matches.push_back(match); ++s.groups;}}
     }
   }
 };
 const auto consume=[&](const CandidateIndex& idx){idx.forEachCandidatePair(maxDistance,process);};
 // Full indexes are authoritative first. If they already cover every possible pair
 // of a media kind, crop indexes cannot add anything and are skipped entirely.
 const auto imageBefore=s.candidates; consume(imageIdx); imageFullCandidates=s.candidates-imageBefore;
 const auto videoBefore=s.candidates; consume(videoIdx); videoFullCandidates=s.candidates-videoBefore;
 const std::size_t imagePossible=possible(imageMap.size()), videoPossible=possible(videoMap.size());
 const bool imageComplete=(imageFullCandidates>=imagePossible), videoComplete=(videoFullCandidates>=videoPossible);
 if(!imageComplete || !videoComplete){
   dedupCrop=true;
   const std::size_t reserveHint=std::min<std::size_t>(s.possiblePairs, std::max<std::size_t>(1024, files_.size()*2));
   seen.reserve(reserveHint);
   // Seed only the pairs from incomplete full indexes. Complete kinds are skipped,
   // so their potentially enormous pair set never needs to be retained for dedup.
   if(!imageComplete) imageIdx.forEachCandidatePair(maxDistance,[&](std::size_t i,const Candidate& c){auto a=i,b=c.index;if(a>b)std::swap(a,b);seen.insert((static_cast<std::uint64_t>(a)<<32)^static_cast<std::uint64_t>(b));});
   if(!videoComplete) videoIdx.forEachCandidatePair(maxDistance,[&](std::size_t i,const Candidate& c){auto a=i,b=c.index;if(a>b)std::swap(a,b);seen.insert((static_cast<std::uint64_t>(a)<<32)^static_cast<std::uint64_t>(b));});
   if(!imageComplete){consume(c4);consume(c1);consume(c916);}
   if(!videoComplete){consume(vc4);consume(vc1);consume(vc916);}
 }
 if(s.possiblePairs)s.candidateReductionPercent=100.0*(1.0-(double)s.candidates/s.possiblePairs);
 return s;
}
const std::vector<MediaFile>& ScanPipeline::files()const{return files_;}
}
