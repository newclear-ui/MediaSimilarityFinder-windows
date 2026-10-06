#include <cmath>
#include "media_search_engine.h"
#include "image_verify.h"
#include "semver.h"
#include "path_utils.h"
#include "incremental_scanner.h"
#include "media_pipeline.h"
#include "video_fingerprint.h"
#include "monitor.h"
#include "walker_queue.h"
#include "similarity.h"
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <mutex>
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
  // Disk freshness in the scanner's own unit (milliseconds ??raw counts differ
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
    ScanPipeline pipe; pipe.setSharedTemporalEngine(&videoEngine_); pipe.setVideoGpuBackend(&videoGpu_); pipe.setVideoGpuActivity(&gpuActive_);
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
  auto bestFullAgainst=[&](const FileState& x){
   double z=0; const std::uint64_t qFull[]={fingerprint,mirrorFingerprint}; const std::uint64_t tFull[]={x.fingerprint,x.mirrorFingerprint};
   for(auto a:qFull)if(hash_usable(a))for(auto b:tFull)if(hash_usable(b))z=std::max(z,hash_similarity(a,b));
   return z;
  };
  auto bestAgainst=[&](const FileState& x){
   double best=bestFullAgainst(x);
   const std::uint64_t qFull[]={fingerprint,mirrorFingerprint}; const std::uint64_t tFull[]={x.fingerprint,x.mirrorFingerprint};
   if(kind==(int)MediaKind::Image){
     const std::uint64_t qc[]={crop4x3,crop1x1,crop9x16,mirrorCrop4x3,mirrorCrop1x1,mirrorCrop9x16};
     const std::uint64_t tc[]={x.crop4x3,x.crop1x1,x.crop9x16,x.mirrorCrop4x3,x.mirrorCrop1x1,x.mirrorCrop9x16};
     for(auto a:qc)if(hash_usable(a))for(auto b:tFull)if(hash_usable(b))best=std::max(best,hash_similarity(a,b));
     for(auto a:qFull)if(hash_usable(a))for(auto b:tc)if(hash_usable(b))best=std::max(best,hash_similarity(a,b));
     const std::uint64_t qSame[][2]={{crop4x3,mirrorCrop4x3},{crop1x1,mirrorCrop1x1},{crop9x16,mirrorCrop9x16}};
     const std::uint64_t tSame[][2]={{x.crop4x3,x.mirrorCrop4x3},{x.crop1x1,x.mirrorCrop1x1},{x.crop9x16,x.mirrorCrop9x16}};
     for(int r=0;r<3;++r) for(auto a:qSame[r]) if(hash_usable(a)) for(auto b:tSame[r]) if(hash_usable(b)) best=std::max(best,hash_similarity(a,b));
   } else if(kind==(int)MediaKind::Video){
     const double full = best;
     double cropBest = full;
     const std::uint64_t qc[][2]={{crop4x3,mirrorCrop4x3},{crop1x1,mirrorCrop1x1},{crop9x16,mirrorCrop9x16}};
     const std::uint64_t tc[][2]={{x.crop4x3,x.mirrorCrop4x3},{x.crop1x1,x.mirrorCrop1x1},{x.crop9x16,x.mirrorCrop9x16}};
     for(int r=0;r<3;++r) for(auto a:qc[r]) if(hash_usable(a)) for(auto b:tc[r]) if(hash_usable(b)) cropBest=std::max(cropBest,hash_similarity(a,b));
     for(int r=0;r<3;++r) for(auto a:qc[r]) if(hash_usable(a)) for(auto b:tFull) if(hash_usable(b)) cropBest=std::max(cropBest,hash_similarity(a,b));
     for(int r=0;r<3;++r) for(auto a:qFull) if(hash_usable(a)) for(auto b:tc[r]) if(hash_usable(b)) cropBest=std::max(cropBest,hash_similarity(a,b));
     // Crop-only video hits earn temporal confirmation instead of matching
     // outright (same rule as the batch analyze path): full-frame hits match
     // immediately, crop-only pairs must survive DTW+SSIM verification.
     // Empty excludePath (adhoc queries) keeps the legacy raw verdict.
     if(full >= threshold || excludePath.empty()){ best = std::max(full,cropBest); }
     else if(cropBest >= std::max(0.0,threshold-12.0) && (!expensiveStageGuard_ || expensiveStageGuard_())){
      VideoFingerprint qa,ta; VideoCropFingerprint qc,tc;
      best = full;
       if(loadTemporal(excludePath,qa,qc) && loadTemporal(x.path,ta,tc)){ VideoSimilarityOptions options{threshold,8,2}; options.gpu=&videoGpu_; options.gpuActivity=&gpuActive_; best=std::max(best,video_crop_similarity(qa,qc,ta,tc,options)); }
     } else best = cropBest;
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
// video cache (never decoded here ??cache misses simply yield no anchors and
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
void MediaSearchEngine::beginTelemetry(const TelemetryConfig& cfg, bool withSampler) {
  telemetry_.start(cfg);
  if (withSampler) telemetry_.startSampler([this]() { return gpuActive_.load(std::memory_order_relaxed); });
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
  const auto telemetryT0=std::chrono::steady_clock::now();
  auto telemetryMsSince=[&](){ return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-telemetryT0).count(); };
  MediaPipeline imagePipeline;
  TelemetryConfig bcfg; bcfg.root=root; bcfg.build=(control&&!control->buildVersion.empty()?control->buildVersion:"?"); bcfg.engine=kEngineVersion; bcfg.db=Database::kDatabaseVersion; bcfg.distance=maxDistance; bcfg.cpuWorkers=workers; bcfg.gpuBatch=gpuBatch; bcfg.gpuBackend=imagePipeline.gpuBackendName(); bcfg.scanImages=!control||control->scanImages; bcfg.scanVideos=!control||control->scanVideos; bcfg.cudaAvailable=imagePipeline.gpuAvailable(); bcfg.purpose=control?control->telemetryPurpose:TelemetryPurpose::UserDiagnostic;
  // B1 Minimal Adaptive Allocation: baseline capacities only. The decision
  // gates backend use exactly where policy_.gpuEnabled gated before, so
  // verdict behavior is unchanged; the shares + decision are recorded.
  auto schedTickMs = []() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
  };
  SchedulerHardware schedHw;
  schedHw.cpuThreads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
  schedHw.gpuEnabled = policy_.gpuEnabled;
  schedHw.gpuAvailable = imagePipeline.gpuAvailable();
  schedHw.gpuComputeUnits = imagePipeline.gpuComputeUnits();
  schedHw.backendName = imagePipeline.gpuBackendName();
  // B6: the scheduler connects to Resource Mode policy (floors, hold,
  // kill band). Manual's CPU limit is enforced upstream by worker counts.
  schedHw.mode = policy_.mode;
  // B5: transfer volume per GPU unit is known from the packing layout;
  // bandwidth stays the documented coarse default until Node C measures it.
  schedHw.transferBytesPerUnit = (double)imagePipeline.gpuTransferBytesPerUnit();
  // C1: profile initial estimate. Machine-wide file beside the per-root
  // managed indexes; dormant until C2 writes the first profile (store
  // empty or unusable -> hardware baselines, behavior identical).
  // maxAge: never-stale in C1 (C3 owns the default age policy).
  // C2: Missing/Hard verdicts trigger one bounded calibration whose fresh
  // estimate applies to THIS scan (plus a re-decide); Exact/Soft reuse the
  // stored estimate without recalibrating (drift is C3's domain).
  bool profCalibrated = false;
  CalibrationResult profCalib;
  // C3: hoisted for finishScan (trigger needs path + identity + estimate).
  std::string profPath;
  ProfileIdentity profIdentity;
  {
    ProfileIdentity profCur;
    profCur.cpuModel = detectCpuModel();
    profCur.cpuThreads = schedHw.cpuThreads;
    profCur.gpuName = imagePipeline.gpuName();
    profCur.gpuBackend = imagePipeline.gpuBackendName();
    profCur.driver = imagePipeline.gpuDriverVersion();
    profCur.appVersion = bcfg.build;
    profCur.engineVersion = kEngineVersion;
    const long long nowSec = (long long)std::time(nullptr);
    ProfileStore profStore;
    profIdentity = profCur;
    if (managedIndexActive_)
      profPath = path_to_utf8(managedIndex_.directory.parent_path() / "PerformanceProfile.ini");
    ProfileMatch profVerdict = ProfileMatch::Missing;
    if (!profPath.empty() && profStore.load(profPath))
      profVerdict = profStore.classify(profCur, PerformanceProfile::kDefaultMaxAgeDays, nowSec);
    const bool profileMetricGap = profStore.hasProfile() && profileNeedsCalibration(profStore.profile(), policy_.gpuEnabled, schedHw.gpuAvailable);
    const bool needsCalibration = profVerdict == ProfileMatch::Missing || profVerdict == ProfileMatch::Hard || profVerdict == ProfileMatch::Stale || profileMetricGap;
    if (needsCalibration && !profPath.empty()) {
      // Bounded first-time calibration. Any throw or failure falls through
      // to hardware baselines: a profile must never fail a search.
      try {
        CalibrationConfig ccfg;
        ccfg.identity = profCur;
        ccfg.gpuAllowed = policy_.gpuEnabled;
        Calibrator calibrator;
        // videoGpu_ always wraps the hardware handle (policy-independent),
        // so the calibrator can tell no-device (not_available) apart from
        // user-off (not_measured) via gpuAllowed.
        profCalib = calibrator.run(ccfg, &videoGpu_);
        profCalibrated = true;
        ProfileStore writer;
        writer.setProfile(profCalib.profile);
        // GPT Fix / C4.1: incomplete retry never overwrites an existing profile.
        const bool candidateComplete = calibrationUsableForUpdate(profCalib) || (!profStore.hasProfile() && profCalib.attempted && profCalib.failedStage.empty());
        if(candidateComplete) writer.save(profPath);
        // Failed/incomplete candidates are never scheduler baselines. A
        // non-stale existing profile may still be reused; stale profiles
        // deliberately fall back to hardware until a usable candidate exists.
        InitialEstimate est;
        if(candidateComplete) {
          est = writer.initialEstimate(profCur, PerformanceProfile::kDefaultMaxAgeDays, nowSec);
        } else if(profStore.hasProfile() && profVerdict != ProfileMatch::Stale) {
          est = profStore.initialEstimate(profCur, PerformanceProfile::kDefaultMaxAgeDays, nowSec);
        }
        if (est.cpuKnown && est.gpuKnown) {
          schedHw.profileBaselineKnown = true;
          schedHw.profileBaselineCpu = est.cpu;
          schedHw.profileBaselineGpu = est.gpu;
        }
      } catch (...) {
        profCalibrated = false;
      }
    } else if (!profPath.empty()) {
      const InitialEstimate est = profStore.initialEstimate(profCur, PerformanceProfile::kDefaultMaxAgeDays, nowSec);
      if (est.cpuKnown && est.gpuKnown) {
        schedHw.profileBaselineKnown = true;
        schedHw.profileBaselineCpu = est.cpu;
        schedHw.profileBaselineGpu = est.gpu;
      }
    }
  }
  scheduler_.reset();
  schedCpuWin_.clear();
  schedGpuWin_.clear();
  // B3: live system load via the existing sampler (CPU delta needs one
  // priming call; GPU % is cached 3 s inside the sampler, so the 2 s
  // re-evaluation cadence costs no extra nvidia-smi spawn).
  SystemLoadMonitor schedLoadmon;
  schedLoadmon.sample();
  auto refreshSchedLoad = [&]() {
    const SystemLoad sl = schedLoadmon.sample();
    schedHw.cpuLoadKnown = true; schedHw.cpuLoad = sl.cpuPercent;
    schedHw.gpuLoadKnown = sl.gpuPercent >= 0;
    schedHw.gpuLoad = sl.gpuPercent >= 0 ? sl.gpuPercent : 0;
    schedHw.memKnown = true; schedHw.memPressure = sl.memoryPercent;
  };
  refreshSchedLoad();
  scheduler_.decide(schedHw);
  // B7 binding: no cached gpuUse flag. Every phase reads the CURRENT
  // published decision after re-evaluating, so execution follows the
  // scheduler (stability-guaranteed by B4 hold + B6 kill band). A cached
  // bool here would silently pin the scan-start verdict (review finding).
  auto schedUseGpuNow = [&]() { return scheduler_.lastDecision().gpuUsed; };
  const bool telemetryOn = !control || control->telemetryEnabled;
  bcfg.detail = telemetryOn;
  telemetry_.start(bcfg);
  // D8a: attach the identity of the bytes under this root so a later reader
  // can tell "same data" from "same path". A missing or unreadable root
  // records not_available/failed instead of a zero. This is telemetry only
  // and never influences scan behavior.
  // The fingerprint reads every file fully, so on a large dataset it is minutes
  // of disk I/O. It must honor the same stop request as the walk, or Stop is
  // dead until the whole dataset has been hashed. Telemetry-only: a cancelled
  // fingerprint never influences scan behavior.
  telemetry_.setDatasetFingerprint(
      msf::computeDatasetFingerprint(root, control ? &control->cancel : nullptr));
  if(telemetryOn) telemetry_.startSampler([this](){ return gpuActive_.load(std::memory_order_relaxed); });
  if(control) telemetry_.addRevalidateMs(control->revalidateMs);
  // C2: record the calibration run that fed this scan (if any). Skipped
  // runs leave the section not_measured, which is the honest record.
  if (profCalibrated) telemetry_.calibration() = profCalib.telemetry;
  // Cluster count consumed by finishScan below (computed after analyze).
  std::size_t clusterGroups = 0;
  auto finishScan=[&](bool completed)->SearchReport{
    // Node A: cancelled/partial benchmarks stay distinguishable from clean
    // completions; file progress separates started/completed/remaining.
    if(!completed){
      if(control && control->cancel.load(std::memory_order_relaxed)) telemetry_.setCancelled("cancelled");
      else telemetry_.setFailed("", "failed");
    }
    // C3: opportunistic recalibration. Only on completed scans fed by a
    // profile, only after K consecutive deviating scans, only with a
    // candidate that agrees with live observation. Failures and
    // inconsistencies keep the existing profile; nothing here can fail
    // the scan (bounded probes, swallowed errors).
    if (completed && schedHw.profileBaselineKnown && !profPath.empty()) {
      try {
        const double nowS = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const double liveCpu = schedCpuWin_.rate(nowS);
        const double liveGpu = schedGpuWin_.rate(nowS);
        ProfileStore rs;
        if (liveCpu > 0 && liveGpu > 0 && rs.load(profPath)) {
          const double baseCpu = schedHw.profileBaselineCpu;
          const double baseGpu = schedHw.profileBaselineGpu;
          if (recalTracker_.feed(liveCpu, liveGpu, baseCpu, baseGpu, rs.profile().id)) {
            CalibrationConfig ccfg;
            ccfg.identity = profIdentity;
            ccfg.gpuAllowed = policy_.gpuEnabled;
            Calibrator rc;
            const CalibrationResult cand = rc.run(ccfg, &videoGpu_);
            const bool candOk = calibrationUsableForUpdate(cand);
            if (candOk) {
              PerformanceProfile upd = rs.profile();
              const double oldConf = upd.confidence;
              const bool consistent = DeviationTracker::candidateConsistent(
                  cand.profile.cpuThroughput.value, cand.profile.gpuThroughput.value,
                  liveCpu, liveGpu, DeviationPolicy{}.consistencyTol);
              if (consistent) {
                upd.cpuThroughput = cand.profile.cpuThroughput;
                upd.gpuThroughput = cand.profile.gpuThroughput;
                if (cand.profile.gpuBatchThroughput.state == MeasureState::Measured)
                  upd.gpuBatchThroughput = cand.profile.gpuBatchThroughput;
                upd.confidence = std::min(DeviationPolicy{}.confidenceMax,
                                          oldConf + DeviationPolicy{}.confidenceStep);
                upd.lastUpdate = {"recalibration_consistent", upd.id, oldConf, upd.confidence, true};
              } else {
                upd.confidence = std::max(DeviationPolicy{}.confidenceMin,
                                          oldConf - DeviationPolicy{}.confidenceStep);
                upd.lastUpdate = {"recalibration_inconsistent", upd.id, oldConf, upd.confidence, true};
              }
              ProfileStore writer;
              writer.setProfile(std::move(upd));
              writer.save(profPath); // ignored: affects future runs only
            }
            // !candOk (failed calibration): existing profile untouched.
          }
        }
      } catch (...) {
        // Recalibration must never fail a scan.
      }
    }
    // "pending" must not include files that settled as analysis failures: they are
    // accounted for separately as a failed state, not as work still outstanding.
    const std::size_t pending = (r.scanned > r.analyzed + r.failed) ? r.scanned - r.analyzed - r.failed : 0;
    telemetry_.setFileProgress(r.scanned, r.analyzed, pending);
    // B1: record the scheduler decision (initial == current; live adjustment
    // arrives in B2+). Fallbacks accumulate image + video backend fallbacks.
    // B2: current capacities are the observed image/sec rates when both
    // backends reported recently, else the baselines.
    // C2: initial capacities are the baseline tier in force (profile pair
    // when a usable profile fed this scan, else hardware proxies).
    {
      const SchedulerDecision sd = scheduler_.lastDecision();
      SchedulerTelemetry& st = telemetry_.scheduler();
      st.markMeasured();
      CpuGpuScheduler::baseCapacities(schedHw, st.initialCpuCapacity, st.initialGpuCapacity);
      scheduler_.currentCapacities(st.currentCpuCapacity, st.currentGpuCapacity);
      st.cpuWorkShare = sd.cpuShare;
      st.gpuWorkShare = sd.gpuShare;
      st.adjustmentCount = scheduler_.adjustments();
      st.throttlingEvents = 0; // B4+: hysteresis/policy events
      st.externalLoadThrottling = scheduler_.throttles();
      st.selectedBackend = sd.backend;
      st.backendFallbacks = (std::uint64_t)r.gpuFallbackImages + telemetry_.videoGpuFallbacks();
    }
    // Per-kind user-facing summary. r.* per-kind fields were populated at each
    // return point (scanned/analyzed) and at the cluster block (pairs/groups/
    // dupFiles, zero when matching never completed). Telemetry mirrors them
    // so the GUI summary and the JSON agree by construction.
    telemetry_.setKindScanned(r.imgScanned, r.vidScanned);
    telemetry_.setMatchBreakdown(r.imgPairs, r.imgGroups, r.imgDupFiles,
                                 r.vidPairs, r.vidGroups, r.vidDupFiles);
    telemetry_.finalize(completed, r.scanned, r.analyzed, r.unchanged, r.candidates, r.matches.size(), clusterGroups, r.candidateReductionPercent, gpuImagesProcessed_.load(std::memory_order_relaxed), r.gpuFallbackImages);
    return r;
  };
  bool telemetryWalkTimed=false;
 const std::string excl = managedIndexActive_ ? path_to_utf8(managedIndex_.directory.parent_path()) : std::string{};
  std::size_t done=0, scanned=0, nAdded=0, nModified=0, nUnchanged=0, nRemoved=0, nFailed=0;
  std::size_t nImgScanned=0, nVidScanned=0, nImgAnalyzed=0, nVidAnalyzed=0;
  std::size_t nImgPairs=0, nVidPairs=0;
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
  // D3-Minimal: bounded walker queue (test override or production default).
  WalkerQueue queue(control && control->walkerQueueCapacity ? control->walkerQueueCapacity
                                                            : WalkerQueue::kDefaultCapacity);
  telemetry_.setWalkerCapacity(queue.capacity());
  std::atomic_bool walkDone{false};
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
   // B1 re-evaluation point (existing phase boundary; no topology change).
   // B2: refresh observed image-path rates first (unknown until 2+ batches).
   // B3: refresh live loads alongside (cheap: GPU % cached in the sampler).
   refreshSchedLoad();
   {
     const double nowS = std::chrono::duration<double>(bt0.time_since_epoch()).count();
     const double cpuR = schedCpuWin_.rate(nowS), gpuR = schedGpuWin_.rate(nowS);
     schedHw.cpuRateKnown = cpuR >= 0; schedHw.cpuRate = cpuR >= 0 ? cpuR : 0;
     schedHw.gpuRateKnown = gpuR >= 0; schedHw.gpuRate = gpuR >= 0 ? gpuR : 0;
   }
   scheduler_.maybeReevaluate(schedHw, (long long)schedTickMs());
   auto results=imagePipeline.imageBatch(batch,schedUseGpuNow(),gpuBatch,&gpuActive_,telemetryOn?&telemetry_:nullptr);
   telemetry_.addImageStageMs(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-bt0).count());
   // B2: attribute completed images to the backend that hashed them.
   // Coarse by design (batch wall includes shared CPU work); video-side
   // throughput belongs to Node C/E. Feeds the next re-evaluation.
    {
      const double nowS = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
      std::size_t cpuN = 0, gpuN = 0;
      for (const auto& ir : results) {
        if (!ir.ok) continue;
        if (ir.usedGpu) ++gpuN; else ++cpuN;
      }
      if (cpuN > 0) schedCpuWin_.add(nowS, (double)cpuN);
      if (gpuN > 0) schedGpuWin_.add(nowS, (double)gpuN);
    }
  for(auto& ir:results){
   FileState x; auto it=currentByPath.find(ir.path);
   if(it==currentByPath.end()) continue;
   x=it->second; x.kind=(int)MediaKind::Image; x.mirrorFingerprint=ir.mirrorFingerprint; x.crop4x3=ir.crops.a4x3; x.crop1x1=ir.crops.a1x1; x.crop9x16=ir.crops.a9x16; x.mirrorCrop4x3=ir.crops.mirrorA4x3; x.mirrorCrop1x1=ir.crops.mirrorA1x1; x.mirrorCrop9x16=ir.crops.mirrorA9x16; if(ir.usedGpu) ++r.gpuImages; if(ir.gpuFallback) ++r.gpuFallbackImages;
   if(ir.ok){
    x.fingerprint=ir.fingerprint; x.analysisFailed=false;
    if(ir.usedGpu) gpuImagesProcessed_.fetch_add(1,std::memory_order_relaxed);
    if(!db_.upsert(x)){ return false; }
    ++r.analyzed; ++nImgAnalyzed;
    // Outcome-time classification: the file now has a real fingerprint, so
    // reporting it as added/modified is finally backed by a usable index row.
    if(oldByPath.find(x.path)==oldByPath.end()) ++nAdded; else ++nModified;
    MediaFile mf{x.path,MediaKind::Image,x.size,(std::uint64_t)x.modified,x.fingerprint,x.mirrorFingerprint,x.crop4x3,x.crop1x1,x.crop9x16,x.mirrorCrop4x3,x.mirrorCrop1x1,x.mirrorCrop9x16,0.0}; files_.push_back(mf); if(liveMatch) livePipe.addAndMatch(mf,maxDistance,liveEmit);
   } else {
    // Analysis ran and produced nothing. Record that outcome on the row so the
    // next scan sees a settled state instead of re-queuing it forever, and count
    // it as `failed` rather than as a successful add/modify.
    x.fingerprint=0; x.analysisFailed=true;
    if(!db_.upsert(x)){ return false; }
    ++nFailed;
   }
   ++done; if(control&&control->progress)control->progress(done,scanned,x.path);
   if(ir.hasColorThumb) putColorThumb(ir.path, ir.colorThumb.width, ir.colorThumb.height, std::move(ir.colorThumb.bgra));
  }
  if(done-lastCommitDone>=500){ if(!checkpoint()) return false; }
  return true;
 };
 // Videos retain the bounded asynchronous CPU/FFmpeg analysis path. This keeps
 // GPU image batching independent from the video decoder architecture.
  auto processVideoRange=[&](std::size_t from,std::size_t to)->bool{
   // B1 re-evaluation point (existing phase boundary; no topology change).
   // B3: video carries no new throughput observation, but loads refresh.
   refreshSchedLoad();
   scheduler_.maybeReevaluate(schedHw, (long long)schedTickMs());
   // D1b: async range granularity (count + admitted files, no timing split).
   // D2: slowest file per range recorded after the join (pre-registered).
   std::vector<double> rangeFileMs(to > from ? to - from : 0, 0.0);
   // B7 binding: phase-fresh published decision (see image path note).
   const bool useGpu = schedUseGpuNow();
   std::vector<std::future<AnalysisJob>> futs;
  for(std::size_t k=from;k<to;++k){
   FileState x=changedVideos[k];
    futs.emplace_back(std::async(std::launch::async,[x,this,telemetryOn,useGpu,&rangeFileMs,slot = k - from](){
     AnalysisJob j{x,false,true}; VideoFingerprint vf;
     const auto vt0=std::chrono::steady_clock::now();
       VideoBuildStats videoStats;
       if(videoEngine_.build(x.path,vf,useGpu?&videoGpu_:nullptr,&gpuActive_,&videoStats)){ j.state.duration=vf.duration; const std::uint64_t h=foldVideoHashes(vf.hashes), mh=foldVideoHashes(vf.mirrorHashes); j.state.fingerprint=h; j.state.mirrorFingerprint=mh; j.state.crop4x3=vf.crop4x3; j.state.crop1x1=vf.crop1x1; j.state.crop9x16=vf.crop9x16; j.state.mirrorCrop4x3=vf.mirrorCrop4x3; j.state.mirrorCrop1x1=vf.mirrorCrop1x1; j.state.mirrorCrop9x16=vf.mirrorCrop9x16; j.ok=!vf.hashes.empty(); if(telemetryOn){ const std::size_t sampled = videoStats.cacheHit ? msf::TelemetryRecorder::kFramesNotProvided : videoStats.sampledFrames; const double bms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-vt0).count(); rangeFileMs[slot]=bms; telemetry_.addVideo(x.size, vf.duration, bms, vf.hashes.size(), x.path, videoStats.decodedFrames, sampled); telemetry_.addVideoGpu(videoStats.gpuUsed,videoStats.gpuFallback,videoStats.gpuMs); telemetry_.addVideoPlan(videoStats.planDecision, videoStats.planReason, videoStats.planSparseAccepted, videoStats.planSparseRejected, videoStats.planSparseSeeks, videoStats.planSparseDecoded, videoStats.planSparseLandingViolations); } }
     return j;
    }));
  }
   bool stopSeen=false;
   // D2: completion order (was index order). Same threads, same joins ??
   // only the harvest sequence changes, so a straggler stops blocking
   // finished siblings. 5 ms idle bound per range, negligible against
   // seconds-long builds. Cancel semantics preserved: post-stop
   // completions are still joined and discarded.
   std::vector<char> taken(futs.size(), 0);
   std::size_t remaining=futs.size();
   while(remaining>0){
    bool progressed=false;
    for(std::size_t i=0;i<futs.size();++i){
     if(taken[i]) continue;
     if(futs[i].wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) continue;
     taken[i]=1; --remaining; progressed=true;
      auto j=futs[i].get(); if(!stopSeen&&stopped(control)) stopSeen=true; if(stopSeen) continue;
      if(j.ok){ FileState vs=j.state; vs.analysisFailed=false; if(!db_.upsert(vs)){ return false; } ++r.analyzed; ++nVidAnalyzed; if(oldByPath.find(vs.path)==oldByPath.end()) ++nAdded; else ++nModified; MediaFile mf{vs.path,(MediaKind)vs.kind,vs.size,(std::uint64_t)vs.modified,vs.fingerprint,vs.mirrorFingerprint,vs.crop4x3,vs.crop1x1,vs.crop9x16,vs.mirrorCrop4x3,vs.mirrorCrop1x1,vs.mirrorCrop9x16,vs.duration};files_.push_back(mf); if(liveMatch) livePipe.addAndMatch(mf,maxDistance,liveEmit); }
      else { FileState vf=j.state; vf.fingerprint=0; vf.analysisFailed=true; if(!db_.upsert(vf)){ return false; } ++nFailed; }
      ++done; if(control&&control->progress)control->progress(done,scanned,j.state.path);}
    if(!progressed && remaining>0) std::this_thread::sleep_for(std::chrono::milliseconds(5));
   }
   // D1b/D2 range record (completed ranges only; failed ranges stay absent).
   {
    double rangeMax = 0;
    for (double v : rangeFileMs) rangeMax = std::max(rangeMax, v);
    telemetry_.recordVideoRange(to > from ? to - from : 0, rangeMax);
   }
   // Prompt stop: in-flight analyses must still join, but their results are
   // discarded and no new range starts, so the scan winds down instead of
   // grinding on. Checkpoints keep completed work; the worker maps cancel.
   if(stopSeen) return false;
  if(done-lastCommitDone>=500){ if(!checkpoint()) return false; }
  return true;
 };
   auto processOne=[&](FileState&& x){
   if(hasIgnored && control->ignoredPaths.find(x.path)!=control->ignoredPaths.end()){ seen.insert(x.path); return; }
   const bool isVid=(kindOf(x.path)==MediaKind::Video);
   if(control && ((isVid && !control->scanVideos) || (!isVid && !control->scanImages))){ seen.insert(x.path); return; }
   ++scanned;
   if(isVid) ++nVidScanned; else ++nImgScanned;
   if(control && control->walked) control->walked(scanned);
   auto it=oldByPath.find(x.path);
   // A skeleton row (fingerprint 0) used to force `changed` unconditionally, which
   // merged two different situations: analysis never completed (cancel/crash, must
   // retry) and analysis completed and failed (deterministic for this content, must
   // NOT be retried or it reports `modified` forever). The explicit
   // analysisFailed flag separates them, so the report converges.
   const bool pendingAnalysis=(it!=oldByPath.end() && it->second.fingerprint==0 && !it->second.analysisFailed);
   const bool changed=(it==oldByPath.end()||it->second.size!=x.size||it->second.modified!=x.modified||it->second.quickHash!=x.quickHash||pendingAnalysis||(isVid&&videoRegrid));
  if(!changed){
   // A known analysis failure on identical content is not "changed": there is
   // nothing to redo. It stays out of added/modified and out of the search set.
   ++nUnchanged; seen.insert(x.path); return;
  }
  // added/modified are counted at analysis OUTCOME, not at intent. Counting them
  // here reported a file as successfully indexed before any fingerprint existed.
  // Remove the previous record before re-analysis. If decoding/analysis fails,
  // the stale fingerprint must not silently survive this successful scan.
  if(it!=oldByPath.end() && !db_.remove(x.path)){ failed=true; return; }
  // Skeleton row first: the file list survives interruption (cancel/crash)
  // even before this file is analyzed. Unanalyzed rows carry fingerprint 0,
  // are invisible to matching, and are picked up by the pendingAnalysis rule
  // above. The analysisFailed flag is set only once a failure is observed.
  { FileState sk=x; sk.kind=(int)kindOf(x.path); sk.fingerprint=0; sk.mirrorFingerprint=0; sk.crop4x3=sk.crop1x1=sk.crop9x16=0; sk.mirrorCrop4x3=sk.mirrorCrop1x1=sk.mirrorCrop9x16=0; sk.duration=0; sk.analysisFailed=false;
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
     telemetry_.addVideoStageMs(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-vt0).count());
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
   cb.onFile=[&](FileState&& f){
    // D3-Minimal: bounded push with cancel-aware backpressure. A dropped
    // file (cancel/shutdown) was never analyzed, so the next scan sees it
    // as new ??identical to an unwalked file on cancel.
    bool waited=false;
    const auto pr=queue.push(std::move(f), control?&control->cancel:nullptr, &waited);
    if(waited) telemetry_.noteWalkerBlocked();
    if(pr==WalkerQueue::PushResult::Pushed) telemetry_.recordWalkerEnqueue(queue.size());
   };
   s.scan_stream(root, excl, cb);
   walkDone.store(true); queue.shutdown();
  });
  while(!failed && !cancelled){
   FileState x; bool have=false;
   {
    // D1b: a 50 ms timeout with an empty queue while the walker is alive is
    // a genuine consumer-idle poll (starved tick). Spurious wakeups and the
    // drained exit are not counted.
    const bool dataReady = queue.waitForData(50);
    if(queue.tryPop(x)){ telemetry_.recordWalkerDequeue(queue.size()); have=true; }
    else if(!dataReady && !walkDone.load()) telemetry_.noteWalkerStarved(); }
   if(have) processOne(std::move(x));
  if(stopped(control)) cancelled=true;
  else if(walkDone.load() && queue.empty()){ if(!telemetryWalkTimed){ telemetryWalkTimed=true; telemetry_.addWalkMs(telemetryMsSince()); } walkCompleted=true; break; }
 }
 if(!failed && !cancelled && !imageBatch.empty()){ if(!processImageBatch(imageBatch)) failed=true; imageBatch.clear(); }
  while(!failed && !cancelled && videoBase<changedVideos.size()){
   const std::size_t n=std::min(static_cast<std::size_t>(workers),changedVideos.size()-videoBase);
   if(stopped(control)){ cancelled=true; break; }
   const auto vt0=std::chrono::steady_clock::now();
   if(!processVideoRange(videoBase,videoBase+n)) failed=true; else videoBase+=n;
   telemetry_.addVideoStageMs(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-vt0).count());
  }
  // D3-Minimal: wake any producer blocked at this point on every exit
  // path (failed/cancelled/normal). Idempotent; join cannot hang on it.
  queue.shutdown();
  walker.join();
  if(failed){ if(tx) db_.rollbackTransaction(); r.scanned=scanned; r.added=nAdded; r.modified=nModified; r.unchanged=nUnchanged; r.failed=nFailed; r.imgScanned=nImgScanned; r.vidScanned=nVidScanned; r.imgAnalyzed=nImgAnalyzed; r.vidAnalyzed=nVidAnalyzed; r.imgPairs=nImgPairs; r.vidPairs=nVidPairs; r.completed=false; return finishScan(false); }
 // Deleted detection needs the complete seen set: only on fully walked scans.
 // Previously indexed files that no longer exist are removed then. Ignored rows
 // are retained in the database (they reappear only when unignored and rescanned).
 if(walkCompleted){
  for(auto& o:old){ if(seen.find(o.path)!=seen.end()) continue; if(hasIgnored && control->ignoredPaths.find(o.path)!=control->ignoredPaths.end()) continue; if(!db_.remove(o.path)){ failed=true; break; } ++nRemoved; }
 }
   if(failed){ if(tx) db_.rollbackTransaction(); r.scanned=scanned; r.added=nAdded; r.modified=nModified; r.unchanged=nUnchanged; r.failed=nFailed; r.imgScanned=nImgScanned; r.vidScanned=nVidScanned; r.imgAnalyzed=nImgAnalyzed; r.vidAnalyzed=nVidAnalyzed; r.imgPairs=nImgPairs; r.vidPairs=nVidPairs; r.completed=false; return finishScan(false); }
 // Persist everything done so far, including on cancel: partial progress is
 // kept by design (checkpoints), so interruption never loses the file list.
  if(!checkpoint()){ r.completed=false; return finishScan(false); }
 r.scanned=scanned; r.added=nAdded; r.modified=nModified; r.unchanged=nUnchanged; r.failed=nFailed; r.imgScanned=nImgScanned; r.vidScanned=nVidScanned; r.imgAnalyzed=nImgAnalyzed; r.vidAnalyzed=nVidAnalyzed; r.imgPairs=nImgPairs; r.vidPairs=nVidPairs;
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
  ScanPipeline pipe; pipe.setSharedTemporalEngine(&videoEngine_); pipe.setVideoGpuBackend(&videoGpu_); pipe.setVideoGpuActivity(&gpuActive_); for(auto&f:files_)pipe.add(f);
  // The final analyze pass can grind through millions of candidate pairs (plus
  // a video re-decode per video pair). Without a stop check, cancel/pause
  // during this phase did nothing until it finished ??the force-quit path
  // that lost every streamed match. Poll pause-aware, like stopped().
  auto stopCheck=[&]()->bool{
    if(!control) return false;
    while(control->pause.load()&&!control->cancel.load()) std::this_thread::sleep_for(std::chrono::milliseconds(80));
    return control->cancel.load();
  };
  ScanStats st;
  const auto benchAT0=std::chrono::steady_clock::now();
  // Cluster count for telemetry: union-find over every analyzed pair, the same
  // linkage the GUI rebuilds from streamed matches. ScanStats.groups counts
  // pairs (pinned by pipeline tests and the CLI journal), so telemetry's
  // matches.groups carries the cluster count shown beside "pairs" instead.
  std::vector<std::size_t> clusterParent(files_.size());
  for (std::size_t i = 0; i < clusterParent.size(); ++i) clusterParent[i] = i;
  std::vector<char> clusterTouched(files_.size(), 0);
  st=pipe.analyze(maxDistance,[&](const MediaMatch& m){
   if(telemetryOn) telemetry_.addStreamedMatch();
   if (m.left < clusterParent.size() && m.right < clusterParent.size()) {
     // Pairs are kind-homogeneous (image and video indexes are separate), so
     // the left side determines the pair's kind. If a mixed pair ever appears
     // it is counted for neither kind rather than misattributed.
     const bool lVid = files_[m.left].kind == MediaKind::Video;
     const bool rVid = files_[m.right].kind == MediaKind::Video;
     if (lVid == rVid) { if (lVid) ++nVidPairs; else ++nImgPairs; }
     std::size_t a = m.left;
     while (clusterParent[a] != a) { clusterParent[a] = clusterParent[clusterParent[a]]; a = clusterParent[a]; }
     std::size_t b = m.right;
     while (clusterParent[b] != b) { clusterParent[b] = clusterParent[clusterParent[b]]; b = clusterParent[b]; }
     if (a != b) clusterParent[a] = b;
     clusterTouched[m.left] = 1; clusterTouched[m.right] = 1;
   }
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
  telemetry_.addAnalyzeMs(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-benchAT0).count());
  // D9a: hand the analyze stage split to the recorder. Copy only -- the
  // pipeline fills a plain struct and no recorder pointer ever travels down
  // into ScanPipeline or the verification code.
  if(telemetryOn) telemetry_.setAnalyzeTelemetry(st.analyze);
  // A stop during analyze() aborts the pair loops above (partial matches were
  // already streamed via onMatch); mark the report incomplete like every
  // other stop path. analyze() itself never propagates. Only completed scans
  // refresh the last-scan marker (a cancelled run must not claim freshness).
  if(cancelled||(control&&control->cancel.load())) r.completed=false;
  else if(managedIndexActive_) IndexManager::updateLastScan(managedIndex_);
  if(r.completed) db_.setSamplingGeneration(kSamplingGeneration);
  // Distinct linkage clusters among this scan's pairs. The GUI rebuilds the
  // same union-find from streamed matches, so telemetry's matches.groups
  // agrees with what the user sees (st.groups counts pairs instead).
  // Per-kind split for the user-facing summary: a cluster never spans kinds
  // because image and video candidate indexes are separate, so counting roots
  // and members per kind partitions the totals exactly.
  {
    std::unordered_set<std::size_t> roots;
    std::unordered_set<std::size_t> imgRoots, vidRoots;
    std::size_t nImgDup = 0, nVidDup = 0;
    for (std::size_t i = 0; i < files_.size() && i < clusterParent.size(); ++i) {
      if (!clusterTouched[i]) continue;
      std::size_t rt = i;
      while (clusterParent[rt] != rt) rt = clusterParent[rt];
      roots.insert(rt);
      if (files_[i].kind == MediaKind::Video) {
        vidRoots.insert(rt);
        ++nVidDup;
      } else {
        imgRoots.insert(rt);
        ++nImgDup;
      }
    }
    clusterGroups = roots.size();
    r.imgGroups = imgRoots.size(); r.vidGroups = vidRoots.size();
    r.imgDupFiles = nImgDup; r.vidDupFiles = nVidDup;
  }
  r.candidates=st.candidates;r.groups=st.groups;r.candidateReductionPercent=st.candidateReductionPercent; r.videoCandidatePairs=st.videoCandidates;r.videoTemporalChecks=st.videoTemporalChecks;
  r.imgScanned=nImgScanned; r.vidScanned=nVidScanned;
  r.imgAnalyzed=nImgAnalyzed; r.vidAnalyzed=nVidAnalyzed;
  r.imgPairs=nImgPairs; r.vidPairs=nVidPairs;
  return finishScan(r.completed);
}
}
