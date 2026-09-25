#include "video_fingerprint.h"
#include "video_sampling.h"
#include "fingerprint.h"
#include "similarity.h"
#include "path_utils.h"
#include <sqlite3.h>
#include <cstring>
#include <string>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
namespace msf {
static sqlite3* VDB(void* p){return reinterpret_cast<sqlite3*>(p);}
static std::string quickIdentity(const std::string& path){std::ifstream f(path,std::ios::binary);if(!f)return{};unsigned char b[65536];f.read(reinterpret_cast<char*>(b),sizeof(b));const std::size_t n=static_cast<std::size_t>(f.gcount());std::uint64_t h=1469598103934665603ULL;for(std::size_t i=0;i<n;++i){h^=b[i];h*=1099511628211ULL;}return std::to_string(h);}
bool VideoFingerprintEngine::preparePersistentStatements() const{
 sqlite3* db=VDB(cacheDb_); if(!db)return false;
  const char* loadSql="SELECT duration,payload FROM video_fingerprint_cache WHERE path=? AND size=? AND modified=? AND quick_hash=? AND cache_version=?";
  const char* saveSql="INSERT INTO video_fingerprint_cache(path,size,modified,quick_hash,duration,step_count,cache_version,payload) VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(path) DO UPDATE SET size=excluded.size,modified=excluded.modified,quick_hash=excluded.quick_hash,duration=excluded.duration,step_count=excluded.step_count,cache_version=excluded.cache_version,payload=excluded.payload";
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
  const char* createSql="CREATE TABLE IF NOT EXISTS video_fingerprint_cache(path TEXT PRIMARY KEY,size INTEGER NOT NULL,modified INTEGER NOT NULL,quick_hash TEXT NOT NULL DEFAULT '',duration REAL NOT NULL,step_count INTEGER NOT NULL,payload BLOB NOT NULL); CREATE INDEX IF NOT EXISTS idx_vfc_stamp ON video_fingerprint_cache(size,modified);";
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
  bool hasQuick=false;
  if(sqlite3_prepare_v2(db,"PRAGMA table_info(video_fingerprint_cache)",-1,&info,nullptr)==SQLITE_OK){
    while(sqlite3_step(info)==SQLITE_ROW){const unsigned char* n=sqlite3_column_text(info,1);if(n&&std::strcmp(reinterpret_cast<const char*>(n),"quick_hash")==0){hasQuick=true;break;}}
    sqlite3_finalize(info);
  }
  if(!hasQuick && sqlite3_exec(db,"ALTER TABLE video_fingerprint_cache ADD COLUMN quick_hash TEXT NOT NULL DEFAULT '';",nullptr,nullptr,nullptr)!=SQLITE_OK){sqlite3_close(db);cacheDb_=nullptr;return false;}
  const std::string purgeSql="DELETE FROM video_fingerprint_cache WHERE cache_version != "+std::to_string(kCacheFormatVersion)+";";
  if(sqlite3_exec(db,purgeSql.c_str(),nullptr,nullptr,nullptr)!=SQLITE_OK){sqlite3_close(db);cacheDb_=nullptr;return false;}
 return preparePersistentStatements();
}
void VideoFingerprintEngine::closePersistentCache() const{
 std::lock_guard<std::mutex> lock(dbMutex_); finalizePersistentStatements(); if(cacheDb_){sqlite3_close(VDB(cacheDb_));cacheDb_=nullptr;}
}
bool VideoFingerprintEngine::loadPersistent(const std::string&p,std::uint64_t sz,std::uint64_t mt,VideoFingerprint&o,VideoCropFingerprint* crop) const{
   const std::string quick=quickIdentity(p); std::lock_guard<std::mutex> lock(dbMutex_); if(!cacheDb_)return false;
  sqlite3_stmt* s=reinterpret_cast<sqlite3_stmt*>(loadStmt_); if(!s)return false;
  sqlite3_reset(s); sqlite3_clear_bindings(s);
   sqlite3_bind_text(s,1,p.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int64(s,2,(sqlite3_int64)sz);sqlite3_bind_int64(s,3,(sqlite3_int64)mt);sqlite3_bind_text(s,4,quick.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int(s,5,kCacheFormatVersion);
  bool ok=false;
  if(sqlite3_step(s)==SQLITE_ROW){
   o.duration=sqlite3_column_double(s,0);const auto*n=(const unsigned char*)sqlite3_column_blob(s,1);int bytes=sqlite3_column_bytes(s,1);
    if(n&&bytes>=sizeof(std::uint32_t)){const char*cur=(const char*)n;
     std::uint32_t count=0;std::memcpy(&count,cur,sizeof(count));cur+=sizeof(count);
     const std::size_t frameBytes=sizeof(count)+count*(sizeof(double)+2*sizeof(std::uint64_t));
     // v5 layout: v4 (frames, 6 crops, sceneCount, scenes) + thumbCount + thumbs.
     const std::size_t headNeed=frameBytes+6*sizeof(std::uint64_t)+sizeof(std::uint32_t);
     const std::size_t thumbBytes=count*(std::size_t)VideoFingerprint::kThumbSize*VideoFingerprint::kThumbSize;
     if(headNeed<=(std::size_t)bytes){
      o.timestamps.resize(count);o.hashes.resize(count);o.mirrorHashes.resize(count);
      for(std::uint32_t i=0;i<count;++i){std::memcpy(&o.timestamps[i],cur,sizeof(double));cur+=sizeof(double);std::memcpy(&o.hashes[i],cur,sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);std::memcpy(&o.mirrorHashes[i],cur,sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);}
      std::memcpy(&o.crop4x3,cur,sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);std::memcpy(&o.crop1x1,cur,sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);std::memcpy(&o.crop9x16,cur,sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);
      std::memcpy(&o.mirrorCrop4x3,cur,sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);std::memcpy(&o.mirrorCrop1x1,cur,sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);std::memcpy(&o.mirrorCrop9x16,cur,sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);
      std::uint32_t sceneCount=0;std::memcpy(&sceneCount,cur,sizeof(std::uint32_t));cur+=sizeof(std::uint32_t);
      if(sceneCount<=count&&(std::size_t)bytes>=headNeed+sceneCount*sizeof(double)){o.sceneChanges.resize(sceneCount);for(std::uint32_t i=0;i<sceneCount;++i){std::memcpy(&o.sceneChanges[i],cur,sizeof(double));cur+=sizeof(double);}}
      // Thumbs are optional-but-strict: absent (thumbCount 0, e.g. rows written
      // before thumbs existed — none in practice since v5 bumps the version)
      // means Hamming-only scoring; present must match the frame count 1:1.
      o.thumb48.clear();
      std::uint32_t thumbCount=0;
      const char* end=(const char*)n+bytes;
      if(cur+sizeof(thumbCount)<=end){
        std::memcpy(&thumbCount,cur,sizeof(thumbCount));cur+=sizeof(thumbCount);
        if(thumbCount>0){
          if(thumbCount==count&&(std::size_t)(end-cur)>=thumbBytes){
            o.thumb48.assign((const std::uint8_t*)cur,(const std::uint8_t*)cur+thumbBytes);
            cur+=thumbBytes;
          } else { sqlite3_reset(s);sqlite3_clear_bindings(s);return false; }
        }
      }
      ok=!o.hashes.empty();
      if(ok&&crop){
        VideoCropFingerprint c;
        std::uint32_t cropCount=0;
        if(cur+sizeof(cropCount)>end){ ok=false; }
        else {
          std::memcpy(&cropCount,cur,sizeof(cropCount));cur+=sizeof(cropCount);
          const std::size_t cropNeed=(std::size_t)cropCount*6*sizeof(std::uint64_t)+sizeof(std::uint32_t);
          if(cropCount>count||(std::size_t)(end-cur)<cropNeed){ ok=false; }
          else {
            c.a4x3.resize(cropCount);c.a1x1.resize(cropCount);c.a9x16.resize(cropCount);
            c.mirrorA4x3.resize(cropCount);c.mirrorA1x1.resize(cropCount);c.mirrorA9x16.resize(cropCount);
            for(std::uint32_t i=0;i<cropCount;++i){
              std::memcpy(&c.a4x3[i],cur,sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);
              std::memcpy(&c.a1x1[i],cur,sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);
              std::memcpy(&c.a9x16[i],cur,sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);
              std::memcpy(&c.mirrorA4x3[i],cur,sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);
              std::memcpy(&c.mirrorA1x1[i],cur,sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);
              std::memcpy(&c.mirrorA9x16[i],cur,sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);
            }
            std::uint32_t cropTsCount=0;std::memcpy(&cropTsCount,cur,sizeof(cropTsCount));cur+=sizeof(cropTsCount);
            if(cropTsCount!=cropCount||(std::size_t)(end-cur)<(std::size_t)cropTsCount*sizeof(double)){ ok=false; }
            else {
              c.timestamps.resize(cropTsCount);
              for(std::uint32_t i=0;i<cropTsCount;++i){std::memcpy(&c.timestamps[i],cur,sizeof(double));cur+=sizeof(double);}
              if(cur!=end){ ok=false; }
              else *crop=std::move(c);
            }
          }
        }
      }
     }
   }
  }
sqlite3_reset(s);sqlite3_clear_bindings(s);return ok;
}
void VideoFingerprintEngine::savePersistent(const std::string&p,std::uint64_t sz,std::uint64_t mt,const VideoFingerprint&f,const VideoCropFingerprint* crop) const{
  const std::string quick=quickIdentity(p); std::lock_guard<std::mutex>lock(dbMutex_);if(!cacheDb_||f.hashes.empty()||f.timestamps.size()!=f.hashes.size())return;
  const std::uint32_t count=(std::uint32_t)f.hashes.size();const std::uint32_t sceneCount=(std::uint32_t)f.sceneChanges.size();
  const std::size_t thumbBytes=count*(std::size_t)VideoFingerprint::kThumbSize*VideoFingerprint::kThumbSize;
  const bool hasThumbs=(f.thumb48.size()==thumbBytes);
  const std::uint32_t thumbCount=hasThumbs?count:0;
  std::uint32_t cropCount=0;
  if(crop){
    const std::size_t n0=crop->a4x3.size();
    if(n0>0&&n0<=count&&crop->a1x1.size()==n0&&crop->a9x16.size()==n0&&crop->mirrorA4x3.size()==n0&&crop->mirrorA1x1.size()==n0&&crop->mirrorA9x16.size()==n0&&crop->timestamps.size()==n0) cropCount=(std::uint32_t)n0;
  }
  std::vector<unsigned char> blob(sizeof(count)+count*(sizeof(double)+2*sizeof(std::uint64_t))+6*sizeof(std::uint64_t)+sizeof(sceneCount)+sceneCount*sizeof(double)+sizeof(thumbCount)+(hasThumbs?thumbBytes:0)+sizeof(cropCount)+cropCount*6*sizeof(std::uint64_t)+sizeof(std::uint32_t)+cropCount*sizeof(double));
  char*cur=(char*)blob.data();std::memcpy(cur,&count,sizeof(count));cur+=sizeof(count);for(std::uint32_t i=0;i<count;++i){std::memcpy(cur,&f.timestamps[i],sizeof(double));cur+=sizeof(double);std::memcpy(cur,&f.hashes[i],sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);const auto mh=(i<f.mirrorHashes.size()?f.mirrorHashes[i]:0);std::memcpy(cur,&mh,sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);}const std::uint64_t crops[]={f.crop4x3,f.crop1x1,f.crop9x16,f.mirrorCrop4x3,f.mirrorCrop1x1,f.mirrorCrop9x16};for(auto h:crops){std::memcpy(cur,&h,sizeof(h));cur+=sizeof(h);}std::memcpy(cur,&sceneCount,sizeof(sceneCount));cur+=sizeof(sceneCount);for(std::uint32_t i=0;i<sceneCount;++i){std::memcpy(cur,&f.sceneChanges[i],sizeof(double));cur+=sizeof(double);}std::memcpy(cur,&thumbCount,sizeof(thumbCount));cur+=sizeof(thumbCount);if(hasThumbs){std::memcpy(cur,f.thumb48.data(),thumbBytes);cur+=thumbBytes;}
  std::memcpy(cur,&cropCount,sizeof(cropCount));cur+=sizeof(cropCount);
  for(std::uint32_t i=0;i<cropCount;++i){
    std::memcpy(cur,&crop->a4x3[i],sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);
    std::memcpy(cur,&crop->a1x1[i],sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);
    std::memcpy(cur,&crop->a9x16[i],sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);
    std::memcpy(cur,&crop->mirrorA4x3[i],sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);
    std::memcpy(cur,&crop->mirrorA1x1[i],sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);
    std::memcpy(cur,&crop->mirrorA9x16[i],sizeof(std::uint64_t));cur+=sizeof(std::uint64_t);
  }
  const std::uint32_t cropTsCount=cropCount;
  std::memcpy(cur,&cropTsCount,sizeof(cropTsCount));cur+=sizeof(cropTsCount);
  for(std::uint32_t i=0;i<cropCount;++i){std::memcpy(cur,&crop->timestamps[i],sizeof(double));cur+=sizeof(double);}
  sqlite3_stmt*s=reinterpret_cast<sqlite3_stmt*>(saveStmt_);if(!s)return;sqlite3_reset(s);sqlite3_clear_bindings(s);sqlite3_bind_text(s,1,p.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int64(s,2,(sqlite3_int64)sz);sqlite3_bind_int64(s,3,(sqlite3_int64)mt);sqlite3_bind_text(s,4,quick.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_double(s,5,f.duration);sqlite3_bind_int(s,6,(int)count);sqlite3_bind_int(s,7,kCacheFormatVersion);sqlite3_bind_blob(s,8,blob.data(),(int)blob.size(),SQLITE_TRANSIENT);sqlite3_step(s);sqlite3_reset(s);sqlite3_clear_bindings(s);
}
VideoFingerprintEngine::~VideoFingerprintEngine(){closePersistentCache();}
static void processVideoFrames(const std::vector<VideoFrame>& frames32, const std::vector<VideoFrame>& frames96, double duration, VideoFingerprint& built, VideoCropFingerprint* crops){
  built.duration=duration;
  constexpr double kMinVariance=6.0; constexpr std::size_t kFloorRatio=30;
  std::vector<char> keep(frames32.size(),1);
  std::size_t nVar=0;for(const auto&f:frames32){double s=0;for(unsigned char v:f.gray)s+=v;const double m=s/(f.gray.empty()?1:(double)f.gray.size());double sq=0;for(unsigned char v:f.gray){double dv=(double)v-m;sq+=dv*dv;}const double sd=f.gray.empty()?0.0:std::sqrt(sq/f.gray.size());if(sd<kMinVariance)keep[nVar]=0;++nVar;}
  std::size_t kept=0;for(char k:keep)if(k)++kept;
  if(frames32.size()&&kept*100/frames32.size()<kFloorRatio) keep.assign(frames32.size(),1);
  std::vector<double> ts; std::vector<std::uint64_t> hs, mhs; std::vector<std::uint8_t> thumbs;
  constexpr int kT = VideoFingerprint::kThumbSize;
  for(std::size_t i=0;i<frames32.size();++i){if(!keep[i])continue;const auto&f=frames32[i];ts.push_back(f.timestamp);{const auto hp=perceptual_hash_pair(f.gray,f.width,f.height);hs.push_back(hp.normal);mhs.push_back(hp.mirrored);}
    const std::size_t base=thumbs.size(); thumbs.resize(base+(std::size_t)kT*kT,0);
    if(i<frames96.size()){const auto&cf=frames96[i];
      if(cf.width==96&&cf.height==96&&(int)cf.gray.size()==96*96){
        for(int y=0;y<kT;++y)for(int x=0;x<kT;++x){
          const std::uint8_t* p=&cf.gray[(std::size_t)(y*2)*96+x*2];
          thumbs[base+(std::size_t)y*kT+x]=(std::uint8_t)((p[0]+p[1]+p[96]+p[97]+2)>>2);
        }
      }
    }
  }
  built.thumb48=std::move(thumbs);
  for(std::size_t i=1;i<ts.size();++i){double sim=hash_similarity(hs[i-1],hs[i]);if(sim<25.0)built.sceneChanges.push_back(ts[i]);}
  built.timestamps=std::move(ts); built.hashes=std::move(hs); built.mirrorHashes=std::move(mhs);
  if(crops){ crops->timestamps.reserve(built.hashes.size()); crops->a4x3.reserve(built.hashes.size()); crops->a1x1.reserve(built.hashes.size()); crops->a9x16.reserve(built.hashes.size()); crops->mirrorA4x3.reserve(built.hashes.size()); crops->mirrorA1x1.reserve(built.hashes.size()); crops->mirrorA9x16.reserve(built.hashes.size()); }
  for(std::size_t i=0;i<frames96.size()&&i<keep.size();++i){if(!keep[i])continue;const auto&cf=frames96[i];GrayImage g;g.width=cf.width;g.height=cf.height;g.pixels=cf.gray;auto c=cropFingerprints(g);built.crop4x3^=c.a4x3;built.crop1x1^=c.a1x1;built.crop9x16^=c.a9x16;built.mirrorCrop4x3^=c.mirrorA4x3;built.mirrorCrop1x1^=c.mirrorA1x1;built.mirrorCrop9x16^=c.mirrorA9x16;
    if(crops){ crops->timestamps.push_back(i<frames32.size()?frames32[i].timestamp:cf.timestamp); crops->a4x3.push_back(c.a4x3); crops->a1x1.push_back(c.a1x1); crops->a9x16.push_back(c.a9x16); crops->mirrorA4x3.push_back(c.mirrorA4x3); crops->mirrorA1x1.push_back(c.mirrorA1x1); crops->mirrorA9x16.push_back(c.mirrorA9x16); }
  }
}
bool VideoFingerprintEngine::build(const std::string&p,VideoFingerprint&o)const{
  // NOTE: p is UTF-8. Build the path with path_from_utf8 first: constructing
  // fs::path from a narrow string throws on Windows when the name holds
  // characters outside the ANSI code page (observed terminate() on Korean
  // filenames), even when an error_code is supplied.
  std::error_code ec; const fs::path fp=path_from_utf8(p);
  if(!std::filesystem::exists(fp,ec))return false; const auto sz=std::filesystem::file_size(fp,ec);if(ec)return false;const auto mt=(std::uint64_t)std::filesystem::last_write_time(fp,ec).time_since_epoch().count();if(ec)return false;
  if(memoryLookup(p,sz,mt,o,nullptr,false))return true;
  if(loadPersistent(p,sz,mt,o,nullptr)){memoryStore(p,sz,mt,o,nullptr);return true;}
  VideoDecoder d;if(!d.open(p))return false;VideoInfo i;if(!d.info(i)){d.close();return false;}
  VideoFingerprint built; auto plan=make_sample_plan(i.duration);
  std::vector<VideoFrame> frames32, frames96;
  if(!d.framesAt(plan.timestamps,32,32,frames32)){d.close();return false;}
  // The crop pass uses the same timestamp list but a larger decode. This remains a
  // second resolution pass; each pass itself is a single sequential decode rather than
  // one seek per timestamp.
  d.framesAt(plan.timestamps,96,96,frames96);
  processVideoFrames(frames32,frames96,i.duration,built,nullptr);
  d.close();if(built.hashes.empty())return false;
  memoryStore(p,sz,mt,built,nullptr);savePersistent(p,sz,mt,built,nullptr);o=std::move(built);return true;
}
bool VideoFingerprintEngine::buildFull(const std::string&p,VideoFingerprint& base,VideoCropFingerprint& crop,int decodeSize) const{
  std::error_code ec2; const fs::path fp2=path_from_utf8(p);
  if(!std::filesystem::exists(fp2,ec2))return false; const auto sz2=std::filesystem::file_size(fp2,ec2);if(ec2)return false;const auto mt2=(std::uint64_t)std::filesystem::last_write_time(fp2,ec2).time_since_epoch().count();if(ec2)return false;
  if(memoryLookup(p,sz2,mt2,base,&crop,true))return true;
  VideoFingerprint b; VideoCropFingerprint c;
  if(loadPersistent(p,sz2,mt2,b,&c)){memoryStore(p,sz2,mt2,b,&c);base=std::move(b);crop=std::move(c);return true;}
  VideoDecoder d;if(!d.open(p))return false;VideoInfo i;if(!d.info(i)){d.close();return false;}
  const int size=std::max(32,std::min(192,decodeSize));
  auto plan=make_sample_plan(i.duration);
  std::vector<VideoFrame> frames32, framesHi;
  if(!d.framesAt(plan.timestamps,32,32,frames32)){d.close();return false;}
  d.framesAt(plan.timestamps,size,size,framesHi);
  d.close();
  VideoCropFingerprint cHi;
  processVideoFrames(frames32,size==96?framesHi:std::vector<VideoFrame>(),i.duration,b,size==96?&cHi:nullptr);
  if(size!=96){
    VideoDecoder d2;
    if(d2.open(p)){ std::vector<VideoFrame> frames96; d2.framesAt(plan.timestamps,96,96,frames96); d2.close();
      for(std::size_t k=0;k<frames96.size()&&k<b.timestamps.size();++k){ GrayImage g;g.width=frames96[k].width;g.height=frames96[k].height;g.pixels=frames96[k].gray;const auto h=cropFingerprints(g);cHi.timestamps.push_back(b.timestamps[k]);cHi.a4x3.push_back(h.a4x3);cHi.a1x1.push_back(h.a1x1);cHi.a9x16.push_back(h.a9x16);cHi.mirrorA4x3.push_back(h.mirrorA4x3);cHi.mirrorA1x1.push_back(h.mirrorA1x1);cHi.mirrorA9x16.push_back(h.mirrorA9x16); }
    }
  }
  if(b.hashes.empty())return false;
  memoryStore(p,sz2,mt2,b,&cHi);savePersistent(p,sz2,mt2,b,&cHi);base=std::move(b);crop=std::move(cHi);return true;
}

void VideoFingerprintEngine::clearCache()const{std::lock_guard<std::mutex>lock(cacheMutex_);cacheMap_.clear();cacheList_.clear();}
bool VideoFingerprintEngine::peekThumb48(const std::string& p,std::vector<std::uint8_t>& gray48) const{
  constexpr std::size_t kPx=(std::size_t)VideoFingerprint::kThumbSize*VideoFingerprint::kThumbSize;
  std::error_code ec; const fs::path fp=path_from_utf8(p);
  if(!std::filesystem::exists(fp,ec))return false; const auto sz=std::filesystem::file_size(fp,ec);if(ec)return false;const auto mt=(std::uint64_t)std::filesystem::last_write_time(fp,ec).time_since_epoch().count();if(ec)return false;
  {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it=cacheMap_.find(p);
    if(it!=cacheMap_.end()){
      const CacheEntry& e=it->second->second;
      if(e.size==sz&&e.modified==mt&&e.fingerprint.thumb48.size()>=kPx){
        gray48.assign(e.fingerprint.thumb48.begin(),e.fingerprint.thumb48.begin()+kPx);
        cacheList_.splice(cacheList_.begin(),cacheList_,it->second);
        return true;
      }
    }
  }
  VideoFingerprint vf;
  if(!loadPersistent(p,sz,mt,vf,nullptr)) return false;
  if(vf.thumb48.size()<kPx) return false;
  gray48.assign(vf.thumb48.begin(),vf.thumb48.begin()+kPx);
  return true;
}
std::size_t VideoFingerprintEngine::memoryCacheSize()const{std::lock_guard<std::mutex>lock(cacheMutex_);return cacheMap_.size();}
bool VideoFingerprintEngine::memoryLookup(const std::string&p,std::uint64_t sz,std::uint64_t mt,VideoFingerprint&o,VideoCropFingerprint* crop,bool needCrop) const{
  const std::string quick=quickIdentity(p); std::lock_guard<std::mutex> lock(cacheMutex_);
  auto it=cacheMap_.find(p);
  if(it==cacheMap_.end()) return false;
  CacheEntry& e=it->second->second;
  if(e.size!=sz||e.modified!=mt||e.quickHash!=quick) return false;
  if(needCrop&&!e.hasCrop) return false;
  cacheList_.splice(cacheList_.begin(),cacheList_,it->second);
  o=e.fingerprint;
  if(crop&&e.hasCrop) *crop=e.crop;
  return !o.hashes.empty();
}
void VideoFingerprintEngine::memoryStore(const std::string&p,std::uint64_t sz,std::uint64_t mt,const VideoFingerprint&f,const VideoCropFingerprint* crop) const{
  const std::string quick=quickIdentity(p); std::lock_guard<std::mutex> lock(cacheMutex_);
  auto it=cacheMap_.find(p);
  if(it!=cacheMap_.end()){
    it->second->second.size=sz; it->second->second.modified=mt; it->second->second.quickHash=quick;
    it->second->second.fingerprint=f;
    if(crop){ it->second->second.crop=*crop; it->second->second.hasCrop=true; }
    cacheList_.splice(cacheList_.begin(),cacheList_,it->second);
    return;
  }
  while(cacheMap_.size()>=kMemoryCacheMax){
    cacheMap_.erase(cacheList_.back().first);
    cacheList_.pop_back();
  }
  CacheEntry e; e.size=sz; e.modified=mt; e.quickHash=quick; e.fingerprint=f;
  if(crop){ e.crop=*crop; e.hasCrop=true; }
  cacheList_.emplace_front(p,std::move(e));
  cacheMap_[p]=cacheList_.begin();
}
// Scene boundaries act as stable DTW anchors: both sides being at a cut gets a
// small similarity bonus (scene cuts survive re-encodes even when frames shift).
// sceneBonus==0 keeps the legacy scoring bit-identical.
static std::vector<char> sceneFlags(const VideoFingerprint& v){
  std::vector<char> f(v.timestamps.size(),0); if(v.sceneChanges.empty()) return f;
  for(std::size_t i=0;i<v.timestamps.size();++i) for(double scc:v.sceneChanges) if(std::abs(v.timestamps[i]-scc)<1e-6){f[i]=1;break;}
  return f;
}
struct ThinPlan { bool active=false; std::vector<std::size_t> keepA, keepB; };
static double gridStep(const VideoFingerprint& v){
  if(v.timestamps.size()<2||v.hashes.empty()) return 0;
  return v.timestamps[1]-v.timestamps[0];
}
static ThinPlan planGridThin(const VideoFingerprint& a, const VideoFingerprint& b){
  ThinPlan p;
  if(a.timestamps.size()!=a.hashes.size()||b.timestamps.size()!=b.hashes.size()) return p;
  const double ia=gridStep(a), ib=gridStep(b);
  if(ia<=0||ib<=0||std::abs(ia-ib)<=1e-9) return p;
  const bool aFine=(ia<ib);
  const double k=(aFine?ib:ia)/(aFine?ia:ib);
  if(k<2||std::abs(k-std::round(k))>0.01) return p;
  const std::vector<double>& ft=aFine?a.timestamps:b.timestamps;
  const std::vector<double>& ct=aFine?b.timestamps:a.timestamps;
  std::vector<std::size_t> keep; std::vector<char> hit(ct.size(),0);
  for(std::size_t i=0;i<ft.size();++i)
    for(std::size_t j=0;j<ct.size();++j)
      if(std::abs(ft[i]-ct[j])<1e-3){ keep.push_back(i); hit[j]=1; break; }
  std::size_t core=0, hitCore=0;
  for(std::size_t j=0;j+1<ct.size();++j){ ++core; if(hit[j]) ++hitCore; }
  if(keep.size()<3||core==0||hitCore*5<core*4) return p;
  p.active=true;
  if(aFine){ p.keepA=keep; p.keepB.resize(b.hashes.size()); for(std::size_t j=0;j<p.keepB.size();++j) p.keepB[j]=j; }
  else { p.keepB=keep; p.keepA.resize(a.hashes.size()); for(std::size_t i=0;i<p.keepA.size();++i) p.keepA[i]=i; }
  return p;
}
static VideoFingerprint thinVideo(const VideoFingerprint& s, const std::vector<std::size_t>& keep){
  VideoFingerprint o; o.duration=s.duration;
  constexpr std::size_t kPx=(std::size_t)VideoFingerprint::kThumbSize*VideoFingerprint::kThumbSize;
  const bool th=(s.thumb48.size()==s.hashes.size()*kPx);
  for(auto i:keep){ if(i>=s.hashes.size()||i>=s.timestamps.size()) break;
    o.timestamps.push_back(s.timestamps[i]); o.hashes.push_back(s.hashes[i]);
    o.mirrorHashes.push_back(i<s.mirrorHashes.size()?s.mirrorHashes[i]:0);
    if(th) o.thumb48.insert(o.thumb48.end(),s.thumb48.begin()+i*kPx,s.thumb48.begin()+(i+1)*kPx);
  }
  o.crop4x3=s.crop4x3; o.crop1x1=s.crop1x1; o.crop9x16=s.crop9x16;
  o.mirrorCrop4x3=s.mirrorCrop4x3; o.mirrorCrop1x1=s.mirrorCrop1x1; o.mirrorCrop9x16=s.mirrorCrop9x16;
  o.sceneChanges=s.sceneChanges;
  return o;
}
static VideoCropFingerprint thinCrop(const VideoCropFingerprint& s, const std::vector<std::size_t>& keep){
  VideoCropFingerprint o;
  auto sub=[&](const std::vector<std::uint64_t>& v){ std::vector<std::uint64_t> r; for(auto i:keep){ if(i>=v.size()) break; r.push_back(v[i]); } return r; };
  o.a4x3=sub(s.a4x3); o.a1x1=sub(s.a1x1); o.a9x16=sub(s.a9x16);
  o.mirrorA4x3=sub(s.mirrorA4x3); o.mirrorA1x1=sub(s.mirrorA1x1); o.mirrorA9x16=sub(s.mirrorA9x16);
  for(auto i:keep){ if(i>=s.timestamps.size()) break; o.timestamps.push_back(s.timestamps[i]); }
  return o;
}
double frame_ssim(const std::uint8_t* a, const std::uint8_t* b, int w, int h){
 if(!a||!b||w<=0||h<=0) return 0;
 // MSSIM with uniform (non-Gaussian) 8x8 windows: a few thousand integer-ish
 // ops per frame pair, far below one CPU pHash. Standard C1/C2 stability
 // constants. Identical frames -> ~1; unrelated content -> low. Edge strips
 // narrower than 8px are dropped (floor windows), never stretched.
 constexpr double C1=6.5025, C2=58.5225; // (0.01*255)^2, (0.03*255)^2
 const int ww=(w/8)*8, hh=(h/8)*8;
 if(ww<=0||hh<=0) return 0;
 double acc=0; int nw=0;
 for(int wy=0;wy<hh;wy+=8)for(int wx=0;wx<ww;wx+=8){
  double sx=0,sy=0,sxx=0,syy=0,sxy=0;
  for(int dy=0;dy<8;++dy)for(int dx=0;dx<8;++dx){
   const double x=(double)a[(wy+dy)*w+wx+dx], y=(double)b[(wy+dy)*w+wx+dx];
   sx+=x; sy+=y; sxx+=x*x; syy+=y*y; sxy+=x*y;
  }
  const double mx=sx/64.0, my=sy/64.0;
  const double vx=sxx/64.0-mx*mx, vy=syy/64.0-my*my, cv=sxy/64.0-mx*my;
  const double num=(2*mx*my+C1)*(2*cv+C2), den=(mx*mx+my*my+C1)*(vx+vy+C2);
  acc+=(den>0?num/den:1.0); ++nw;
 }
 if(!nw) return 0;
 return std::clamp(acc/nw,0.0,1.0);
}
double video_similarity(const VideoFingerprint&a,const VideoFingerprint&b,const VideoSimilarityOptions& options){
  if(a.hashes.empty()||b.hashes.empty())return 0;const double threshold=std::clamp(options.thresholdPercent,0.0,100.0),gap=std::max(0.0,options.gapPenalty),tol=std::max(0.0,options.timeToleranceSeconds),bonus=std::max(0.0,options.sceneBonus);
  ThinPlan tp=planGridThin(a,b);
  VideoFingerprint a2,b2; const VideoFingerprint *pa=&a,*pb=&b;
  if(tp.active){ a2=thinVideo(a,tp.keepA); b2=thinVideo(b,tp.keepB); pa=&a2; pb=&b2; }
  const auto&x=(pa->hashes.size()<=pb->hashes.size()?*pa:*pb);const auto&y=(pa->hashes.size()<=pb->hashes.size()?*pb:*pa);const std::size_t n=x.hashes.size(),m=y.hashes.size();std::vector<double>prev(m+1),cur(m+1);double best=0;
 const std::vector<char> xa=sceneFlags(x), yb=sceneFlags(y);
 // L3 verification prep: thumbs must be 1:1 with hashes on both sides.
 // All-zero 48x48 blocks mean "no thumb" (cache predates v5, or the 96 pass
 // failed at build): those cells stay Hamming-only, i.e. legacy scoring.
 constexpr int kT=VideoFingerprint::kThumbSize; constexpr std::size_t kPx=(std::size_t)kT*kT;
 const bool useSsim=(x.thumb48.size()==n*kPx&&y.thumb48.size()==m*kPx);
 std::vector<char> xHas, yHas; std::vector<std::uint8_t> yFlip;
 if(useSsim){
  xHas.assign(n,0); yHas.assign(m,0); yFlip.resize(m*kPx);
  for(std::size_t i=0;i<n;++i){const auto* p=&x.thumb48[i*kPx]; for(std::size_t k=0;k<kPx;++k) if(p[k]){xHas[i]=1;break;}}
  for(std::size_t j=0;j<m;++j){
   const auto* p=&y.thumb48[j*kPx]; for(std::size_t k=0;k<kPx;++k) if(p[k]){yHas[j]=1;break;}
   auto* f=&yFlip[j*kPx]; for(int yy=0;yy<kT;++yy)for(int xx=0;xx<kT;++xx) f[(std::size_t)yy*kT+xx]=p[(std::size_t)yy*kT+(kT-1-xx)];
  }
 }
 for(std::size_t i=1;i<=n;++i){cur[0]=0;for(std::size_t j=1;j<=m;++j){double sim=hash_similarity(x.hashes[i-1],y.hashes[j-1]);
 std::uint64_t xm=(i-1<x.mirrorHashes.size()?x.mirrorHashes[i-1]:0), ym=(j-1<y.mirrorHashes.size()?y.mirrorHashes[j-1]:0);
 if(xm) sim=std::max(sim,hash_similarity(xm,y.hashes[j-1]));
 if(ym) sim=std::max(sim,hash_similarity(x.hashes[i-1],ym));
 if(xm&&ym) sim=std::max(sim,hash_similarity(xm,ym));
  // L3 gate (not replacement): only cells the Hamming stage already passes
  // pay for SSIM. Failing cells keep the cheap reject path below, so SSIM can
  // never promote a Hamming reject, and costs nothing on misses. Passing cells
  // are re-scored 0.4*Hamming + 0.6*SSIM, so a same-low-frequency false
  // positive (high H, low S) drops while true re-encodes (high H, high S)
  // hold. Missing/flat thumbs skip to Hamming.
  if(sim>=threshold&&useSsim&&xHas[i-1]&&yHas[j-1]){
   const double s1=frame_ssim(&x.thumb48[(i-1)*kPx],&y.thumb48[(j-1)*kPx],kT,kT);
   const double s2=frame_ssim(&x.thumb48[(i-1)*kPx],&yFlip[(j-1)*kPx],kT,kT);
   sim=0.4*sim+0.6*(100.0*std::max(s1,s2));
  }
  double timePenalty=0;if(i-1<x.timestamps.size()&&j-1<y.timestamps.size()&&tol>0){if(i>1&&j>1){double dx=x.timestamps[i-1]-x.timestamps[i-2];double dy=y.timestamps[j-1]-y.timestamps[j-2];double dt=std::abs(dx-dy);timePenalty=std::min(20.0,20.0*dt/tol);}}double match=(sim>=threshold?sim:sim-100.0)-timePenalty;if(bonus>0&&xa[i-1]&&yb[j-1])match+=bonus;cur[j]=std::max({0.0,prev[j-1]+match,prev[j]-gap,cur[j-1]-gap});best=std::max(best,cur[j]);}std::swap(prev,cur);}
 return std::clamp(100.0*best/(100.0*n),0.0,100.0);
}

static double crop_temporal_score(const VideoFingerprint& baseA, const VideoCropFingerprint& cropA,
                                  const VideoFingerprint& baseB, const VideoCropFingerprint& cropB,
                                  const VideoSimilarityOptions& options){
  const double threshold=std::clamp(options.thresholdPercent,0.0,100.0), gap=std::max(0.0,options.gapPenalty), tol=std::max(0.0,options.timeToleranceSeconds), bonus=std::max(0.0,options.sceneBonus);
  ThinPlan tp=planGridThin(baseA,baseB);
  VideoFingerprint ba2,bb2; VideoCropFingerprint ca2,cb2;
  const VideoFingerprint *pba=&baseA,*pbb=&baseB; const VideoCropFingerprint *pca=&cropA,*pcb=&cropB;
  if(tp.active){ ba2=thinVideo(baseA,tp.keepA); bb2=thinVideo(baseB,tp.keepB); ca2=thinCrop(cropA,tp.keepA); cb2=thinCrop(cropB,tp.keepB); pba=&ba2; pbb=&bb2; pca=&ca2; pcb=&cb2; }
  const std::size_t n=std::min(pca->timestamps.size(),pba->hashes.size());
  const std::size_t m=std::min(pcb->timestamps.size(),pbb->hashes.size());
  if(!n||!m) return 0;
  const std::vector<char> xa=sceneFlags(*pba), yb=sceneFlags(*pbb);
  const std::uint64_t* ax[6]={pca->a4x3.data(),pca->a1x1.data(),pca->a9x16.data(),pca->mirrorA4x3.data(),pca->mirrorA1x1.data(),pca->mirrorA9x16.data()};
  const std::uint64_t* bx[6]={pcb->a4x3.data(),pcb->a1x1.data(),pcb->a9x16.data(),pcb->mirrorA4x3.data(),pcb->mirrorA1x1.data(),pcb->mirrorA9x16.data()};
 const int ratioOf[6]={0,1,2,0,1,2}; double globalBest=0;
 // Each crop ratio is evaluated independently. This deliberately prevents
 // arbitrary 4:3-vs-1:1 crop-to-crop matching while still allowing crop/full.
 for(int r=0;r<3;++r){
   std::vector<double> prev(m+1),cur(m+1); double best=0;
   for(std::size_t i=1;i<=n;++i){cur[0]=0;
      for(std::size_t j=1;j<=m;++j){double sim=0;
        const int ai0=r, ai1=r+3, bj0=r, bj1=r+3;
        for(int u: {ai0,ai1}) for(int v: {bj0,bj1}) if(ax[u]&&bx[v]) sim=std::max(sim,hash_similarity(ax[u][i-1],bx[v][j-1]));
        for(int u: {ai0,ai1}){if(ax[u]){sim=std::max(sim,hash_similarity(ax[u][i-1],pbb->hashes[j-1])); if(j-1<pbb->mirrorHashes.size())sim=std::max(sim,hash_similarity(ax[u][i-1],pbb->mirrorHashes[j-1]));}}
        for(int v: {bj0,bj1}){if(bx[v]){sim=std::max(sim,hash_similarity(pba->hashes[i-1],bx[v][j-1])); if(i-1<pba->mirrorHashes.size())sim=std::max(sim,hash_similarity(pba->mirrorHashes[i-1],bx[v][j-1]));}}
        double timePenalty=0;if(tol>0&&i>1&&j>1){double dx=pca->timestamps[i-1]-pca->timestamps[i-2],dy=pcb->timestamps[j-1]-pcb->timestamps[j-2];timePenalty=std::min(20.0,20.0*std::abs(dx-dy)/tol);}double match=(sim>=threshold?sim:sim-100.0)-timePenalty;if(bonus>0&&i-1<xa.size()&&j-1<yb.size()&&xa[i-1]&&yb[j-1])match+=bonus;cur[j]=std::max({0.0,prev[j-1]+match,prev[j]-gap,cur[j-1]-gap});best=std::max(best,cur[j]);
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
