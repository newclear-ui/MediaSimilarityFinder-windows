#include "video_fingerprint.h"
#include "video_sampling.h"
#include "fingerprint.h"
#include "similarity.h"
#include "path_utils.h"
#include <sqlite3.h>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <mutex>
namespace msf {
static sqlite3* VDB(void* p){return reinterpret_cast<sqlite3*>(p);}
bool VideoFingerprintEngine::preparePersistentStatements() const{
 sqlite3* db=VDB(cacheDb_); if(!db)return false;
 const char* loadSql="SELECT duration,payload FROM video_fingerprint_cache WHERE path=? AND size=? AND modified=? AND cache_version=?";
 const char* saveSql="INSERT INTO video_fingerprint_cache(path,size,modified,duration,step_count,cache_version,payload) VALUES(?,?,?,?,?,?,?) ON CONFLICT(path) DO UPDATE SET size=excluded.size,modified=excluded.modified,duration=excluded.duration,step_count=excluded.step_count,cache_version=excluded.cache_version,payload=excluded.payload";
 sqlite3_stmt* load=nullptr; sqlite3_stmt* save=nullptr;
 if(sqlite3_prepare_v2(db,loadSql,-1,&load,nullptr)!=SQLITE_OK)return false;
 if(sqlite3_prepare_v2(db,saveSql,-1,&save,nullptr)!=SQLITE_OK){sqlite3_finalize(load);return false;}
 loadStmt_=load; saveStmt_=save; return true;
}
void VideoFingerprintEngine::finalizePersistentStatements() const{
 if(loadStmt_){sqlite3_finalize(reinterpret_cast<sqlite3_stmt*>(loadStmt_));loadStmt_=nullptr;}
 if(saveStmt_){sqlite3_finalize(reinterpret_cast<sqlite3_stmt*>(saveStmt_));saveStmt_=nullptr;}
}
bool VideoFingerprintEngine::openPersistentCache(const std::string& path) const{
 std::lock_guard<std::mutex> lock(dbMutex_);
 finalizePersistentStatements();
 if(cacheDb_){sqlite3_close(VDB(cacheDb_));cacheDb_=nullptr;}
 sqlite3* db=nullptr;
 if(sqlite3_open(path.c_str(),&db)!=SQLITE_OK){if(db)sqlite3_close(db);return false;}
 cacheDb_=db;
 sqlite3_busy_timeout(db,5000);
 if(sqlite3_exec(db,"PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA temp_store=MEMORY;",nullptr,nullptr,nullptr)!=SQLITE_OK){sqlite3_close(db);cacheDb_=nullptr;return false;}
 const char* createSql="CREATE TABLE IF NOT EXISTS video_fingerprint_cache(path TEXT PRIMARY KEY,size INTEGER NOT NULL,modified INTEGER NOT NULL,duration REAL NOT NULL,step_count INTEGER NOT NULL,payload BLOB NOT NULL); CREATE INDEX IF NOT EXISTS idx_vfc_stamp ON video_fingerprint_cache(size,modified);";
 if(sqlite3_exec(db,createSql,nullptr,nullptr,nullptr)!=SQLITE_OK){sqlite3_close(db);cacheDb_=nullptr;return false;}
 // 0.9.2.26 cache entries have no algorithm/version discriminator. Add it once;
 // legacy rows are deliberately invalidated below rather than trusted silently.
 bool hasVersion=false;
 sqlite3_stmt* info=nullptr;
 if(sqlite3_prepare_v2(db,"PRAGMA table_info(video_fingerprint_cache)",-1,&info,nullptr)==SQLITE_OK){
   while(sqlite3_step(info)==SQLITE_ROW){const unsigned char* n=sqlite3_column_text(info,1);if(n&&std::strcmp(reinterpret_cast<const char*>(n),"cache_version")==0){hasVersion=true;break;}}
   sqlite3_finalize(info);
 }
 if(!hasVersion){
   if(sqlite3_exec(db,"ALTER TABLE video_fingerprint_cache ADD COLUMN cache_version INTEGER NOT NULL DEFAULT 0;",nullptr,nullptr,nullptr)!=SQLITE_OK){sqlite3_close(db);cacheDb_=nullptr;return false;}
 }
 if(sqlite3_exec(db,"DELETE FROM video_fingerprint_cache WHERE cache_version != 3;",nullptr,nullptr,nullptr)!=SQLITE_OK){sqlite3_close(db);cacheDb_=nullptr;return false;}
 return preparePersistentStatements();
}
void VideoFingerprintEngine::closePersistentCache() const{
 std::lock_guard<std::mutex> lock(dbMutex_); finalizePersistentStatements(); if(cacheDb_){sqlite3_close(VDB(cacheDb_));cacheDb_=nullptr;}
}
bool VideoFingerprintEngine::loadPersistent(const std::string&p,std::uint64_t sz,std::uint64_t mt,VideoFingerprint&o) const{
 std::lock_guard<std::mutex> lock(dbMutex_); if(!cacheDb_)return false;
 sqlite3_stmt* s=reinterpret_cast<sqlite3_stmt*>(loadStmt_); if(!s)return false;
 sqlite3_reset(s); sqlite3_clear_bindings(s);
 sqlite3_bind_text(s,1,p.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int64(s,2,(sqlite3_int64)sz);sqlite3_bind_int64(s,3,(sqlite3_int64)mt);sqlite3_bind_int(s,4,kCacheFormatVersion);
 bool ok=false;
 if(sqlite3_step(s)==SQLITE_ROW){
  o.duration=sqlite3_column_double(s,0);const auto*n=(const unsigned char*)sqlite3_column_blob(s,1);int bytes=sqlite3_column_bytes(s,1);
  if(n&&bytes>=sizeof(std::uint32_t)){const char*cur=(const char*)n;std::uint32_t count=0;std::memcpy(&count,cur,sizeof(count));cur+=sizeof(count);std::size_t frameBytes=sizeof(count)+count*(sizeof(double)+2*sizeof(std::uint64_t));std::size_t need=frameBytes+6*sizeof(std::uint64_t);
   if(need==(std::size_t)bytes){o.timestamps.resize(count);o.hashes.resize(count);o.mirrorHashes.resize(count);for(std::uint32_t i=0;i<count;++i){std::memcpy(&o.timestamps[i],cur,sizeof(double));cur+=sizeof(double);std::memcpy(&o.hashes[i],cur,sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);std::memcpy(&o.mirrorHashes[i],cur,sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);}std::memcpy(&o.crop4x3,cur,sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);std::memcpy(&o.crop1x1,cur,sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);std::memcpy(&o.crop9x16,cur,sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);std::memcpy(&o.mirrorCrop4x3,cur,sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);std::memcpy(&o.mirrorCrop1x1,cur,sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);std::memcpy(&o.mirrorCrop9x16,cur,sizeof(std::uint64_t));ok=!o.hashes.empty();}
  }
 }
 sqlite3_reset(s);sqlite3_clear_bindings(s);return ok;
}
void VideoFingerprintEngine::savePersistent(const std::string&p,std::uint64_t sz,std::uint64_t mt,const VideoFingerprint&f) const{
 std::lock_guard<std::mutex> lock(dbMutex_);if(!cacheDb_||f.hashes.empty()||f.timestamps.size()!=f.hashes.size())return;
 const std::uint32_t count=(std::uint32_t)f.hashes.size();std::vector<unsigned char> blob(sizeof(count)+count*(sizeof(double)+2*sizeof(std::uint64_t))+6*sizeof(std::uint64_t));char*cur=(char*)blob.data();std::memcpy(cur,&count,sizeof(count));cur+=sizeof(count);for(std::uint32_t i=0;i<count;++i){std::memcpy(cur,&f.timestamps[i],sizeof(double));cur+=sizeof(double);std::memcpy(cur,&f.hashes[i],sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);const auto mh=(i<f.mirrorHashes.size()?f.mirrorHashes[i]:0);std::memcpy(cur,&mh,sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);}const std::uint64_t crops[]={f.crop4x3,f.crop1x1,f.crop9x16,f.mirrorCrop4x3,f.mirrorCrop1x1,f.mirrorCrop9x16};for(auto h:crops){std::memcpy(cur,&h,sizeof(h));cur+=sizeof(h);}
 sqlite3_stmt*s=reinterpret_cast<sqlite3_stmt*>(saveStmt_);if(!s)return;sqlite3_reset(s);sqlite3_clear_bindings(s);sqlite3_bind_text(s,1,p.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int64(s,2,(sqlite3_int64)sz);sqlite3_bind_int64(s,3,(sqlite3_int64)mt);sqlite3_bind_double(s,4,f.duration);sqlite3_bind_int(s,5,(int)count);sqlite3_bind_int(s,6,kCacheFormatVersion);sqlite3_bind_blob(s,7,blob.data(),(int)blob.size(),SQLITE_TRANSIENT);sqlite3_step(s);sqlite3_reset(s);sqlite3_clear_bindings(s);
}
VideoFingerprintEngine::~VideoFingerprintEngine(){closePersistentCache();}
bool VideoFingerprintEngine::build(const std::string&p,VideoFingerprint&o)const{
 // NOTE: p is UTF-8. Build the path with path_from_utf8 first: constructing
 // fs::path from a narrow string throws on Windows when the name holds
 // characters outside the ANSI code page (observed terminate() on Korean
 // filenames), even when an error_code is supplied.
 std::error_code ec; const fs::path fp=path_from_utf8(p);
 if(!std::filesystem::exists(fp,ec))return false; const auto sz=std::filesystem::file_size(fp,ec);if(ec)return false;const auto mt=(std::uint64_t)std::filesystem::last_write_time(fp,ec).time_since_epoch().count();if(ec)return false;
 {std::lock_guard<std::mutex>lock(cacheMutex_);auto it=cache_.find(p);if(it!=cache_.end()&&it->second.size==sz&&it->second.modified==mt){o=it->second.fingerprint;return !o.hashes.empty();}}
 if(loadPersistent(p,sz,mt,o)){std::lock_guard<std::mutex>lock(cacheMutex_);cache_[p]={sz,mt,o};return true;}
 VideoDecoder d;if(!d.open(p))return false;VideoInfo i;if(!d.info(i)){d.close();return false;}
 VideoFingerprint built; built.duration=i.duration; auto plan=make_sample_plan(i.duration);
 std::vector<VideoFrame> frames32, frames96;
 if(!d.framesAt(plan.timestamps,32,32,frames32)){d.close();return false;}
 // The crop pass uses the same timestamp list but a larger decode. This remains a
 // second resolution pass; each pass itself is a single sequential decode rather than
 // one seek per timestamp.
 d.framesAt(plan.timestamps,96,96,frames96);
 built.timestamps.reserve(frames32.size()); built.hashes.reserve(frames32.size()); built.mirrorHashes.reserve(frames32.size());
 for(std::size_t iFrame=0;iFrame<frames32.size();++iFrame){
   const auto& f=frames32[iFrame]; built.timestamps.push_back(f.timestamp); built.hashes.push_back(perceptual_hash(f.gray,f.width,f.height)); built.mirrorHashes.push_back(perceptual_hash_mirrored(f.gray,f.width,f.height));
 }
 for(const auto& cf:frames96){GrayImage g;g.width=cf.width;g.height=cf.height;g.pixels=cf.gray;auto c=cropFingerprints(g);built.crop4x3^=c.a4x3;built.crop1x1^=c.a1x1;built.crop9x16^=c.a9x16;built.mirrorCrop4x3^=c.mirrorA4x3;built.mirrorCrop1x1^=c.mirrorA1x1;built.mirrorCrop9x16^=c.mirrorA9x16;}
 d.close();if(built.hashes.empty())return false;
 {std::lock_guard<std::mutex>lock(cacheMutex_);cache_[p]={sz,mt,built};}savePersistent(p,sz,mt,built);o=std::move(built);return true;
}
bool VideoFingerprintEngine::buildCropAware(const std::string&p, const VideoFingerprint& base, VideoCropFingerprint& out, int decodeSize) const{
 if(base.hashes.empty() || base.timestamps.size()!=base.hashes.size()) return false;
 std::error_code ec; if(!std::filesystem::exists(path_from_utf8(p),ec)) return false;
 VideoDecoder d; if(!d.open(p)) return false;
 out={}; const int size=std::max(32,std::min(192,decodeSize)); std::vector<VideoFrame> frames;
 if(!d.framesAt(base.timestamps,size,size,frames)){d.close();return false;}
 out.timestamps.reserve(frames.size()); out.a4x3.reserve(frames.size()); out.a1x1.reserve(frames.size()); out.a9x16.reserve(frames.size());
 out.mirrorA4x3.reserve(frames.size()); out.mirrorA1x1.reserve(frames.size()); out.mirrorA9x16.reserve(frames.size());
 for(const auto& f:frames){GrayImage g;g.width=f.width;g.height=f.height;g.pixels=f.gray;const auto c=cropFingerprints(g);out.timestamps.push_back(f.timestamp);out.a4x3.push_back(c.a4x3);out.a1x1.push_back(c.a1x1);out.a9x16.push_back(c.a9x16);out.mirrorA4x3.push_back(c.mirrorA4x3);out.mirrorA1x1.push_back(c.mirrorA1x1);out.mirrorA9x16.push_back(c.mirrorA9x16);}
 d.close(); return !out.timestamps.empty();
}

void VideoFingerprintEngine::clearCache()const{std::lock_guard<std::mutex>lock(cacheMutex_);cache_.clear();}
std::size_t VideoFingerprintEngine::memoryCacheSize()const{std::lock_guard<std::mutex>lock(cacheMutex_);return cache_.size();}
double video_similarity(const VideoFingerprint&a,const VideoFingerprint&b,const VideoSimilarityOptions& options){
 if(a.hashes.empty()||b.hashes.empty())return 0;const double threshold=std::clamp(options.thresholdPercent,0.0,100.0),gap=std::max(0.0,options.gapPenalty),tol=std::max(0.0,options.timeToleranceSeconds);
 const auto&x=(a.hashes.size()<=b.hashes.size()?a:b);const auto&y=(a.hashes.size()<=b.hashes.size()?b:a);const std::size_t n=x.hashes.size(),m=y.hashes.size();std::vector<double>prev(m+1),cur(m+1);double best=0;
 for(std::size_t i=1;i<=n;++i){cur[0]=0;for(std::size_t j=1;j<=m;++j){double sim=hash_similarity(x.hashes[i-1],y.hashes[j-1]);
 std::uint64_t xm=(i-1<x.mirrorHashes.size()?x.mirrorHashes[i-1]:0), ym=(j-1<y.mirrorHashes.size()?y.mirrorHashes[j-1]:0);
 if(xm) sim=std::max(sim,hash_similarity(xm,y.hashes[j-1]));
 if(ym) sim=std::max(sim,hash_similarity(x.hashes[i-1],ym));
 if(xm&&ym) sim=std::max(sim,hash_similarity(xm,ym));double timePenalty=0;if(i-1<x.timestamps.size()&&j-1<y.timestamps.size()&&tol>0){if(i>1&&j>1){double dx=x.timestamps[i-1]-x.timestamps[i-2];double dy=y.timestamps[j-1]-y.timestamps[j-2];double dt=std::abs(dx-dy);timePenalty=std::min(20.0,20.0*dt/tol);}}double match=(sim>=threshold?sim:sim-100.0)-timePenalty;cur[j]=std::max({0.0,prev[j-1]+match,prev[j]-gap,cur[j-1]-gap});best=std::max(best,cur[j]);}std::swap(prev,cur);}
 return std::clamp(100.0*best/(100.0*n),0.0,100.0);
}

static double crop_temporal_score(const VideoFingerprint& baseA, const VideoCropFingerprint& cropA,
                                  const VideoFingerprint& baseB, const VideoCropFingerprint& cropB,
                                  const VideoSimilarityOptions& options){
 const double threshold=std::clamp(options.thresholdPercent,0.0,100.0), gap=std::max(0.0,options.gapPenalty), tol=std::max(0.0,options.timeToleranceSeconds);
 const std::size_t n=std::min(cropA.timestamps.size(),baseA.hashes.size());
 const std::size_t m=std::min(cropB.timestamps.size(),baseB.hashes.size());
 if(!n||!m) return 0;
 const std::uint64_t* ax[6]={cropA.a4x3.data(),cropA.a1x1.data(),cropA.a9x16.data(),cropA.mirrorA4x3.data(),cropA.mirrorA1x1.data(),cropA.mirrorA9x16.data()};
 const std::uint64_t* bx[6]={cropB.a4x3.data(),cropB.a1x1.data(),cropB.a9x16.data(),cropB.mirrorA4x3.data(),cropB.mirrorA1x1.data(),cropB.mirrorA9x16.data()};
 const int ratioOf[6]={0,1,2,0,1,2}; double globalBest=0;
 // Each crop ratio is evaluated independently. This deliberately prevents
 // arbitrary 4:3-vs-1:1 crop-to-crop matching while still allowing crop/full.
 for(int r=0;r<3;++r){
   std::vector<double> prev(m+1),cur(m+1); double best=0;
   for(std::size_t i=1;i<=n;++i){cur[0]=0;
     for(std::size_t j=1;j<=m;++j){double sim=0;
       const int ai0=r, ai1=r+3, bj0=r, bj1=r+3;
       for(int u: {ai0,ai1}) for(int v: {bj0,bj1}) if(ax[u]&&bx[v]) sim=std::max(sim,hash_similarity(ax[u][i-1],bx[v][j-1]));
       // Crop-to-full is part of the second-stage check and is orientation aware.
       for(int u: {ai0,ai1}){if(ax[u]){sim=std::max(sim,hash_similarity(ax[u][i-1],baseB.hashes[j-1])); if(j-1<baseB.mirrorHashes.size())sim=std::max(sim,hash_similarity(ax[u][i-1],baseB.mirrorHashes[j-1]));}}
       for(int v: {bj0,bj1}){if(bx[v]){sim=std::max(sim,hash_similarity(baseA.hashes[i-1],bx[v][j-1])); if(i-1<baseA.mirrorHashes.size())sim=std::max(sim,hash_similarity(baseA.mirrorHashes[i-1],bx[v][j-1]));}}
       double timePenalty=0;if(tol>0&&i>1&&j>1){double dx=cropA.timestamps[i-1]-cropA.timestamps[i-2],dy=cropB.timestamps[j-1]-cropB.timestamps[j-2];timePenalty=std::min(20.0,20.0*std::abs(dx-dy)/tol);}double match=(sim>=threshold?sim:sim-100.0)-timePenalty;cur[j]=std::max({0.0,prev[j-1]+match,prev[j]-gap,cur[j-1]-gap});best=std::max(best,cur[j]);
     } std::swap(prev,cur);
   }
   globalBest=std::max(globalBest,100.0*best/(100.0*n));
 }
 return std::clamp(globalBest,0.0,100.0);
}

double video_crop_similarity(const VideoFingerprint&a,const VideoCropFingerprint&ca,const VideoFingerprint&b,const VideoCropFingerprint&cb,const VideoSimilarityOptions&options){
 return std::max(video_similarity(a,b,options),crop_temporal_score(a,ca,b,cb,options));
}
}
