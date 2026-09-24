#include "scan_pipeline.h"
#include "candidate_index.h"
#include "similarity.h"
#include "image_verify.h"
#include "video_fingerprint.h"
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
namespace msf {
void ScanPipeline::add(const MediaFile& f){files_.push_back(f);}
void ScanPipeline::clear(){
 files_.clear(); imageMap_.clear(); videoMap_.clear();
 imageIdx_.clear(); videoIdx_.clear(); vc4_.clear(); vc1_.clear(); vc916_.clear(); c4_.clear(); c1_.clear(); c916_.clear();
}
static double thresholdFor(unsigned maxDistance){
  return 100.0-100.0*std::min<unsigned>(64,maxDistance)/64.0;
}
// Duration prefilter (viddup L2-style). Applied before the expensive temporal
// decode+DTW stage: full-length duplicates share roughly the same duration, so
// massively different lengths mean the temporal pass would only find a sparse
// sub-sequence and is skipped. Sub-clips shorter than the floor (and "unknown"
// durations) bypass the gate entirely.
constexpr double kMinDurationSeconds=1.0, kMaxDurationRatio=4.0;
static bool durationGate(const MediaFile&a,const MediaFile&b){
  if(a.duration<=0||b.duration<=0) return true;
  const double lo=std::min(a.duration,b.duration), hi=std::max(a.duration,b.duration);
  if(lo<kMinDurationSeconds) return true;
  return hi/lo<=kMaxDurationRatio;
}
// Full pairwise verdict: normal/mirror Hamming plus same-ratio crop aware
// comparison. Shared verbatim by batch analyze() and incremental addAndMatch()
// so live matches carry identical similarity values.
static double bestMatch(const MediaFile&a,const MediaFile&b){
 double z=0; const std::uint64_t xFull[]={a.fingerprint,a.mirrorFingerprint}; const std::uint64_t yFull[]={b.fingerprint,b.mirrorFingerprint};
 for(auto u:xFull) if(u) for(auto v:yFull) if(v) z=std::max(z,hash_similarity(u,v));
 if(a.kind==MediaKind::Image || a.kind==MediaKind::Video){
  const std::uint64_t xCrop[][2]={{a.crop4x3,a.mirrorCrop4x3},{a.crop1x1,a.mirrorCrop1x1},{a.crop9x16,a.mirrorCrop9x16}};
  const std::uint64_t yCrop[][2]={{b.crop4x3,b.mirrorCrop4x3},{b.crop1x1,b.mirrorCrop1x1},{b.crop9x16,b.mirrorCrop9x16}};
  for(int r=0;r<3;++r){for(auto u:xCrop[r])if(u)for(auto v:yFull)if(v)z=std::max(z,hash_similarity(u,v));for(auto u:xFull)if(u)for(auto v:yCrop[r])if(v)z=std::max(z,hash_similarity(u,v));for(auto u:xCrop[r])if(u)for(auto v:yCrop[r])if(v)z=std::max(z,hash_similarity(u,v));}
 } return z;
}
static void indexFile(CandidateIndex& full,CandidateIndex& c4,CandidateIndex& c1,CandidateIndex& c916,std::size_t idx,const MediaFile& f){
 full.add(idx,f.fingerprint); if(f.mirrorFingerprint)full.add(idx,f.mirrorFingerprint);
 if(f.crop4x3)c4.add(idx,f.crop4x3); if(f.crop1x1)c1.add(idx,f.crop1x1); if(f.crop9x16)c916.add(idx,f.crop9x16);
}
void ScanPipeline::addAndMatch(const MediaFile& f,unsigned maxDistance,const MatchCallback& onMatch){
 if(!onMatch){add(f);return;}
 const std::size_t idx=files_.size();
 if(f.fingerprint){
  const double threshold=thresholdFor(maxDistance);
  std::unordered_set<std::size_t> seenPartners;
  auto consider=[&](const CandidateIndex& ix,std::uint64_t h){
   if(!h) return;
   for(const auto& c:ix.query(h,maxDistance)){
    if(c.index>=idx) continue;
    if(!seenPartners.insert(c.index).second) continue;
     const MediaFile& o=files_[c.index];
     if(!o.fingerprint||o.kind!=f.kind) continue;
     const double sim=bestMatch(f,o);
     // Image second stage: Hamming-only verdicts let same-low-frequency false
     // positives through (dark smooth photos within D<=8). verifyImagePair
     // re-scores grey-zone pairs with SSIM; near-identical and video pairs
     // pass through untouched, decode failures fall back to Hamming.
     const double v=verifyImagePair(f.path,o.path,f.kind==MediaKind::Image,sim,threshold);
     if(v>=threshold) onMatch(MediaMatch{c.index,idx,v});
   }
  };
  if(f.kind==MediaKind::Image){
   consider(imageIdx_,f.fingerprint); consider(imageIdx_,f.mirrorFingerprint);
   consider(c4_,f.crop4x3); consider(c4_,f.mirrorCrop4x3);
   consider(c1_,f.crop1x1); consider(c1_,f.mirrorCrop1x1);
   consider(c916_,f.crop9x16); consider(c916_,f.mirrorCrop9x16);
  }else if(f.kind==MediaKind::Video){
   consider(videoIdx_,f.fingerprint); consider(videoIdx_,f.mirrorFingerprint);
   consider(vc4_,f.crop4x3); consider(vc4_,f.mirrorCrop4x3);
   consider(vc1_,f.crop1x1); consider(vc1_,f.mirrorCrop1x1);
   consider(vc916_,f.crop9x16); consider(vc916_,f.mirrorCrop9x16);
  }
 }
 files_.push_back(f);
 if(!f.fingerprint) return;
 if(f.kind==MediaKind::Image){indexFile(imageIdx_,c4_,c1_,c916_,idx,f);imageMap_.push_back(idx);}
 else if(f.kind==MediaKind::Video){indexFile(videoIdx_,vc4_,vc1_,vc916_,idx,f);videoMap_.push_back(idx);}
}
ScanStats ScanPipeline::analyze(unsigned maxDistance){
 return analyze(maxDistance, {});
}
ScanStats ScanPipeline::analyze(unsigned maxDistance, const MatchCallback& onMatch){
 return analyze(maxDistance, onMatch, {});
}
ScanStats ScanPipeline::analyze(unsigned maxDistance, const MatchCallback& onMatch, const StopCheck& stop){
  ScanStats s; s.files=files_.size();
 imageIdx_.clear(); videoIdx_.clear(); vc4_.clear(); vc1_.clear(); vc916_.clear(); c4_.clear(); c1_.clear(); c916_.clear();
 imageMap_.clear(); videoMap_.clear(); imageMap_.reserve(files_.size()); videoMap_.reserve(files_.size());
 for(std::size_t i=0;i<files_.size();++i){const auto&f=files_[i];if(!f.fingerprint)continue; if(f.kind==MediaKind::Image){indexFile(imageIdx_,c4_,c1_,c916_,i,f);imageMap_.push_back(i);}else if(f.kind==MediaKind::Video){indexFile(videoIdx_,vc4_,vc1_,vc916_,i,f);videoMap_.push_back(i); for(auto a:f.anchors) if(a) videoIdx_.add(i,a);}++s.indexed;}
 const auto possible=[](std::size_t n){return n>1?n*(n-1)/2:0;};s.possiblePairs=possible(imageMap_.size())+possible(videoMap_.size());
 // Stream candidate pairs instead of materializing the output of all eight indexes.
 // This is important for bucket-heavy datasets where the pair count can be millions.
 const double threshold=thresholdFor(maxDistance);
  auto best=[&](const MediaFile&a,const MediaFile&b){ return bestMatch(a,b); };
  // Anchor gate: best() above only sees the single XOR/mirror/crop values, so
  // a re-encoded pair whose XORs drifted apart can never reach temporal
  // through it. Frame anchors carry per-frame evidence instead: if any anchor
  // pair is close, the pair earns the same temporal verification (which must
  // still pass threshold to yield). Runs only on the miss path, and only for
  // videos that actually carry anchors.
  auto anchorSim=[&](const MediaFile&a,const MediaFile&b)->double{
    if(a.anchors.empty()||b.anchors.empty()) return 0;
    double z=0;
    for(auto u:a.anchors){ if(!u)continue; for(auto v:b.anchors){ if(!v)continue; z=std::max(z,hash_similarity(u,v)); } }
    return z;
  };
  VideoFingerprintEngine temporalEngine;
  // Shared cache-backed engine when provided (same verdicts, skips re-decode
  // on cache hits); otherwise a local engine that always decodes.
  const VideoFingerprintEngine& te = temporalEngine_ ? *temporalEngine_ : temporalEngine;
  std::unordered_map<std::string,VideoFingerprint> baseCache; std::unordered_map<std::string,VideoCropFingerprint> cropCache;
  auto temporal=[&](const MediaFile& f, VideoFingerprint& vf, VideoCropFingerprint& cf)->bool{
    auto it=baseCache.find(f.path); if(it==baseCache.end()){VideoFingerprint b;if(!te.build(f.path,b))return false;it=baseCache.emplace(f.path,std::move(b)).first;} vf=it->second;
    auto ic=cropCache.find(f.path); if(ic==cropCache.end()){VideoCropFingerprint c;if(!te.buildCropAware(f.path,vf,c,96))return false;ic=cropCache.emplace(f.path,std::move(c)).first;} cf=ic->second; return true;
  };
  std::unordered_set<std::uint64_t> seen;
  std::size_t imageFullCandidates=0,videoFullCandidates=0;
  bool dedupCrop=false;
  // Cooperative cancellation: candidate-pair loops can run into the millions
  // (plus a video re-decode per video pair), so poll periodically. Without
  // this, stop/pause during the final analyze phase did nothing and users had
  // to force-quit — losing every match streamed so far. The throw is caught
  // below; analyze() always returns partial stats, never propagates.
  struct LocalCancel {};
  std::size_t sincePoll=0;
  const auto poll=[&]{
    if(stop && ((++sincePoll & 1023)==0) && stop()) throw LocalCancel{};
  };
  const auto process=[&](std::size_t rawA,const Candidate& c){
    poll();
    auto i=rawA,j=c.index;if(i==j)return;if(i>j)std::swap(i,j);
   if(dedupCrop){
     const std::uint64_t key=(static_cast<std::uint64_t>(i)<<32)^static_cast<std::uint64_t>(j);
     if(!seen.insert(key).second)return;
   }
    ++s.candidates;
    if(i>=files_.size()||j>=files_.size()||files_[i].kind!=files_[j].kind)return;
    const bool isVideo=(files_[i].kind==MediaKind::Video);
    if(isVideo) ++s.videoCandidates;
    double sim=best(files_[i],files_[j]);
    if(sim>=threshold){
     const double v=verifyImagePair(files_[i].path,files_[j].path,!isVideo,sim,threshold);
     if(v>=threshold){MediaMatch match{i,j,v}; if(onMatch) onMatch(match); else s.matches.push_back(match); ++s.groups;}
     return;
    }
if(isVideo){
      const double trigger=std::max(0.0,threshold-12.0);
      double gate=sim;
      if(gate<trigger) gate=std::max(gate,anchorSim(files_[i],files_[j]));
      if(gate>=trigger&&durationGate(files_[i],files_[j])){
        VideoFingerprint ai,bi;VideoCropFingerprint ac,bc;
        if(temporal(files_[i],ai,ac)&&temporal(files_[j],bi,bc)){++s.videoTemporalChecks;double ts=video_crop_similarity(ai,ac,bi,bc,{threshold,8,2,2.0});if(ts>=threshold){MediaMatch match{i,j,ts}; if(onMatch) onMatch(match); else s.matches.push_back(match); ++s.groups;}}
      }
    }
 };
  const auto consume=[&](const CandidateIndex& idx){idx.forEachCandidatePair(maxDistance,process);};
  // Full indexes are authoritative first. If they already cover every possible pair
  // of a media kind, crop indexes cannot add anything and are skipped entirely.
  try {
  const auto imageBefore=s.candidates; consume(imageIdx_); imageFullCandidates=s.candidates-imageBefore;
  const auto videoBefore=s.candidates; consume(videoIdx_); videoFullCandidates=s.candidates-videoBefore;
  const std::size_t imagePossible=possible(imageMap_.size()), videoPossible=possible(videoMap_.size());
  const bool imageComplete=(imageFullCandidates>=imagePossible), videoComplete=(videoFullCandidates>=videoPossible);
  if(!imageComplete || !videoComplete){
    dedupCrop=true;
    const std::size_t reserveHint=std::min<std::size_t>(s.possiblePairs, std::max<std::size_t>(1024, files_.size()*2));
    seen.reserve(reserveHint);
    // Seed only verdict-known pairs (close in full-hash space, hence already
    // evaluated above): seeding far pairs would wrongly skip their crop
    // evaluation below, hiding crop-only duplicates.
    auto seed=[&](std::size_t i,const Candidate& c){poll();if(c.distance>maxDistance)return;auto a=i,b=c.index;if(a>b)std::swap(a,b);seen.insert((static_cast<std::uint64_t>(a)<<32)^static_cast<std::uint64_t>(b));};
    if(!imageComplete) imageIdx_.forEachCandidatePair(maxDistance,seed);
    if(!videoComplete) videoIdx_.forEachCandidatePair(maxDistance,seed);
    if(!imageComplete){consume(c4_);consume(c1_);consume(c916_);}
    if(!videoComplete){consume(vc4_);consume(vc1_);consume(vc916_);}
  }
  } catch (const LocalCancel&) {
    // Partial stats (and every match already streamed via onMatch) survive;
    // the caller observes the stop through its own control flag.
  }
  if(s.possiblePairs)s.candidateReductionPercent=100.0*(1.0-(double)s.candidates/s.possiblePairs);
  return s;
}
const std::vector<MediaFile>& ScanPipeline::files()const{return files_;}
}
