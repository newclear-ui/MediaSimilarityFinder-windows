#include <cmath>
#include "media_search_engine.h"
#include "path_utils.h"
#include "incremental_scanner.h"
#include "media_pipeline.h"
#include "video_fingerprint.h"
#include "similarity.h"
#include <filesystem>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <thread>
#include <future>
namespace msf {
static MediaKind kindOf(const std::string&p){auto e=path_from_utf8(p).extension().string();for(char&c:e)c=(char)std::tolower((unsigned char)c);return (e==".mp4"||e==".mkv"||e==".avi"||e==".mov"||e==".webm"||e==".m4v"||e==".wmv")?MediaKind::Video:MediaKind::Image;}
static bool stopped(ScanControl* c){ if(!c) return false; while(c->pause.load()&&!c->cancel.load())std::this_thread::sleep_for(std::chrono::milliseconds(80)); return c->cancel.load(); }
struct AnalysisJob { FileState state; bool ok=false; bool changed=false; };
bool MediaSearchEngine::openIndex(const std::string& p){ managedIndexActive_=false; if(!db_.open(p)||!db_.initialize()) return false; candidateStates_=db_.all(); rebuildCandidateIndexes(); return videoEngine_.openPersistentCache(p+".video_cache.sqlite"); }
bool MediaSearchEngine::openIndexForRoot(const std::string& rootPath, const std::string& applicationDirectory){
 IndexPaths paths; if(!IndexManager::resolve(path_from_utf8(applicationDirectory),path_from_utf8(rootPath),paths)) return false;
 if(!db_.open(paths.database.string())||!db_.initialize()) return false;
 if(!videoEngine_.openPersistentCache(paths.videoCache.string())) return false;
 managedIndex_=paths; managedIndexActive_=true; candidateStates_=db_.all(); rebuildCandidateIndexes(); return true;
}
bool MediaSearchEngine::upsertFingerprint(const std::string& path, std::uint64_t fingerprint, int kind, std::uint64_t size, std::int64_t modified, std::uint64_t mirrorFingerprint, std::uint64_t crop4x3, std::uint64_t crop1x1, std::uint64_t crop9x16, std::uint64_t mirrorCrop4x3, std::uint64_t mirrorCrop1x1, std::uint64_t mirrorCrop9x16) {
 FileState x; x.path=path; x.fingerprint=fingerprint; x.mirrorFingerprint=mirrorFingerprint; x.crop4x3=crop4x3; x.crop1x1=crop1x1; x.crop9x16=crop9x16; x.mirrorCrop4x3=mirrorCrop4x3; x.mirrorCrop1x1=mirrorCrop1x1; x.mirrorCrop9x16=mirrorCrop9x16; x.kind=kind; x.size=size; x.modified=modified; if(!db_.upsert(x)) return false; candidateStates_=db_.all(); rebuildCandidateIndexes(); return true;
}
void MediaSearchEngine::rebuildCandidateIndexes(){
 imageCandidates_.clear(); videoCandidates_.clear(); videoCrop4x3_.clear(); videoCrop1x1_.clear(); videoCrop9x16_.clear(); imageCrop4x3_.clear(); imageCrop1x1_.clear(); imageCrop9x16_.clear();
 imageCandidates_.reserve(candidateStates_.size()); videoCandidates_.reserve(candidateStates_.size()); videoCrop4x3_.reserve(candidateStates_.size()); videoCrop1x1_.reserve(candidateStates_.size()); videoCrop9x16_.reserve(candidateStates_.size()); imageCrop4x3_.reserve(candidateStates_.size()); imageCrop1x1_.reserve(candidateStates_.size()); imageCrop9x16_.reserve(candidateStates_.size());
 for(std::size_t i=0;i<candidateStates_.size();++i){ const auto& x=candidateStates_[i]; if(!x.fingerprint) continue; if(x.kind==(int)MediaKind::Image){ imageCandidates_.add(i,x.fingerprint); if(x.mirrorFingerprint) imageCandidates_.add(i,x.mirrorFingerprint); if(x.crop4x3) imageCrop4x3_.add(i,x.crop4x3); if(x.crop1x1) imageCrop1x1_.add(i,x.crop1x1); if(x.crop9x16) imageCrop9x16_.add(i,x.crop9x16); } else if(x.kind==(int)MediaKind::Video){ videoCandidates_.add(i,x.fingerprint); if(x.mirrorFingerprint) videoCandidates_.add(i,x.mirrorFingerprint); if(x.crop4x3) videoCrop4x3_.add(i,x.crop4x3); if(x.crop1x1) videoCrop1x1_.add(i,x.crop1x1); if(x.crop9x16) videoCrop9x16_.add(i,x.crop9x16); } }
}
bool MediaSearchEngine::removePath(const std::string& path) { if(!db_.remove(path)) return false; candidateStates_=db_.all(); rebuildCandidateIndexes(); return true; }
std::vector<SearchMatch> MediaSearchEngine::compareFingerprint(std::uint64_t fingerprint, int kind, double thresholdPercent, const std::string& excludePath, std::uint64_t mirrorFingerprint, std::uint64_t crop4x3, std::uint64_t crop1x1, std::uint64_t crop9x16, std::uint64_t mirrorCrop4x3, std::uint64_t mirrorCrop1x1, std::uint64_t mirrorCrop9x16) const {
 std::vector<SearchMatch> out; const double threshold=std::clamp(thresholdPercent,0.0,100.0); const unsigned maxDistance=static_cast<unsigned>(std::floor(64.0*(100.0-threshold)/100.0));
 const CandidateIndex* index=(kind==(int)MediaKind::Image)?&imageCandidates_:&videoCandidates_; std::vector<Candidate> candidates=index->query(fingerprint,maxDistance);
 if(mirrorFingerprint){auto a=index->query(mirrorFingerprint,maxDistance);candidates.insert(candidates.end(),a.begin(),a.end());}
 if(kind==(int)MediaKind::Image){
   const std::pair<std::uint64_t,const CandidateIndex*> crops[]={{crop4x3,&imageCrop4x3_},{crop1x1,&imageCrop1x1_},{crop9x16,&imageCrop9x16_}};
   for(auto [h,ci]:crops) if(h){auto a=ci->query(h,maxDistance);candidates.insert(candidates.end(),a.begin(),a.end());}
   const std::uint64_t mirrors[]={mirrorCrop4x3,mirrorCrop1x1,mirrorCrop9x16}; const CandidateIndex* cis[]={&imageCrop4x3_,&imageCrop1x1_,&imageCrop9x16_};
   for(int i=0;i<3;++i) if(mirrors[i]){auto a=cis[i]->query(mirrors[i],maxDistance);candidates.insert(candidates.end(),a.begin(),a.end());}
 }
 if(kind==(int)MediaKind::Video){
   const std::pair<std::uint64_t,const CandidateIndex*> crops[]={{crop4x3,&videoCrop4x3_},{crop1x1,&videoCrop1x1_},{crop9x16,&videoCrop9x16_}};
   for(auto [h,ci]:crops) if(h){auto a=ci->query(h,maxDistance);candidates.insert(candidates.end(),a.begin(),a.end());}
   const std::uint64_t mirrors[]={mirrorCrop4x3,mirrorCrop1x1,mirrorCrop9x16}; const CandidateIndex* cis[]={&videoCrop4x3_,&videoCrop1x1_,&videoCrop9x16_};
   for(int i=0;i<3;++i) if(mirrors[i]){auto a=cis[i]->query(mirrors[i],maxDistance);candidates.insert(candidates.end(),a.begin(),a.end());}
 }
 std::sort(candidates.begin(),candidates.end(),[](const Candidate&a,const Candidate&b){return a.index==b.index?a.distance<b.distance:a.index<b.index;}); candidates.erase(std::unique(candidates.begin(),candidates.end(),[](const Candidate&a,const Candidate&b){return a.index==b.index;}),candidates.end());
 std::unordered_map<std::string,VideoFingerprint> temporalBaseCache; std::unordered_map<std::string,VideoCropFingerprint> temporalCropCache;
 auto loadTemporal=[&](const std::string& path, VideoFingerprint& vf, VideoCropFingerprint& cf)->bool{
   auto it=temporalBaseCache.find(path); if(it==temporalBaseCache.end()){VideoFingerprint b;if(!videoEngine_.build(path,b))return false;it=temporalBaseCache.emplace(path,std::move(b)).first;} vf=it->second;
   auto ic=temporalCropCache.find(path); if(ic==temporalCropCache.end()){VideoCropFingerprint c;if(!videoEngine_.buildCropAware(path,vf,c,96))return false;ic=temporalCropCache.emplace(path,std::move(c)).first;} cf=ic->second; return true;
 };
 auto bestAgainst=[&](const FileState& x){
   double best=0; const std::uint64_t qFull[]={fingerprint,mirrorFingerprint}; const std::uint64_t tFull[]={x.fingerprint,x.mirrorFingerprint};
   for(auto a:qFull)if(a)for(auto b:tFull)if(b)best=std::max(best,hash_similarity(a,b));
   if(kind==(int)MediaKind::Image){
     const std::uint64_t qc[]={crop4x3,crop1x1,crop9x16,mirrorCrop4x3,mirrorCrop1x1,mirrorCrop9x16};
     const std::uint64_t tc[]={x.crop4x3,x.crop1x1,x.crop9x16,x.mirrorCrop4x3,x.mirrorCrop1x1,x.mirrorCrop9x16};
     for(auto a:qc)if(a)for(auto b:tFull)if(b)best=std::max(best,hash_similarity(a,b));
     for(auto a:qFull)if(a)for(auto b:tc)if(b)best=std::max(best,hash_similarity(a,b));
     const std::uint64_t qSame[][2]={{crop4x3,mirrorCrop4x3},{crop1x1,mirrorCrop1x1},{crop9x16,mirrorCrop9x16}};
     const std::uint64_t tSame[][2]={{x.crop4x3,x.mirrorCrop4x3},{x.crop1x1,x.mirrorCrop1x1},{x.crop9x16,x.mirrorCrop9x16}};
     for(int r=0;r<3;++r) for(auto a:qSame[r]) if(a) for(auto b:tSame[r]) if(b) best=std::max(best,hash_similarity(a,b));
   } else if(kind==(int)MediaKind::Video){
     const std::uint64_t qc[][2]={{crop4x3,mirrorCrop4x3},{crop1x1,mirrorCrop1x1},{crop9x16,mirrorCrop9x16}};
     const std::uint64_t tc[][2]={{x.crop4x3,x.mirrorCrop4x3},{x.crop1x1,x.mirrorCrop1x1},{x.crop9x16,x.mirrorCrop9x16}};
     for(int r=0;r<3;++r) for(auto a:qc[r]) if(a) for(auto b:tc[r]) if(b) best=std::max(best,hash_similarity(a,b));
     for(int r=0;r<3;++r) for(auto a:qc[r]) if(a) for(auto b:tFull) if(b) best=std::max(best,hash_similarity(a,b));
     for(int r=0;r<3;++r) for(auto a:qFull) if(a) for(auto b:tc[r]) if(b) best=std::max(best,hash_similarity(a,b));
   }
   if(kind==(int)MediaKind::Video && !excludePath.empty() && best < threshold && best >= std::max(0.0,threshold-12.0) && (!expensiveStageGuard_ || expensiveStageGuard_())){
     VideoFingerprint qa,ta; VideoCropFingerprint qc,tc;
     if(loadTemporal(excludePath,qa,qc) && loadTemporal(x.path,ta,tc)) best=std::max(best,video_crop_similarity(qa,qc,ta,tc,{threshold,8,2}));
   }
   return best;
 };
 for(const auto& c:candidates){if(c.index>=candidateStates_.size())continue;const auto&x=candidateStates_[c.index];if(x.path==excludePath||x.fingerprint==0||x.kind!=kind)continue;double pct=bestAgainst(x);if(pct>=threshold)out.push_back({"",x.path,pct});}
 std::sort(out.begin(),out.end(),[](const SearchMatch&a,const SearchMatch&b){return a.percent>b.percent;}); return out;
}
SearchReport MediaSearchEngine::scan(const std::string& root,unsigned maxDistance,ScanControl* control){
 SearchReport r; files_.clear(); const bool tx= db_.beginTransaction(); if(!tx) return r; Scanner s; auto cur=s.scan(root,managedIndexActive_ ? managedIndex_.directory.parent_path().string() : std::string{}); r.scanned=cur.size(); auto old=db_.all(); IncrementalScanner inc; auto ch=inc.classify(cur,old);
 r.added=ch.added.size();r.modified=ch.modified.size();r.unchanged=ch.unchanged.size();r.removed=ch.deleted.size();
 for(auto&x:ch.deleted) db_.remove(x.path);
 std::unordered_map<std::string,FileState> oldByPath; oldByPath.reserve(old.size()*2+1); for(const auto&x:old) oldByPath.emplace(x.path,x);
 const int workers=recommended_worker_count(policy_,static_cast<int>(std::thread::hardware_concurrency()));
 const std::size_t gpuBatch=std::max<std::size_t>(1,recommended_gpu_batch_size(policy_,256));
 std::size_t done=0;
 MediaPipeline imagePipeline;
 std::vector<FileState> changedVideos;
 std::vector<std::string> changedImages;
  std::unordered_map<std::string,FileState> currentByPath; currentByPath.reserve(cur.size()*2+1);
 for(const auto& x:cur) currentByPath.emplace(x.path,x);
 for(const auto& x:cur){
   auto it=oldByPath.find(x.path); const bool changed=(it==oldByPath.end()||it->second.size!=x.size||it->second.modified!=x.modified);
   if(!changed) continue;
   // Remove the previous record before re-analysis. If decoding/analysis fails,
   // the stale fingerprint must not silently survive this successful scan.
   if(it!=oldByPath.end() && !db_.remove(x.path)){ if(tx) db_.rollbackTransaction(); return r; }
   if(kindOf(x.path)==MediaKind::Image){ changedImages.push_back(x.path); }
   else { FileState v=x; v.kind=(int)MediaKind::Video; changedVideos.push_back(v); }
 }
 // Images are the CUDA-accelerated path. Decode on CPU, pack normalized 32x32
 // grayscale frames, then hash them in bounded GPU batches. If CUDA is absent,
 // MediaPipeline transparently executes the same CPU pHash reference path.
 for(std::size_t base=0;base<changedImages.size();){
   if(stopped(control)) break;
   const std::size_t n=std::min(gpuBatch,changedImages.size()-base);
   std::vector<std::string> batch(changedImages.begin()+base,changedImages.begin()+base+n);
   auto results=imagePipeline.imageBatch(batch,policy_.gpuEnabled,gpuBatch);
   for(const auto& ir:results){
     FileState x; auto it=currentByPath.find(ir.path);
     if(it==currentByPath.end()) continue;
     x=it->second; x.kind=(int)MediaKind::Image; x.mirrorFingerprint=ir.mirrorFingerprint; x.crop4x3=ir.crops.a4x3; x.crop1x1=ir.crops.a1x1; x.crop9x16=ir.crops.a9x16; x.mirrorCrop4x3=ir.crops.mirrorA4x3; x.mirrorCrop1x1=ir.crops.mirrorA1x1; x.mirrorCrop9x16=ir.crops.mirrorA9x16; if(ir.usedGpu) ++r.gpuImages; if(ir.gpuFallback) ++r.gpuFallbackImages; if(ir.ok){x.fingerprint=ir.fingerprint; if(!db_.upsert(x)){ db_.rollbackTransaction(); return r; } ++r.analyzed; files_.push_back({x.path,MediaKind::Image,x.size,(std::uint64_t)x.modified,x.fingerprint,x.mirrorFingerprint,x.crop4x3,x.crop1x1,x.crop9x16,x.mirrorCrop4x3,x.mirrorCrop1x1,x.mirrorCrop9x16});}
     ++done; if(control&&control->progress)control->progress(done,cur.size(),x.path);
   }
   base+=n;
 }
 if(control&&control->cancel.load()){ if(tx) db_.rollbackTransaction(); return r; }
 // Videos retain the bounded asynchronous CPU/FFmpeg analysis path. This keeps
 // GPU image batching independent from the video decoder architecture.
 for(std::size_t base=0;base<changedVideos.size();base+=static_cast<std::size_t>(workers)){
   if(stopped(control)) break;
   std::vector<std::future<AnalysisJob>> futs;
   const std::size_t end=std::min(changedVideos.size(),base+static_cast<std::size_t>(workers));
   for(std::size_t k=base;k<end;++k){
     FileState x=changedVideos[k];
     futs.emplace_back(std::async(std::launch::async,[x,this](){
       AnalysisJob j{x,false,true}; VideoFingerprint vf;
       if(videoEngine_.build(x.path,vf)){ j.state.duration=vf.duration; std::uint64_t h=0,mh=0; for(auto v:vf.hashes) h^=v; for(auto v:vf.mirrorHashes) mh^=v; j.state.fingerprint=h; j.state.mirrorFingerprint=mh; j.state.crop4x3=vf.crop4x3; j.state.crop1x1=vf.crop1x1; j.state.crop9x16=vf.crop9x16; j.state.mirrorCrop4x3=vf.mirrorCrop4x3; j.state.mirrorCrop1x1=vf.mirrorCrop1x1; j.state.mirrorCrop9x16=vf.mirrorCrop9x16; j.ok=h!=0; }
       return j;
     }));
   }
   for(auto&f:futs){auto j=f.get(); if(j.ok){if(!db_.upsert(j.state)){ db_.rollbackTransaction(); return r; } ++r.analyzed;files_.push_back({j.state.path,(MediaKind)j.state.kind,j.state.size,(std::uint64_t)j.state.modified,j.state.fingerprint,j.state.mirrorFingerprint,j.state.crop4x3,j.state.crop1x1,j.state.crop9x16,j.state.mirrorCrop4x3,j.state.mirrorCrop1x1,j.state.mirrorCrop9x16});} ++done; if(control&&control->progress)control->progress(done,cur.size(),j.state.path);}
 }
 if(control&&control->cancel.load()){ if(tx) db_.rollbackTransaction(); return r; }
 if(tx && !db_.commitTransaction()){ db_.rollbackTransaction(); return r; }
 candidateStates_=db_.all(); rebuildCandidateIndexes();
 // Unchanged files must participate in every incremental search.
 files_.clear();
 const auto currentStates=db_.all(); files_.reserve(currentStates.size());
 for(const auto& x:currentStates) if(x.fingerprint) files_.push_back({x.path,(MediaKind)x.kind,x.size,(std::uint64_t)x.modified,x.fingerprint,x.mirrorFingerprint,x.crop4x3,x.crop1x1,x.crop9x16,x.mirrorCrop4x3,x.mirrorCrop1x1,x.mirrorCrop9x16});
 ScanPipeline pipe; for(auto&f:files_)pipe.add(f); auto st=pipe.analyze(maxDistance,[&](const MediaMatch& m){
   SearchMatchRef ref{m.left,m.right,m.percent};
   if(control && control->onMatchRef) control->onMatchRef(ref);
   if(control && control->onMatch) {
     SearchMatch sm{files_[m.left].path,files_[m.right].path,m.percent};
     control->onMatch(sm);
   }
   if(!control || control->retainMatches) {
     if(!control || control->maxRetainedMatches==0 || r.matches.size()<control->maxRetainedMatches) {
       SearchMatch sm{files_[m.left].path,files_[m.right].path,m.percent};
       r.matches.push_back(std::move(sm));
     }
   }
 }); r.candidates=st.candidates;r.groups=st.groups;r.candidateReductionPercent=st.candidateReductionPercent; if(managedIndexActive_) IndexManager::updateLastScan(managedIndex_); return r;
}
}
