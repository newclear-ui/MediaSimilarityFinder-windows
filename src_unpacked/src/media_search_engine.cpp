#include <cmath>
#include "media_search_engine.h"
#include "image_verify.h"
#include "semver.h"
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
static MediaKind kindOf(const std::string&p){return Scanner::isVideoPath(path_from_utf8(p))?MediaKind::Video:MediaKind::Image;}
static std::uint64_t foldVideoHashes(const std::vector<std::uint64_t>& values){
  std::uint64_t h=0x9e3779b97f4a7c15ULL;
  for(const auto v:values){ h^=v+0x9e3779b97f4a7c15ULL+(h<<6)+(h>>2); h=(h<<13)|(h>>51); }
  return h ? h : 1;
}
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
bool MediaSearchEngine::saveMatches(const std::vector<SearchMatch>& matches){
  std::vector<StoredMatch> s; s.reserve(matches.size());
  for(const auto& m:matches) s.push_back({m.leftPath,m.rightPath,m.percent});
  return db_.saveMatches(s);
}
static void loadVideoAnchors(const VideoFingerprintEngine& engine, MediaFile& mf);
std::vector<SearchMatch> MediaSearchEngine::loadMatches() const{
  std::vector<SearchMatch> out; const auto s=db_.loadMatches(); out.reserve(s.size());
  for(const auto& m:s) out.push_back({m.left,m.right,m.percent});
  return out;
}
bool MediaSearchEngine::revalidateMatches(ScanControl* control, int* kept, int* dropped){
  if(kept) *kept=0; if(dropped) *dropped=0;
  if(!semverLess(db_.engineVersion(), kEngineVersion)) return true; // current: nothing to do
  const auto stored=db_.loadMatches();
  if(stored.empty()){ db_.setEngineVersion(kEngineVersion); return true; }
  const auto states=db_.all();
  std::unordered_map<std::string,const FileState*> byPath; byPath.reserve(states.size()*2+1);
  for(const auto& x:states) byPath.emplace(x.path,&x);
  // Disk freshness in the scanner's own unit (milliseconds — raw counts differ
  // by clock granularity). Missing or changed files cannot safely retain an old
  // verdict; the next scan will recreate a current pair if it still matches.
  auto fresh=[&](const FileState& x)->bool{
    std::error_code ec; const auto fp=path_from_utf8(x.path);
    const auto sz=std::filesystem::file_size(fp,ec); if(ec) return true;
    const auto mt=std::chrono::duration_cast<std::chrono::milliseconds>(
      std::filesystem::last_write_time(fp,ec).time_since_epoch()).count(); if(ec) return true;
    return (std::uint64_t)sz==x.size && (std::int64_t)mt==x.modified;
  };
  auto toMedia=[&](const FileState& x)->MediaFile{
    MediaFile mf{x.path,(MediaKind)x.kind,x.size,(std::uint64_t)x.modified,x.fingerprint,x.mirrorFingerprint,x.crop4x3,x.crop1x1,x.crop9x16,x.mirrorCrop4x3,x.mirrorCrop1x1,x.mirrorCrop9x16,x.duration};
    if((MediaKind)x.kind==MediaKind::Video) loadVideoAnchors(videoEngine_,mf);
    return mf;
  };
  // One shared cache-backed temporal engine for the whole pass: video pairs
  // whose fingerprints are cached skip re-decode entirely (same verdicts).
  std::vector<StoredMatch> survivors; survivors.reserve(stored.size());
  std::size_t n=0;
  for(const auto& m:stored){
    if(control && ((++n & 31)==0) && control->cancel.load()) return false;
    auto it1=byPath.find(m.left), it2=byPath.find(m.right);
    if(it1==byPath.end()||it2==byPath.end()){ if(dropped)++*dropped; continue; }
    const FileState &a=*it1->second, &b=*it2->second;
    if(!fresh(a)||!fresh(b)){ if(dropped)++*dropped; continue; }
    if(!a.fingerprint||!b.fingerprint){ if(dropped)++*dropped; continue; }
    // Exact pipeline verdict on the two files (L1 + anchors + temporal + SSIM
    // gates, same code as scans). Pair-bounded and one-time per engine bump.
    ScanPipeline pipe; pipe.setSharedTemporalEngine(&videoEngine_);
    pipe.add(toMedia(a)); pipe.add(toMedia(b));
    auto st=pipe.analyze(8);
    if(!st.matches.empty()){ survivors.push_back({m.left,m.right,st.matches.front().percent}); if(kept)++*kept; }
    else if(dropped) ++*dropped;
  }
  if(!db_.saveMatches(survivors)) return false;
  db_.setEngineVersion(kEngineVersion);
  return true;
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
  auto loadTemporal=[&](const std::string& path, VideoFingerprint& vf, VideoCropFingerprint& cf)->bool{
    return videoEngine_.buildFull(path, vf, cf, 96);
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
  for(const auto& c:candidates){if(c.index>=candidateStates_.size())continue;const auto&x=candidateStates_[c.index];if(x.path==excludePath||x.fingerprint==0||x.kind!=kind)continue;double pct=bestAgainst(x);if(pct>=threshold){
   // Same image second stage as the scan paths (monitor live matches share
   // the false-positive profile). Empty excludePath (adhoc queries) skips
   // verification and keeps the raw Hamming verdict.
   if(kind==(int)MediaKind::Image&&!excludePath.empty())
     pct=verifyImagePair(excludePath,x.path,true,pct,threshold);
   if(pct>=threshold)out.push_back({"",x.path,pct});}}
 std::sort(out.begin(),out.end(),[](const SearchMatch&a,const SearchMatch&b){return a.percent>b.percent;}); return out;
}
// L1 temporal anchors: per-frame hashes loaded read-only from the persistent
// video cache (never decoded here — cache misses simply yield no anchors and
// the video falls back to XOR-only candidacy). Strided to at most 16 so very
// long videos cannot flood the candidate index.
static constexpr std::size_t kMaxAnchors = 16;
static void loadVideoAnchors(const VideoFingerprintEngine& engine, MediaFile& mf){
  VideoFingerprint vf;
  if(!engine.loadPersistent(mf.path, mf.size, mf.modified, vf, nullptr) || vf.hashes.empty()) return;
  const std::size_t n = vf.hashes.size();
  const std::size_t stride = (n + kMaxAnchors - 1) / kMaxAnchors;
  mf.anchors.reserve(std::min(n, kMaxAnchors));
  for(std::size_t i = 0; i < n && mf.anchors.size() < kMaxAnchors; i += stride)
    if(vf.hashes[i]) mf.anchors.push_back(vf.hashes[i]);
}
void MediaSearchEngine::beginBenchmark(const BenchmarkConfig& cfg, bool withSampler) {
  bench_.start(cfg);
  if (withSampler) bench_.startSampler([this]() { return gpuActive_.load(std::memory_order_relaxed); });
}
void MediaSearchEngine::putColorThumb(const std::string& path, int w, int h, std::vector<unsigned char>&& bgra) const {
  if (w <= 0 || h <= 0 || bgra.size() != (std::size_t)w * h * 4) return;
  std::lock_guard<std::mutex> lock(thumbMutex_);
  auto it = thumbMap_.find(path);
  if (it != thumbMap_.end()) {
    it->second->second.w = w; it->second->second.h = h;
    it->second->second.bgra = std::move(bgra);
    thumbList_.splice(thumbList_.begin(), thumbList_, it->second);
    return;
  }
  while (thumbMap_.size() >= kColorThumbMax) {
    thumbMap_.erase(thumbList_.back().first);
    thumbList_.pop_back();
  }
  ColorThumb t; t.w = w; t.h = h; t.bgra = std::move(bgra);
  thumbList_.emplace_front(path, std::move(t));
  thumbMap_[path] = thumbList_.begin();
}
bool MediaSearchEngine::getColorThumb(const std::string& path, int& w, int& h, std::vector<unsigned char>& bgra) const {
  std::lock_guard<std::mutex> lock(thumbMutex_);
  auto it = thumbMap_.find(path);
  if (it == thumbMap_.end()) return false;
  thumbList_.splice(thumbList_.begin(), thumbList_, it->second);
  w = it->second->second.w; h = it->second->second.h; bgra = it->second->second.bgra;
  return w > 0 && h > 0 && bgra.size() == (std::size_t)w * h * 4;
}
bool MediaSearchEngine::getVideoThumb(const std::string& path, std::vector<unsigned char>& gray48) const {
  return videoEngine_.peekThumb48(path, gray48);
}
SearchReport MediaSearchEngine::scan(const std::string& root,unsigned maxDistance,ScanControl* control){ SearchReport r; files_.clear(); gpuImagesProcessed_.store(0); gpuActive_.store(false,std::memory_order_relaxed); const bool tx= db_.beginTransaction(); if(!tx) return r;
  auto old=db_.all();
  std::unordered_map<std::string,FileState> oldByPath; oldByPath.reserve(old.size()*2+1); for(const auto&x:old) oldByPath.emplace(x.path,x);
  const bool videoRegrid=(db_.samplingGeneration()!=kSamplingGeneration);
 const bool hasIgnored=control && !control->ignoredPaths.empty();
  const int workers=recommended_worker_count(policy_,static_cast<int>(std::thread::hardware_concurrency()));
  const std::size_t gpuBatch=std::max<std::size_t>(1,recommended_gpu_batch_size(policy_,256));
  const auto benchT0=std::chrono::steady_clock::now();
  auto benchMsSince=[&](){ return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-benchT0).count(); };
  MediaPipeline imagePipeline;
  BenchmarkConfig bcfg; bcfg.root=root; bcfg.build=(control&&!control->buildVersion.empty()?control->buildVersion:"?"); bcfg.engine=kEngineVersion; bcfg.db=Database::kDatabaseVersion; bcfg.distance=maxDistance; bcfg.cpuWorkers=workers; bcfg.gpuBatch=gpuBatch; bcfg.scanImages=!control||control->scanImages; bcfg.scanVideos=!control||control->scanVideos; bcfg.cudaAvailable=imagePipeline.gpuAvailable();
  const bool benchOn = !control || control->benchmarkEnabled;
  bcfg.detail = benchOn;
  bench_.start(bcfg);
  if(benchOn) bench_.startSampler([this](){ return gpuActive_.load(std::memory_order_relaxed); });
  if(control) bench_.addRevalidateMs(control->revalidateMs);
  auto finishScan=[&](bool completed)->SearchReport{
    bench_.finalize(completed, r.scanned, r.analyzed, r.unchanged, r.candidates, r.matches.size(), r.groups, r.candidateReductionPercent, gpuImagesProcessed_.load(std::memory_order_relaxed), r.gpuFallbackImages);
    return r;
  };
  bool benchWalkTimed=false;
 const std::string excl = managedIndexActive_ ? path_to_utf8(managedIndex_.directory.parent_path()) : std::string{};
 std::size_t done=0, scanned=0, nAdded=0, nModified=0, nUnchanged=0, nRemoved=0;
 std::unordered_set<std::string> seen; seen.reserve(old.size()*2+1024);
  std::unordered_map<std::string,FileState> currentByPath;
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
  const auto bt0=std::chrono::steady_clock::now();
  auto results=imagePipeline.imageBatch(batch,policy_.gpuEnabled,gpuBatch,&gpuActive_,benchOn?&bench_:nullptr);
  bench_.addImageStageMs(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-bt0).count());
  for(auto& ir:results){
   FileState x; auto it=currentByPath.find(ir.path);
   if(it==currentByPath.end()) continue;
   x=it->second; x.kind=(int)MediaKind::Image; x.mirrorFingerprint=ir.mirrorFingerprint; x.crop4x3=ir.crops.a4x3; x.crop1x1=ir.crops.a1x1; x.crop9x16=ir.crops.a9x16; x.mirrorCrop4x3=ir.crops.mirrorA4x3; x.mirrorCrop1x1=ir.crops.mirrorA1x1; x.mirrorCrop9x16=ir.crops.mirrorA9x16; if(ir.usedGpu) ++r.gpuImages; if(ir.gpuFallback) ++r.gpuFallbackImages; if(ir.ok){x.fingerprint=ir.fingerprint; if(ir.usedGpu) gpuImagesProcessed_.fetch_add(1,std::memory_order_relaxed); if(!db_.upsert(x)){ return false; } ++r.analyzed; MediaFile mf{x.path,MediaKind::Image,x.size,(std::uint64_t)x.modified,x.fingerprint,x.mirrorFingerprint,x.crop4x3,x.crop1x1,x.crop9x16,x.mirrorCrop4x3,x.mirrorCrop1x1,x.mirrorCrop9x16,0.0}; files_.push_back(mf); if(liveMatch) livePipe.addAndMatch(mf,maxDistance,liveEmit);}
   ++done; if(control&&control->progress)control->progress(done,scanned,x.path);
   if(ir.hasColorThumb) putColorThumb(ir.path, ir.colorThumb.width, ir.colorThumb.height, std::move(ir.colorThumb.bgra));
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
    futs.emplace_back(std::async(std::launch::async,[x,this,benchOn](){
     AnalysisJob j{x,false,true}; VideoFingerprint vf;
     const auto vt0=std::chrono::steady_clock::now();
      if(videoEngine_.build(x.path,vf)){ j.state.duration=vf.duration; const std::uint64_t h=foldVideoHashes(vf.hashes), mh=foldVideoHashes(vf.mirrorHashes); j.state.fingerprint=h; j.state.mirrorFingerprint=mh; j.state.crop4x3=vf.crop4x3; j.state.crop1x1=vf.crop1x1; j.state.crop9x16=vf.crop9x16; j.state.mirrorCrop4x3=vf.mirrorCrop4x3; j.state.mirrorCrop1x1=vf.mirrorCrop1x1; j.state.mirrorCrop9x16=vf.mirrorCrop9x16; j.ok=!vf.hashes.empty(); if(benchOn) bench_.addVideo(x.size, vf.duration, std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-vt0).count(), vf.hashes.size(), x.path); }
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
   if(control && control->walked) control->walked(scanned);
   auto it=oldByPath.find(x.path); const bool changed=(it==oldByPath.end()||it->second.size!=x.size||it->second.modified!=x.modified||it->second.quickHash!=x.quickHash||it->second.fingerprint==0||(isVid&&videoRegrid));
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
     const auto vt0=std::chrono::steady_clock::now();
     if(!processVideoRange(videoBase,videoBase+static_cast<std::size_t>(workers))) failed=true;
     else videoBase+=static_cast<std::size_t>(workers);
     bench_.addVideoStageMs(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-vt0).count());
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
  else if(walkDone.load() && queue.empty()){ if(!benchWalkTimed){ benchWalkTimed=true; bench_.addWalkMs(benchMsSince()); } walkCompleted=true; break; }
 }
 if(!failed && !cancelled && !imageBatch.empty()){ if(!processImageBatch(imageBatch)) failed=true; imageBatch.clear(); }
  while(!failed && !cancelled && videoBase<changedVideos.size()){
   const std::size_t n=std::min(static_cast<std::size_t>(workers),changedVideos.size()-videoBase);
   if(stopped(control)){ cancelled=true; break; }
   const auto vt0=std::chrono::steady_clock::now();
   if(!processVideoRange(videoBase,videoBase+n)) failed=true; else videoBase+=n;
   bench_.addVideoStageMs(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-vt0).count());
  }
 walker.join();
  if(failed){ if(tx) db_.rollbackTransaction(); r.scanned=scanned; r.added=nAdded; r.modified=nModified; r.unchanged=nUnchanged; r.completed=false; return finishScan(false); }
 // Deleted detection needs the complete seen set: only on fully walked scans.
 // Previously indexed files that no longer exist are removed then. Ignored rows
 // are retained in the database (they reappear only when unignored and rescanned).
 if(walkCompleted){
  for(auto& o:old){ if(seen.find(o.path)!=seen.end()) continue; if(hasIgnored && control->ignoredPaths.find(o.path)!=control->ignoredPaths.end()) continue; if(!db_.remove(o.path)){ failed=true; break; } ++nRemoved; }
 }
   if(failed){ if(tx) db_.rollbackTransaction(); r.scanned=scanned; r.added=nAdded; r.modified=nModified; r.unchanged=nUnchanged; r.completed=false; return finishScan(false); }
 // Persist everything done so far, including on cancel: partial progress is
 // kept by design (checkpoints), so interruption never loses the file list.
  if(!checkpoint()){ r.completed=false; return finishScan(false); }
 r.scanned=scanned; r.added=nAdded; r.modified=nModified; r.unchanged=nUnchanged;
 r.removed=nRemoved;
 if(cancelled||(control&&control->cancel.load())) r.completed=false;
 // Final commit also closes the trailing transaction checkpoint() reopened:
 // leaving it open would make the *next* scan's BEGIN fail and return empty.
  if(tx && !db_.commitTransaction()){ db_.rollbackTransaction(); r.completed=false; return finishScan(false); }
 candidateStates_=db_.all(); rebuildCandidateIndexes();
 // Unchanged files must participate in every incremental search.
  files_.clear();
  const auto currentStates=db_.all(); files_.reserve(currentStates.size());
  for(const auto& x:currentStates) if(x.fingerprint){
   if(hasIgnored && control->ignoredPaths.find(x.path)!=control->ignoredPaths.end()) continue;
   const bool video=kindOf(x.path)==MediaKind::Video;
   if(control && ((video&&!control->scanVideos)||(!video&&!control->scanImages))) continue;
   files_.push_back({x.path,(MediaKind)x.kind,x.size,(std::uint64_t)x.modified,x.fingerprint,x.mirrorFingerprint,x.crop4x3,x.crop1x1,x.crop9x16,x.mirrorCrop4x3,x.mirrorCrop1x1,x.mirrorCrop9x16,x.duration});
   if(video){ loadVideoAnchors(videoEngine_, files_.back()); ++r.indexedVideos; }
  }
  ScanPipeline pipe; pipe.setSharedTemporalEngine(&videoEngine_); for(auto&f:files_)pipe.add(f);
  // The final analyze pass can grind through millions of candidate pairs (plus
  // a video re-decode per video pair). Without a stop check, cancel/pause
  // during this phase did nothing until it finished — the force-quit path
  // that lost every streamed match. Poll pause-aware, like stopped().
  auto stopCheck=[&]()->bool{
    if(!control) return false;
    while(control->pause.load()&&!control->cancel.load()) std::this_thread::sleep_for(std::chrono::milliseconds(80));
    return control->cancel.load();
  };
  ScanStats st;
  const auto benchAT0=std::chrono::steady_clock::now();
   st=pipe.analyze(maxDistance,[&](const MediaMatch& m){
    if(benchOn) bench_.addStreamedMatch();
    SearchMatchRef ref{m.left,m.right,m.percent};
   if(control && control->onMatchRef) control->onMatchRef(ref);
   if(control && control->onMatch) {
     SearchMatch sm{files_[m.left].path,files_[m.right].path,m.percent};
     if(files_[m.left].kind==MediaKind::Video) ++r.videoMatches;
     control->onMatch(sm);
   }
    if(!control || control->retainMatches) {
      if(!control || control->maxRetainedMatches==0 || r.matches.size()<control->maxRetainedMatches) {
        SearchMatch sm{files_[m.left].path,files_[m.right].path,m.percent};
        r.matches.push_back(std::move(sm));
      }
    }
  }, stopCheck);
  bench_.addAnalyzeMs(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-benchAT0).count());
  // A stop during analyze() aborts the pair loops above (partial matches were
  // already streamed via onMatch); mark the report incomplete like every
  // other stop path. analyze() itself never propagates. Only completed scans
  // refresh the last-scan marker (a cancelled run must not claim freshness).
  if(cancelled||(control&&control->cancel.load())) r.completed=false;
  else if(managedIndexActive_) IndexManager::updateLastScan(managedIndex_);
  if(r.completed) db_.setSamplingGeneration(kSamplingGeneration);
  r.candidates=st.candidates;r.groups=st.groups;r.candidateReductionPercent=st.candidateReductionPercent; r.videoCandidatePairs=st.videoCandidates;r.videoTemporalChecks=st.videoTemporalChecks; return finishScan(r.completed);
}
}
