#include "scanner.h"
#include "path_utils.h"
#include "scan_pipeline.h"  // MediaKind, so the walker can report the kind it already decided
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <cctype>
#include <thread>
namespace fs=std::filesystem;
namespace msf {
static bool media(const fs::path&p){return Scanner::isMediaPath(p);}
bool Scanner::isVideoPath(const fs::path& p) {
  auto e=p.extension().string();
  for(auto&c:e)c=char(std::tolower((unsigned char)c));
  return e==".mp4"||e==".mkv"||e==".avi"||e==".mov"||e==".webm"||e==".m4v"||e==".wmv";
}
bool Scanner::isMediaPath(const fs::path& p) {
  if (isVideoPath(p)) return true;
  auto e=p.extension().string();
  for(auto&c:e)c=char(std::tolower((unsigned char)c));
  return e==".jpg"||e==".jpeg"||e==".png"||e==".bmp"||e==".webp"||e==".gif"||e==".tif"||e==".tiff";
}
std::size_t Scanner::count(const std::string& root, const std::string& excludedDirectory,
                           bool scanImages, bool scanVideos,
                           const std::unordered_set<std::string>& ignoredPaths,
                           const std::atomic_bool* cancel,
                           const std::function<void(std::size_t)>& onProgress) const {
  std::error_code ec;
  const fs::path excluded = excludedDirectory.empty() ? fs::path{} : fs::weakly_canonical(path_from_utf8(excludedDirectory), ec);
  ec.clear();
  fs::recursive_directory_iterator it(path_from_utf8(root), fs::directory_options::skip_permission_denied, ec), end;
  ec.clear();
  std::size_t total = 0;
  std::size_t visited = 0;
  for (; it != end; it.increment(ec)) {
    if (cancel && cancel->load()) break;
    if (ec) { ec.clear(); continue; }
    ++visited;
    if (onProgress && (visited % 2000) == 0) onProgress(visited);
    std::error_code e;
    if (!excluded.empty() && it->is_directory(e)) {
      const auto p = fs::weakly_canonical(it->path(), e);
      if (!e && p == excluded) { it.disable_recursion_pending(); continue; }
    }
    e.clear();
    if (!it->is_regular_file(e) || !isMediaPath(it->path())) continue;
    const bool video = isVideoPath(it->path());
    if ((video && !scanVideos) || (!video && !scanImages)) continue;
    const auto canonical = fs::absolute(it->path(), e).lexically_normal();
    if (e || ignoredPaths.find(path_to_utf8(canonical)) != ignoredPaths.end()) continue;
    ++total;
  }
  if (onProgress) onProgress(visited);
  return total;
}
static std::int64_t stamp(const fs::path&p){std::error_code ec;auto t=fs::last_write_time(p,ec);if(ec)return 0;return std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()).count();}
static std::string quick(const fs::path&p){std::ifstream f(p,std::ios::binary);if(!f)return{};std::vector<unsigned char>b(65536);f.read((char*)b.data(),b.size());auto n=(size_t)f.gcount();std::uint64_t h=1469598103934665603ULL;for(size_t i=0;i<n;i++){h^=b[i];h*=1099511628211ULL;}return std::to_string(h);}
std::vector<FileState> Scanner::scan(const std::string& root, const std::string& excludedDirectory, const std::function<void(std::size_t)>& onProgress) const{
 ScanCallbacks cb; cb.onProgress = onProgress;
 return scan_stream(root, excludedDirectory, cb);
}
std::vector<FileState> Scanner::scan_stream(const std::string& root, const std::string& excludedDirectory, const ScanCallbacks& cb) const{
 std::vector<FileState>o; std::error_code ec;
 const fs::path excluded=excludedDirectory.empty()?fs::path{}:fs::weakly_canonical(path_from_utf8(excludedDirectory),ec); ec.clear();
 fs::recursive_directory_iterator it(path_from_utf8(root),fs::directory_options::skip_permission_denied,ec),end;
 std::size_t n=0;
  auto cancelled=[&]{ return (cb.cancel && cb.cancel->load()) || (cb.shouldStop && cb.shouldStop()); };
 // The per-file 64KB content hash (quick) is the walk's dominant cost and is
 // latency-bound on the file open, not CPU. Keep enumeration and metadata on
 // this thread, but fan the reads out over a bounded pool so a large or
 // latency-heavy tree no longer serializes the whole walk behind one core.
 // Batches flush in enumeration order, so emission order (and therefore
 // results, progress and determinism) is unchanged.
 unsigned walkHw=std::thread::hardware_concurrency();
 if(walkHw<1) walkHw=1; if(walkHw>8) walkHw=8;
 constexpr std::size_t kHashBatch=256;
 std::vector<FileState> batch; batch.reserve(kHashBatch);
 std::vector<fs::path> bpaths; bpaths.reserve(kHashBatch);
 auto flushBatch=[&](){
  if(batch.empty()) return;
  // Compute the 64 KB content hash for the batch. Prefer the bounded pool, but
  // never let a scheduling failure (std::async throwing under resource
  // pressure) escape: on any failure fall back to a serial pass so the walk
  // always makes progress and no exception can terminate the walker thread.
  bool parallelDone=false;
  try{
   const unsigned jobs=std::min<unsigned>(walkHw,(unsigned)batch.size());
   const std::size_t chunk=(batch.size()+jobs-1)/jobs;
   std::vector<std::future<void>> futs; futs.reserve(jobs);
   for(unsigned j=0;j<jobs;++j){
    const std::size_t b=j*chunk, e=std::min(batch.size(),b+chunk);
    if(b>=e) break;
    futs.emplace_back(std::async(std::launch::async,[&,b,e]{
     // A worker must never terminate the walk: a failed read becomes an empty
     // quick hash for that slot, exactly as the serial quick() did.
     try{ for(std::size_t i=b;i<e;++i) batch[i].quickHash=quick(bpaths[i]); }catch(...){}
    }));
   }
   for(auto& f:futs) f.get();
   parallelDone=true;
  }catch(...){ parallelDone=false; }
  if(!parallelDone){
   for(std::size_t i=0;i<batch.size();++i){ try{ batch[i].quickHash=quick(bpaths[i]); }catch(...){} }
  }
   for(auto& s:batch){
    ++n;
    if(cb.onFile) cb.onFile(std::move(s)); else o.push_back(std::move(s));
   }
   // Report each completed hash batch. The caller throttles UI delivery; this
   // keeps producer-side read progress visible even while analysis is busy.
   if(cb.onProgress) cb.onProgress(n);
   batch.clear(); bpaths.clear();
 };
 for(;it!=end;it.increment(ec)){
  if(cancelled()) break;
  while(cb.pause && cb.pause->load() && !cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(80));
  if(cancelled()) break;
  if(ec){ec.clear();continue;}
  std::error_code e;
  if(!excluded.empty() && it->is_directory(e)){
   const auto p=fs::weakly_canonical(it->path(),e);
   if(!e && p==excluded){it.disable_recursion_pending();continue;}
  }
  if(!it->is_regular_file(e)||!media(it->path()))continue;
  FileState s;s.path=path_to_utf8(fs::absolute(it->path(),ec).lexically_normal());s.size=it->file_size(e);s.modified=stamp(it->path());
  // Media kind was left at Unknown here, which made any consumer of the returned
  // FileState unable to tell an image from a video. The classifier is the one the
  // rest of the product already uses (media_search_engine's kindOf() is built on
  // Scanner::isVideoPath), so no second rule is introduced: isMediaPath() already
  // passed above, so a file reaching this line is an image or a video and never
  // Unknown. The production scan path is unaffected either way, because
  // MediaSearchEngine::processOne() recomputes the kind from the path itself.
  s.kind=(int)(isVideoPath(it->path())?MediaKind::Video:MediaKind::Image);
  bpaths.push_back(it->path());
  batch.push_back(std::move(s));
  if(batch.size()>=kHashBatch) flushBatch();
 }
 flushBatch();
 if(cb.onProgress) cb.onProgress(cb.onFile ? n : o.size());
 return o;
}
}
