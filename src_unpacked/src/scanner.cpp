#include "scanner.h"
#include "path_utils.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cctype>
#include <thread>
namespace fs=std::filesystem;
namespace msf {
static bool media(const fs::path&p){auto e=p.extension().string();for(auto&c:e)c=char(std::tolower((unsigned char)c));return e==".jpg"||e==".jpeg"||e==".png"||e==".bmp"||e==".webp"||e==".gif"||e==".tif"||e==".tiff"||e==".mp4"||e==".mkv"||e==".avi"||e==".mov"||e==".webm"||e==".m4v"||e==".wmv";}
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
 auto cancelled=[&]{ return cb.cancel && cb.cancel->load(); };
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
  FileState s;s.path=path_to_utf8(fs::absolute(it->path(),ec).lexically_normal());s.size=it->file_size(e);s.modified=stamp(it->path());s.quickHash=quick(it->path());++n;
  if(cb.onProgress && (n%2000)==0) cb.onProgress(n);
  if(cb.onFile) cb.onFile(std::move(s)); else o.push_back(std::move(s));
 }
 if(cb.onProgress) cb.onProgress(cb.onFile ? n : o.size());
 return o;
}
}
