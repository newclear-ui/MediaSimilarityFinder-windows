#include <cmath>
#include "media_search_engine.h"
#include "path_utils.h"
#include "incremental_scanner.h"
#include "media_pipeline.h"
#include "video_fingerprint.h"
#include "similarity.h"
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <future>
namespace msf {
static MediaKind kindOf(const std::string&p){auto e=path_from_utf8(p).extension().string();for(char&c:e)c=(char)std::tolower((unsigned char)c);return (e==".mp4"||e==".mkv"||e==".avi"||e==".mov"||e==".webm"||e==".m4v"||e==".wmv")?MediaKind::Video:MediaKind::Image;}
static bool stopped(ScanControl* c){ if(!c) return false; while(c->pause.load()&&!c->cancel.load())std::this_thread::sleep_for(std::chrono::milliseconds(80)); return c->cancel.load(); }
struct AnalysisJob { FileState state; bool ok=false; bool changed=false; };
bool MediaSearchEngine::openIndex(const std::string& p){ managedIndexActive_=false; if(!db_.open(p)||!db_.initialize()) return false; candidateStates_=db_.all(); rebuildCandidateIndexes(); return videoEngine_.openPersistentCache(p+".video_cache.sqlite"); }
void MediaSearchEngine::close(){ videoEngine_.closePersistentCache(); db_.close(); managedIndexActive_=false; }
bool MediaSearchEngine::openIndexForRoot(const std::string& rootPath, const std::string& applicationDirectory){
 IndexPaths paths; if(!IndexManager::resolve(path_from_utf8(applicationDirectory),path_from_utf8(rootPath),paths)) return false;
 if(!db_.open(path_to_utf8(paths.database))||!db_.initialize()) return false;
 if(!videoEngine_.openPersistentCache(path_to_utf8(paths.videoCache))) return false;
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
 SearchReport r; files_.clear(); const bool tx= db_.beginTransaction(); if(!tx) return r;
 auto old=db_.all();
 std::unordered_map<std::string,FileState> oldByPath; oldByPath.reserve(old.size()*2+1); for(const auto&x:old) oldByPath.emplace(x.path,x);
 const bool hasIgnored=control && !control->ignoredPaths.empty();
 const int workers=recommended_worker_count(policy_,static_cast<int>(std::thread::hardware_concurrency()));
 const std::size_t gpuBatch=std::max<std::size_t>(1,recommended_gpu_batch_size(policy_,256));
 const std::string excl = managedIndexActive_ ? path_to_utf8(managedIndex_.directory.parent_path()) : std::string{};
 std::size_t done=0, scanned=0, nAdded=0, nModified=0, nUnchanged=0, nRemoved=0;
 std::unordered_set<std::string> seen; seen.reserve(old.size()*2+1024);
 std::unordered_map<std::string,FileState> currentByPath;
 MediaPipeline imagePipeline;
 ScanPipeline livePipe;
 auto liveEmit=[&](const MediaMatch& m){
  if(!control || !control->onMatch) return;
  const auto& lf=livePipe.files();
  if(m.left>=lf.size()||m.right>=lf.size()) return;
  SearchMatch sm{lf[m.left].path,lf[m.right].path,m.percent};
  control->onMatch(sm);
 };
 const bool liveMatch = control && (bool)control->onMatch;
 std::vector<FileState> changedVideos;
 std::vector<std::string> imageBatch; imageBatch.reserve(gpuBatch);
 std::size_t videoBase=0;
 std::mutex queueMutex; std::condition_variable queueCv;
 std::queue<FileState> queue; std::atomic_bool walkDone{false};
 bool failed=false, cancelled=false, walkCompleted=false;
 std::size_t lastCommitDone=0, lastCommitScanned=0;
 // Checkpoints persist completed work so interruption (cancel/crash) never
 // loses the file list: commit every 500 analyzed files, and the walk
 // skeleton rows are covered by the scanned-based trigger in processOne.
 auto checkpoint=[&]()->bool{
  if(!db_.commitTransaction()){ db_.rollbackTransaction(); return false; }
  if(!db_.beginTransaction()) return false;
  lastCommitDone=done; lastCommitScanned=scanned;
  return true;
 };
 // Images are the CUDA-accelerated path. Decode on CPU, pack normalized 32x32
 // grayscale frames, then hash them in bounded GPU batches. If CUDA is absent,
 // MediaPipeline transparently executes the same CPU pHash reference path.
 // Single DB connection: only this thread touches db_/files_/r.
 auto processImageBatch=[&](std::vector<std::string>& batch)->bool{
  auto results=imagePipeline.imageBatch(batch,policy_.gpuEnabled,gpuBatch);
  for(const auto& ir:results){
   FileState x; auto it=currentByPath.find(ir.path);
   if(it==currentByPath.end()) continue;
   x=it->second; x.kind=(int)MediaKind::Image; x.mirrorFingerprint=ir.mirrorFingerprint; x.crop4x3=ir.crops.a4x3; x.crop1x1=ir.crops.a1x1; x.crop9x16=ir.crops.a9x16; x.mirrorCrop4x3=ir.crops.mirrorA4x3; x.mirrorCrop1x1=ir.crops.mirrorA1x1; x.mirrorCrop9x16=ir.crops.mirrorA9x16; if(ir.usedGpu) ++r.gpuImages; if(ir.gpuFallback) ++r.gpuFallbackImages; if(ir.ok){x.fingerprint=ir.fingerprint; if(!db_.upsert(x)){ return false; } ++r.analyzed; MediaFile mf{x.path,MediaKind::Image,x.size,(std::uint64_t)x.modified,x.fingerprint,x.mirrorFingerprint,x.crop4x3,x.crop1x1,x.crop9x16,x.mirrorCrop4x3,x.mirrorCrop1x1,x.mirrorCrop9x16,0.0}; files_.push_back(mf); if(liveMatch) livePipe.addAndMatch(mf,maxDistance,liveEmit);}
   ++done; if(control&&control->progress)control->progress(done,scanned,x.path);
  }
  if(done-lastCommitDone>=500){ if(!checkpoint()) return false; }
  return true;
 };
 // Videos retain the bounded asynchronous CPU/FFmpeg analysis path. This keeps
 // GPU image batching independent from the video decoder architecture.
 auto processVideoRange=[&](std::size_t from,std::size_t to)->bool{
  std::vector<std::future<AnalysisJob>> futs;
  for(std::size_t k=from;k<to;++k){
   FileState x=changedVideos[k];
   futs.emplace_back(std::async(std::launch::async,[x,this](){
    AnalysisJob j{x,false,true}; VideoFingerprint vf;
    if(videoEngine_.build(x.path,vf)){ j.state.duration=vf.duration; std::uint64_t h=0,mh=0; for(auto v:vf.hashes) h^=v; for(auto v:vf.mirrorHashes) mh^=v; j.state.fingerprint=h; j.state.mirrorFingerprint=mh; j.state.crop4x3=vf.crop4x3; j.state.crop1x1=vf.crop1x1; j.state.crop9x16=vf.crop9x16; j.state.mirrorCrop4x3=vf.mirrorCrop4x3; j.state.mirrorCrop1x1=vf.mirrorCrop1x1; j.state.mirrorCrop9x16=vf.mirrorCrop9x16; j.ok=h!=0; }
    return j;
   }));
  }
  for(auto&f:futs){auto j=f.get(); if(j.ok){if(!db_.upsert(j.state)){ return false; } ++r.analyzed;MediaFile mf{j.state.path,(MediaKind)j.state.kind,j.state.size,(std::uint64_t)j.state.modified,j.state.fingerprint,j.state.mirrorFingerprint,j.state.crop4x3,j.state.crop1x1,j.state.crop9x16,j.state.mirrorCrop4x3,j.state.mirrorCrop1x1,j.state.mirrorCrop9x16,j.state.duration};files_.push_back(mf); if(liveMatch) livePipe.addAndMatch(mf,maxDistance,liveEmit);} ++done; if(control&&control->progress)control->progress(done,scanned,j.state.path);}
  if(done-lastCommitDone>=500){ if(!checkpoint()) return false; }
  return true;
 };
 auto processOne=[&](FileState&& x){
  if(hasIgnored && control->ignoredPaths.find(x.path)!=control->ignoredPaths.end()){ seen.insert(x.path); return; }
  const bool isVid=(kindOf(x.path)==MediaKind::Video);
  if(control && ((isVid && !control->scanVideos) || (!isVid && !control->scanImages))){ seen.insert(x.path); return; }
  ++scanned;
  auto it=oldByPath.find(x.path); const bool changed=(it==oldByPath.end()||it->second.size!=x.size||it->second.modified!=x.modified||it->second.fingerprint==0);
  if(!changed){ ++nUnchanged; seen.insert(x.path); return; }
  if(it==oldByPath.end()) ++nAdded; else ++nModified;
  // Remove the previous record before re-analysis. If decoding/analysis fails,
  // the stale fingerprint must not silently survive this successful scan.
  if(it!=oldByPath.end() && !db_.remove(x.path)){ failed=true; return; }
  // Skeleton row first: the file list survives interruption (cancel/crash)
  // even before this file is analyzed. Unanalyzed rows carry fingerprint 0,
  // are invisible to matching, and are picked up by the fp==0 rule above.
  { FileState sk=x; sk.kind=(int)kindOf(x.path); sk.fingerprint=0; sk.mirrorFingerprint=0; sk.crop4x3=sk.crop1x1=sk.crop9x16=0; sk.mirrorCrop4x3=sk.mirrorCrop1x1=sk.mirrorCrop9x16=0; sk.duration=0;
    if(!db_.upsert(sk)){ failed=true; return; } }
  seen.insert(x.path);
  currentByPath.emplace(x.path,x);
  if(kindOf(x.path)==MediaKind::Image){
   imageBatch.push_back(x.path);
   if(imageBatch.size()>=gpuBatch){ if(!processImageBatch(imageBatch)) failed=true; imageBatch.clear(); }
   } else {
    FileState v=x; v.kind=(int)MediaKind::Video; changedVideos.push_back(std::move(v));
    while(!failed && changedVideos.size()-videoBase>=static_cast<std::size_t>(workers)){
     if(!processVideoRange(videoBase,videoBase+static_cast<std::size_t>(workers))) failed=true;
     else videoBase+=static_cast<std::size_t>(workers);
    }
   }
   if(!failed && scanned-lastCommitScanned>=1000){ if(!checkpoint()) failed=true; }
  };
 // The walker streams walked files while this thread analyzes them, so CPU/GPU
 // work overlaps the directory walk instead of waiting for it.
 std::thread walker([&]{
  Scanner s; Scanner::ScanCallbacks cb;
  if(control){
   cb.onProgress=[control](std::size_t n){ if(control->listing) control->listing(n); };
   cb.cancel=&control->cancel; cb.pause=&control->pause;
  }
  cb.onFile=[&](FileState&& f){ {std::lock_guard<std::mutex> g(queueMutex); queue.push(std::move(f));} queueCv.notify_one(); };
  s.scan_stream(root, excl, cb);
  walkDone.store(true); queueCv.notify_all();
 });
 while(!failed && !cancelled){
  FileState x; bool have=false;
  { std::unique_lock<std::mutex> g(queueMutex);
   queueCv.wait_for(g,std::chrono::milliseconds(50),[&]{return !queue.empty()||walkDone.load();});
   if(!queue.empty()){ x=std::move(queue.front()); queue.pop(); have=true; } }
  if(have) processOne(std::move(x));
  if(stopped(control)) cancelled=true;
  else if(walkDone.load() && queue.empty()){ walkCompleted=true; break; }
 }
 if(!failed && !cancelled && !imageBatch.empty()){ if(!processImageBatch(imageBatch)) failed=true; imageBatch.clear(); }
 while(!failed && !cancelled && videoBase<changedVideos.size()){
  const std::size_t n=std::min(static_cast<std::size_t>(workers),changedVideos.size()-videoBase);
  if(stopped(control)){ cancelled=true; break; }
  if(!processVideoRange(videoBase,videoBase+n)) failed=true; else videoBase+=n;
 }
 walker.join();
 if(failed){ if(tx) db_.rollbackTransaction(); r.completed=false; return r; }
 // Deleted detection needs the complete seen set: only on fully walked scans.
 // Previously indexed files that no longer exist are removed then. Ignored rows
 // are retained in the database (they reappear only when unignored and rescanned).
 if(walkCompleted){
  for(auto& o:old){ if(seen.find(o.path)!=seen.end()) continue; if(hasIgnored && control->ignoredPaths.find(o.path)!=control->ignoredPaths.end()) continue; if(!db_.remove(o.path)){ failed=true; break; } ++nRemoved; }
 }
 if(failed){ if(tx) db_.rollbackTransaction(); r.completed=false; return r; }
 // Persist everything done so far, including on cancel: partial progress is
 // kept by design (checkpoints), so interruption never loses the file list.
 if(!checkpoint()){ r.completed=false; return r; }
 r.scanned=scanned; r.added=nAdded; r.modified=nModified; r.unchanged=nUnchanged;
 r.removed=nRemoved;
 if(cancelled||(control&&control->cancel.load())) r.completed=false;
 // Final commit also closes the trailing transaction checkpoint() reopened:
 // leaving it open would make the *next* scan's BEGIN fail and return empty.
 if(tx && !db_.commitTransaction()){ db_.rollbackTransaction(); r.completed=false; return r; }
 candidateStates_=db_.all(); rebuildCandidateIndexes();
 // Unchanged files must participate in every incremental search.
 files_.clear();
 const auto currentStates=db_.all(); files_.reserve(currentStates.size());
 for(const auto& x:currentStates) if(x.fingerprint){ if(hasIgnored && control->ignoredPaths.find(x.path)!=control->ignoredPaths.end()) continue; files_.push_back({x.path,(MediaKind)x.kind,x.size,(std::uint64_t)x.modified,x.fingerprint,x.mirrorFingerprint,x.crop4x3,x.crop1x1,x.crop9x16,x.mirrorCrop4x3,x.mirrorCrop1x1,x.mirrorCrop9x16,x.duration}); }
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
