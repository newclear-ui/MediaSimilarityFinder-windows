#include "scan_pipeline.h"
#include "backend_log.h"
#include "candidate_index.h"
#include "similarity.h"
#include "image_verify.h"
#include "video_fingerprint.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <sstream>
#include <thread>
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
 for(auto u:xFull) if(hash_usable(u)) for(auto v:yFull) if(hash_usable(v)) z=std::max(z,hash_similarity(u,v));
 if(a.kind==MediaKind::Image || a.kind==MediaKind::Video){
  const std::uint64_t xCrop[][2]={{a.crop4x3,a.mirrorCrop4x3},{a.crop1x1,a.mirrorCrop1x1},{a.crop9x16,a.mirrorCrop9x16}};
  const std::uint64_t yCrop[][2]={{b.crop4x3,b.mirrorCrop4x3},{b.crop1x1,b.mirrorCrop1x1},{b.crop9x16,b.mirrorCrop9x16}};
  for(int r=0;r<3;++r){for(auto u:xCrop[r])if(hash_usable(u))for(auto v:yFull)if(hash_usable(v))z=std::max(z,hash_similarity(u,v));for(auto u:xFull)if(hash_usable(u))for(auto v:yCrop[r])if(hash_usable(v))z=std::max(z,hash_similarity(u,v));for(auto u:xCrop[r])if(hash_usable(u))for(auto v:yCrop[r])if(hash_usable(v))z=std::max(z,hash_similarity(u,v));}
 } return z;
}
// Full-frame-only verdict (normal+mirror). Crop-only similarities must earn
// temporal confirmation instead of short-circuiting: center crops of unrelated
// videos routinely score at the match line while full frames disagree
// (temporal 0), so a crop-only L1 hit is evidence for verification, not a
// verdict. Mirror full-frame hits keep short-circuit rights (mirrored
// same-framing duplicates are high-confidence).
static double bestFull(const MediaFile&a,const MediaFile&b){
 double z=0; const std::uint64_t xFull[]={a.fingerprint,a.mirrorFingerprint}; const std::uint64_t yFull[]={b.fingerprint,b.mirrorFingerprint};
 for(auto u:xFull) if(hash_usable(u)) for(auto v:yFull) if(hash_usable(v)) z=std::max(z,hash_similarity(u,v));
 return z;
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
     // Live video path is Hamming-only by design (temporal stays exclusive to
     // the final analyze pass). Never stream an unverified crop-only video
     // hit: full-frame hits stream immediately, crop-only pairs wait for the
     // final temporal verdict instead of flashing as duplicates in the UI.
     if(f.kind==MediaKind::Video&&bestFull(f,o)<threshold) return;
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
ScanStats ScanPipeline::analyze(unsigned maxDistance, const MatchCallback& onMatch, const StopCheck& stop,
                                 const std::vector<char>* changedFiles, bool skipBothChangedImages,
                                 std::size_t sliceGroups,
                                 const std::function<void(int,std::size_t,std::size_t)>* onSlice,
                                 std::size_t startImageGroups){
   ScanStats s; s.files=files_.size();
  // D9a: the analyze total is the parent of every sub-stage below, so the
  // sub-stages are defined as non-overlapping slices of it. index/verify/video
  // are accumulated at their own call sites; scan is the remainder. That
  // makes index+scan+verify+video equal the total by construction, which is
  // what keeps the sum from ever exceeding analyzeMs.
  const auto analyzeT0=std::chrono::steady_clock::now();
  const auto msSince=[](const std::chrono::steady_clock::time_point& t){
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count(); };
  double indexMs=0,verifyMs=0,videoMs=0;
  AnalyzeTelemetry tel;
  s.analyze.analyzeRan=true;
 imageIdx_.clear(); videoIdx_.clear(); vc4_.clear(); vc1_.clear(); vc916_.clear(); c4_.clear(); c1_.clear(); c916_.clear();
 imageMap_.clear(); videoMap_.clear(); imageMap_.reserve(files_.size()); videoMap_.reserve(files_.size());
  const auto indexT0=std::chrono::steady_clock::now();
  std::size_t withCrops=0;
  for(std::size_t i=0;i<files_.size();++i){const auto&f=files_[i];if(!f.fingerprint)continue; if(f.crop4x3||f.crop1x1||f.crop9x16)++withCrops; if(f.kind==MediaKind::Image){indexFile(imageIdx_,c4_,c1_,c916_,i,f);imageMap_.push_back(i);}else if(f.kind==MediaKind::Video){indexFile(videoIdx_,vc4_,vc1_,vc916_,i,f);videoMap_.push_back(i); for(auto a:f.anchors) if(a) videoIdx_.add(i,a);}++s.indexed;}
  indexMs=msSince(indexT0);
  // 0.9.4.86 determinism diagnostic: the candidate set is a pure function of
  // files_ (fingerprints + crops), so two runs over one unchanged dataset must
  // report identical numbers here. A difference proves the DB crop columns
  // differ between runs, not a code nondeterminism.
  backendLogLine(std::string("scanIndex files=")+std::to_string(files_.size())+
    " indexed="+std::to_string(s.indexed)+" withCrops="+std::to_string(withCrops)+
    " imageIdx="+std::to_string(imageIdx_.size())+" c4="+std::to_string(c4_.size())+
    " c1="+std::to_string(c1_.size())+" c916="+std::to_string(c916_.size()));
 const auto possible=[](std::size_t n){return n>1?n*(n-1)/2:0;};s.possiblePairs=possible(imageMap_.size())+possible(videoMap_.size());
 // Stream candidate pairs instead of materializing the output of all eight indexes.
 // This is important for bucket-heavy datasets where the pair count can be millions.
  const double threshold=thresholdFor(maxDistance);
   auto best=[&](const MediaFile&a,const MediaFile&b){ return bestMatch(a,b); };
   // 0.9.4.81 mode filter: decides whether this candidate pair is (re)verified
   // in the final pass. changedFiles==nullptr verifies everything, exactly as
   // before (Sequential/B). Otherwise only pairs touching a changed file are
   // verified; with skipBothChangedImages, image pairs where BOTH sides changed
   // are also skipped (the live streaming pass already emitted them). Verdicts
   // never change -- only which pairs this pass revisits.
   auto included=[&](std::size_t i,std::size_t j)->bool{
     if(!changedFiles) return true;
     const bool ci = i<changedFiles->size() && (*changedFiles)[i];
     const bool cj = j<changedFiles->size() && (*changedFiles)[j];
     if(i<files_.size() && files_[i].kind==MediaKind::Video) return ci||cj;
     if(skipBothChangedImages) return ci!=cj;
     return ci||cj;
   };
  // Anchor gate: best() above only sees the single XOR/mirror/crop values, so
  // a re-encoded pair whose XORs drifted apart can never reach temporal
  // through it. Frame anchors carry per-frame evidence instead: if any anchor
  // pair is close, the pair earns the same temporal verification (which must
  // still pass threshold to yield). Runs only on the miss path, and only for
  // videos that actually carry anchors.
  auto anchorSim=[&](const MediaFile&a,const MediaFile&b)->double{
    if(a.anchors.empty()||b.anchors.empty()) return 0;
    double z=0;
    for(auto u:a.anchors){ if(!hash_usable(u))continue; for(auto v:b.anchors){ if(!hash_usable(v))continue; z=std::max(z,hash_similarity(u,v)); } }
    return z;
  };
  VideoFingerprintEngine temporalEngine;
  // Shared cache-backed engine when provided (same verdicts, skips re-decode
  // on cache hits); otherwise a local engine that always decodes.
  const VideoFingerprintEngine& te = temporalEngine_ ? *temporalEngine_ : temporalEngine;
  auto temporal=[&](const MediaFile& f, VideoFingerprint& vf, VideoCropFingerprint& cf)->bool{
    return te.buildFull(f.path, vf, cf, 96);
  };
  std::unordered_set<std::uint64_t> seen;
  std::size_t imageFullCandidates=0,videoFullCandidates=0;
  bool dedupCrop=false;
  // Cooperative cancellation: candidate-pair loops can run into the millions
  // (plus a video re-decode per video pair), so poll periodically. Without
  // this, stop/pause during the final analyze phase did nothing and users had
  // to force-quit ??losing every match streamed so far. The throw is caught
  // below; analyze() always returns partial stats, never propagates.
  struct LocalCancel {};
  struct VideoTask { std::size_t i=0, j=0; };
  struct VideoResult { std::size_t i=0, j=0; bool verified=false, matched=false; double percent=0; };
  std::vector<VideoTask> pendingVideo;
  std::size_t sincePoll=0;
  const auto poll=[&]{
    if(stop && ((++sincePoll & 1023)==0) && stop()) throw LocalCancel{};
  };
  auto emitVideo=[&](const VideoResult& vr){
    if(vr.verified) ++s.videoTemporalChecks;
    if(vr.matched){ MediaMatch match{vr.i,vr.j,vr.percent}; if(onMatch) onMatch(match); else s.matches.push_back(match); ++s.groups; }
  };
  auto flushVideo=[&](){
    if(pendingVideo.empty()) return;
    // D9a: the temporal stage is entered only when there is real work, so
    // videoMs is a measurement rather than a structural zero. Its time is
    // accumulated and subtracted from the scan remainder below, because
    // flushVideo also runs *inside* the candidate loop. The scope guard adds
    // the elapsed time on every exit path, including the prompt-stop return
    // below, so a cancelled flush is not silently recorded as 0 ms.
    tel.videoStageEntered=true;
    const auto vt0=std::chrono::steady_clock::now();
    struct VideoTimeGuard { const std::chrono::steady_clock::time_point& t0; double& acc; ~VideoTimeGuard(){ acc += std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count(); } } vguard{vt0, videoMs};
    unsigned hw=std::thread::hardware_concurrency(); if(hw<2)hw=2;
    const std::size_t chunk=64;
    for(std::size_t b=0;b<pendingVideo.size();b+=chunk){
      poll();
      // Prompt stop: abandon not-yet-started chunks instead of grinding
      // through thousands of re-decodes after the user hit stop.
      if(stop && stop()){ pendingVideo.clear(); return; }
      const std::size_t e=std::min(pendingVideo.size(),b+chunk);
      const std::size_t n=e-b;
      const unsigned w=std::min<unsigned>(hw,static_cast<unsigned>(n));
      const std::size_t per=(n+w-1)/w;
      std::vector<std::future<std::vector<VideoResult>>> futs; futs.reserve(w);
      for(unsigned k=0;k<w;++k){
        const std::size_t sb=b+k*per, se=std::min(e,sb+per);
        if(sb>=se) break;
        futs.emplace_back(std::async(std::launch::async,[&,sb,se](){
          std::vector<VideoResult> out; out.reserve(se-sb);
          for(std::size_t t=sb;t<se;++t){
            if(stop && stop()) break;
            const auto tk=pendingVideo[t];
            VideoResult vr; vr.i=tk.i; vr.j=tk.j;
            VideoFingerprint ai,bi;VideoCropFingerprint ac,bc;
            if(temporal(files_[tk.i],ai,ac)&&temporal(files_[tk.j],bi,bc)){
              vr.verified=true;
              VideoSimilarityOptions options{threshold,8,2,2.0}; options.gpu=videoGpu_; options.gpuActivity=videoGpuActivity_;
              const double ts=video_crop_similarity(ai,ac,bi,bc,options);
              if(ts>=threshold){ vr.matched=true; vr.percent=ts; }
            }
            out.push_back(vr);
          }
          return out;
        }));
      }
      for(auto& fu:futs) for(const auto& vr:fu.get()) emitVideo(vr);
    }
    pendingVideo.clear();
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
    if(!included(i,j))return;
     double sim=best(files_[i],files_[j]);
     if(!isVideo){
      if(sim>=threshold){
       // D9a: time the verification call itself, including the kFast
       // short-circuit, so analyzeVerifyMs and verifyCalls share a numerator
       // and denominator for msPerVerifyCall.
       const auto vt1=std::chrono::steady_clock::now();
       const double v=verifyImagePair(files_[i].path,files_[j].path,true,sim,threshold,&tel);
       verifyMs+=msSince(vt1);
       if(v>=threshold){MediaMatch match{i,j,v}; if(onMatch) onMatch(match); else s.matches.push_back(match); ++s.groups;}
       return;
      }
     } else {
      // Video short-circuit needs a full-frame hit. Crop-only hits fall through
      // to the trigger/temporal path below, where duration, anchors, DTW, and
      // SSIM decide (crop_temporal_score still catches true cropped duplicates).
      const double full=bestFull(files_[i],files_[j]);
      if(full>=threshold){
       const auto vt1=std::chrono::steady_clock::now();
       const double v=verifyImagePair(files_[i].path,files_[j].path,false,full,threshold,&tel);
       verifyMs+=msSince(vt1);
       if(v>=threshold){MediaMatch match{i,j,full}; if(onMatch) onMatch(match); else s.matches.push_back(match); ++s.groups;}
       return;
      }
     }
if(isVideo){
      const double trigger=std::max(0.0,threshold-12.0);
      double gate=sim;
      if(gate<trigger) gate=std::max(gate,anchorSim(files_[i],files_[j]));
       const bool anchorMatched=anchorSim(files_[i],files_[j])>=trigger;
       if(gate>=trigger&&(durationGate(files_[i],files_[j])||anchorMatched)){
        pendingVideo.push_back({i,j});
        ++tel.videoTemporalPairs;
        if(pendingVideo.size()>=4096) flushVideo();
      }
    }
 };
  // 0.9.4.81 B-slice: windowed variant of the pair enumeration used for the
  // crop passes (phase 2). Group boundaries keep a window from splitting a
  // group; stop is honored between windows.
  const auto consumeWindowed=[&](const CandidateIndex& idx){
   const std::vector<std::size_t> bnd=idx.groupBoundaries();
   const std::size_t g=bnd.size()>0?bnd.size()-1:0;
   if(g==0) return;
   const std::size_t win=sliceGroups>0?sliceGroups:g;
   for(std::size_t w0=0;w0<g;w0+=win){
    const std::size_t w1=std::min(g,w0+win);
    idx.forEachCandidatePairInRange(bnd[w0],bnd[w1],maxDistance,process);
    if(onSlice) (*onSlice)(2,w1,g);
    if(stop && stop()) throw LocalCancel{};
   }
  };
  // Parallel full-image pass. Image candidate enumeration is the analyze hot
  // loop on duplicate-heavy datasets: for every candidate it recomputes a pure
  // Hamming verdict (bestMatch is read-only; verifyImagePair is internally
  // synchronized) and, for grey-zone pairs, runs verification.
  // 0.9.4.80: dynamic group dispensing replaces contiguous ranges. Contiguous
  // ranges starve workers when pairs concentrate in one group (one worker
  // grinds for tens of minutes while the rest idle). Workers now claim whole
  // groups atomically; the merge below reorders segments by group index, so
  // the emitted match sequence stays identical to the sequential pass
  // (verdict parity, deterministic order). Worker count follows the machine
  // (the old 16-cap left cores idle); no policy gate here, same as before.
  // 0.9.4.80 is UNVERIFIED (no compile/test: production scan owns the
  // machine) — parity re-check is queued as the first gate on rebuild.
  auto consumeImageParallel=[&](){
   const std::size_t total=imageIdx_.size();
   if(total==0) return;
   const std::vector<std::size_t> bounds=imageIdx_.groupBoundaries();
   const std::size_t groups=bounds.size()>0?bounds.size()-1:0;
   if(groups==0) return;
   unsigned hw=std::thread::hardware_concurrency(); if(hw<1)hw=1;
   // 0.9.4.81 B-slice: process image groups in windows of sliceGroups (0 = one
   // window). After each window onSlice(groupsDone, groupsTotal) lets the caller
   // checkpoint matches and persist a resume frontier; stop is honored between
   // windows. startImageGroups skips already-verified leading groups on resume
   // (their pairs come from the persisted match set). The dynamic group
   // dispenser and group-ordered merge are per-window, so the emitted sequence
   // stays identical to the sequential pass.
   const std::size_t window = sliceGroups>0 ? sliceGroups : groups;
   const std::size_t g0 = std::min(startImageGroups, groups);
   const auto benchP0=std::chrono::steady_clock::now();
   std::size_t shardMaxPairs=0; double shardMaxWall=0, shardSumWall=0; std::size_t shardActive=0;
   std::size_t lastJobs=0;
   for(std::size_t w0=g0; w0<groups; w0+=window){
    const std::size_t w1=std::min(groups, w0+window);
    const std::size_t wg=w1-w0;
    const std::size_t jobs=std::min<std::size_t>(hw,wg);
    lastJobs=jobs;
    struct Part {
      std::size_t candidates=0;
      double verifyMs=0;
      double wallMs=0;
      AnalyzeTelemetry tel;
      std::vector<std::size_t> segGroups;
      std::vector<std::vector<MediaMatch>> segMatches;
    };
    std::vector<Part> parts(jobs);
    std::vector<std::future<void>> futs; futs.reserve(jobs);
    std::atomic<std::size_t> nextGroup{w0};
    for(std::size_t p=0;p<jobs;++p){
     futs.emplace_back(std::async(std::launch::async,[&,p]{
      Part& pr=parts[p];
      const auto wt0=std::chrono::steady_clock::now();
      std::size_t sincePoll=0;
      try{
       for(;;){
        const std::size_t g=nextGroup.fetch_add(1,std::memory_order_relaxed);
        if(g>=w1) break;
        const std::size_t b=bounds[g], e=bounds[g+1];
        std::vector<MediaMatch> seg;
        imageIdx_.forEachCandidatePairInRange(b,e,maxDistance,[&](std::size_t i,const Candidate& c){
         if(stop && ((++sincePoll & 1023)==0) && stop()) throw LocalCancel{};
         auto j=c.index; if(i==j) return; if(i>j) std::swap(i,j);
         ++pr.candidates;
         if(i>=files_.size()||j>=files_.size()||files_[i].kind!=files_[j].kind) return;
         if(!included(i,j)) return;
         const double sim=best(files_[i],files_[j]);
         if(sim>=threshold){
          const auto vt1=std::chrono::steady_clock::now();
          const double v=verifyImagePair(files_[i].path,files_[j].path,true,sim,threshold,&pr.tel);
          pr.verifyMs+=msSince(vt1);
          if(v>=threshold) seg.push_back(MediaMatch{i,j,v});
         }
        });
        pr.segGroups.push_back(g);
        pr.segMatches.push_back(std::move(seg));
       }
      }catch(const LocalCancel&){}
      pr.wallMs=msSince(wt0);
     }));
    }
    for(auto& f:futs) f.get();
    // Deterministic merge within the window: emit this window's groups in order.
    std::vector<std::vector<MediaMatch>*> byGroup(wg,nullptr);
    for(Part& pr:parts)
     for(std::size_t k=0;k<pr.segGroups.size();++k)
      byGroup[pr.segGroups[k]-w0]=&pr.segMatches[k];
    for(Part& pr:parts){
     s.candidates+=pr.candidates;
     verifyMs+=pr.verifyMs;
     addAnalyzeTelemetry(tel,pr.tel);
     if(!pr.segGroups.empty()){ ++shardActive; shardSumWall+=pr.wallMs; shardMaxWall=std::max(shardMaxWall,pr.wallMs); }
     shardMaxPairs=std::max(shardMaxPairs,pr.candidates);
    }
    for(std::size_t g=w0;g<w1;++g){
     if(!byGroup[g-w0]) continue;
     for(auto& m:*byGroup[g-w0]){ if(onMatch) onMatch(m); else s.matches.push_back(m); ++s.groups; }
    }
    if(onSlice) (*onSlice)(0, w1, groups);
    if(stop && stop()) throw LocalCancel{};
   }
   // Shard balance telemetry: proves (or disproves) the straggler fix from
   // the backend log alone, without a profiler attached.
   {
    const double totalWall=msSince(benchP0);
    std::ostringstream sh;
    sh<<"analyzeShards jobs="<<lastJobs<<" active="<<shardActive
      <<" candidates="<<s.candidates<<" maxPairsPerShard="<<shardMaxPairs
      <<" maxShardWallMs="<<(long long)shardMaxWall
      <<" avgShardWallMs="<<(shardActive?(long long)(shardSumWall/shardActive):0)
      <<" totalWallMs="<<(long long)totalWall;
    backendLogLine(sh.str());
   }
  };
  // Full indexes are authoritative first. If they already cover every possible pair
  // of a media kind, crop indexes cannot add anything and are skipped entirely.
  try {
  const auto imageBefore=s.candidates; consumeImageParallel(); imageFullCandidates=s.candidates-imageBefore;
  const auto videoBefore=s.candidates;
  {
   // 0.9.4.81 B-slice: window the video full pass (phase 1) and decode each
   // window's temporal pairs immediately (flushVideo) so progress is real and a
   // stop is responsive between windows.
   const std::vector<std::size_t> vb=videoIdx_.groupBoundaries();
   const std::size_t vg=vb.size()>0?vb.size()-1:0;
   const std::size_t vwin=sliceGroups>0?sliceGroups:vg;
   for(std::size_t w0=0; w0<vg; w0+=vwin){
    const std::size_t w1=std::min(vg,w0+vwin);
    videoIdx_.forEachCandidatePairInRange(vb[w0],vb[w1],maxDistance,process);
    flushVideo();
    if(onSlice) (*onSlice)(1,w1,vg);
    if(stop && stop()) throw LocalCancel{};
   }
  }
  videoFullCandidates=s.candidates-videoBefore;
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
    if(!imageComplete){consumeWindowed(c4_);consumeWindowed(c1_);consumeWindowed(c916_);}
    if(!videoComplete){consumeWindowed(vc4_);consumeWindowed(vc1_);consumeWindowed(vc916_);}
  }
  flushVideo();
  } catch (const LocalCancel&) {
    // Partial stats (and every match already streamed via onMatch) survive;
    // the caller observes the stop through its own control flag.
  }
  if(s.possiblePairs)s.candidateReductionPercent=100.0*(1.0-(double)s.candidates/s.possiblePairs);
  // D9a: close out the stage split. scan is deliberately the remainder of the
  // analyze total after index, verify and video, so the four slices sum to the
  // total exactly and can never exceed it -- which is what the D9a timing
  // rule requires. It also means scan honestly absorbs the unattributed
  // control overhead (poll, callbacks, bookkeeping) rather than pretending
  // that overhead belongs to a stage it was never measured in.
  const double totalMs=msSince(analyzeT0);
  // D9a: the four slices must stay non-overlapping and sum to at most the
  // analyze total. Parallel verify wall times are summed across workers, so on
  // a saturated machine they can add up past that total; cap verify at the
  // budget still available so the slices keep the sequential invariant by
  // construction (scan is then the non-negative remainder as before).
  const double verifyCap=std::max(0.0,totalMs-indexMs-videoMs);
  if(verifyMs>verifyCap) verifyMs=verifyCap;
  double scanMs=totalMs-indexMs-verifyMs-videoMs;
  if(scanMs<0) scanMs=0;   // timer noise guard; never report negative time
  s.analyze.indexMs=indexMs;
  s.analyze.scanMs=scanMs;
  s.analyze.verifyMs=verifyMs;
  s.analyze.videoMs=videoMs;
  s.analyze.verifyCalls=tel.verifyCalls;
  s.analyze.verifyDecodeMisses=tel.verifyDecodeMisses;
  s.analyze.verifyCacheHits=tel.verifyCacheHits;
  s.analyze.ssimEvals=tel.ssimEvals;
  s.analyze.frameSsimEvals=tel.frameSsimEvals;
  s.analyze.videoTemporalPairs=tel.videoTemporalPairs;
  // D9c: close out the verify breakdown the same way. The seven measured
  // stages are disjoint code regions, so "other" is the remainder of the
  // verify total after them. It absorbs the per-call control work the timers
  // do not wrap (kFast short-circuit, argument checks, the GrayImage
  // assignments) rather than pretending that work belongs to a stage it was
  // never measured in. Because it is a remainder, a mis-scoped timer would
  // make it negative, which the benchmark report and the test both check.
  s.analyze.verifyKeyMs=tel.verifyKeyMs;
  s.analyze.verifyDecodeMs=tel.verifyDecodeMs;
  s.analyze.verifyCacheStoreMs=tel.verifyCacheStoreMs;
  s.analyze.verifyCacheCopyMs=tel.verifyCacheCopyMs;
  s.analyze.verifyCropMs=tel.verifyCropMs;
  s.analyze.verifyFlipMs=tel.verifyFlipMs;
  s.analyze.verifyFrameSsimMs=tel.verifyFrameSsimMs;
  double otherMs=verifyMs-tel.verifyKeyMs-tel.verifyDecodeMs-tel.verifyCacheStoreMs
                 -tel.verifyCacheCopyMs-tel.verifyCropMs-tel.verifyFlipMs-tel.verifyFrameSsimMs;
  if(otherMs<0) otherMs=0;   // timer noise guard; never report negative time
  s.analyze.verifyOtherMs=otherMs;
  s.analyze.verifyBufferLookups=tel.verifyBufferLookups;
  s.analyze.verifyQuickHashReads=tel.verifyQuickHashReads;
  s.analyze.verifyQuickHashBytes=tel.verifyQuickHashBytes;
  s.analyze.verifyDecodes=tel.verifyDecodes;
  s.analyze.verifyCacheCopies=tel.verifyCacheCopies;
  s.analyze.verifyCropCalls=tel.verifyCropCalls;
  s.analyze.verifyFlipCalls=tel.verifyFlipCalls;
  // D9d: decode sub-stage breakdown and cache-mutex accounting. The mutex wait
  // and hold are reported side by side rather than summed, because the verdict
  // depends on their ratio.
   s.analyze.decode=tel.decode;
   // D3: the same per-call split. Without these two copies the fields above
   // stay zero in the report even though the verify path filled them, because
   // this function transfers the telemetry member by member.
   s.analyze.decodeFull=tel.decodeFull;
   s.analyze.decodeAspect=tel.decodeAspect;
   s.analyze.cacheMutexWaitMs=tel.cacheMutexWaitMs;
  s.analyze.cacheMutexHoldMs=tel.cacheMutexHoldMs;
  s.analyze.cacheMutexAcquires=tel.cacheMutexAcquires;
  return s;
}
const std::vector<MediaFile>& ScanPipeline::files()const{return files_;}
}
