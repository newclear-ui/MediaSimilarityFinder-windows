#include "video_fingerprint.h"
#include <filesystem>
#include <iostream>
#include <sqlite3.h>
#include <cstring>
#include <vector>
#include <fstream>
#include <string>

static std::string quick_identity(const std::string& path){
  std::ifstream f(path,std::ios::binary); if(!f)return{};
  unsigned char b[65536]; f.read(reinterpret_cast<char*>(b),sizeof(b));
  const std::size_t n=static_cast<std::size_t>(f.gcount());
  std::uint64_t h=1469598103934665603ULL;
  for(std::size_t i=0;i<n;++i){h^=b[i];h*=1099511628211ULL;}
  return std::to_string(h);
}

static bool insert_seed(sqlite3* raw,const std::string& media,int version){
  constexpr int kT=48; constexpr std::size_t kPx=(std::size_t)kT*kT;
  std::vector<unsigned char> blob(sizeof(std::uint32_t)+3*(sizeof(double)+2*sizeof(std::uint64_t))+6*sizeof(std::uint64_t)+sizeof(std::uint32_t)+sizeof(std::uint32_t)+3*kPx+sizeof(std::uint32_t)+3*6*sizeof(std::uint64_t)+sizeof(std::uint32_t)+3*sizeof(double));
  std::uint32_t n=3; std::memcpy(blob.data(),&n,sizeof(n)); char* c=(char*)blob.data()+sizeof(n);
  const std::uint64_t hashes[3]={1,3,7};
  for(int i=0;i<3;++i){double t=i*2.0;std::uint64_t h=hashes[i];std::memcpy(c,&t,sizeof(t));c+=sizeof(t);std::memcpy(c,&h,sizeof(h));c+=sizeof(h);std::uint64_t mh=h^0xffffULL;std::memcpy(c,&mh,sizeof(mh));c+=sizeof(mh);}
  const std::uint64_t crops[6]={11,22,33,44,55,66}; for(auto h:crops){std::memcpy(c,&h,sizeof(h));c+=sizeof(h);}
  std::uint32_t sceneCount=0; std::memcpy(c,&sceneCount,sizeof(sceneCount)); c+=sizeof(sceneCount);
  std::uint32_t thumbCount=3; std::memcpy(c,&thumbCount,sizeof(thumbCount)); c+=sizeof(thumbCount);
  for(int i=0;i<3;++i){ for(std::size_t k=0;k<kPx;++k) *c++=(char)((i*64+k)&0xFF); }
  std::uint32_t cropCount=3; std::memcpy(c,&cropCount,sizeof(cropCount)); c+=sizeof(cropCount);
  for(int i=0;i<3;++i){const std::uint64_t n=static_cast<std::uint64_t>(i); const std::uint64_t ch[6]={101+n,201+n,301+n,401+n,501+n,601+n}; for(auto h:ch){std::memcpy(c,&h,sizeof(h));c+=sizeof(h);}}
  std::uint32_t cropTsCount=3; std::memcpy(c,&cropTsCount,sizeof(cropTsCount)); c+=sizeof(cropTsCount);
  for(int i=0;i<3;++i){double t=i*2.0;std::memcpy(c,&t,sizeof(t));c+=sizeof(t);}
 sqlite3_stmt* st=nullptr; const char*q="INSERT OR REPLACE INTO video_fingerprint_cache(path,size,modified,quick_hash,duration,step_count,cache_version,payload) VALUES(?,?,?,?,?,?,?,?)";
 if(sqlite3_prepare_v2(raw,q,-1,&st,nullptr)!=SQLITE_OK)return false;
  const auto quick=quick_identity(media);
  sqlite3_bind_text(st,1,media.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int64(st,2,(sqlite3_int64)std::filesystem::file_size(media));sqlite3_bind_int64(st,3,(sqlite3_int64)std::filesystem::last_write_time(media).time_since_epoch().count());sqlite3_bind_text(st,4,quick.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_double(st,5,12.0);sqlite3_bind_int(st,6,3);sqlite3_bind_int(st,7,version);sqlite3_bind_blob(st,8,blob.data(),(int)blob.size(),SQLITE_TRANSIENT);
 const bool ok=sqlite3_step(st)==SQLITE_DONE;sqlite3_finalize(st);return ok;
}

int main(){
 auto d=std::filesystem::temp_directory_path()/"msf_v327_cache"; std::filesystem::remove_all(d); std::filesystem::create_directories(d);
 auto db=(d/"cache.sqlite").string(); msf::VideoFingerprintEngine e; if(!e.openPersistentCache(db))return 1;
 auto media=(d/"x.mp4").string(); std::ofstream(d/"x.mp4")<<"not a video";
 sqlite3* raw=nullptr; if(sqlite3_open(db.c_str(),&raw)!=SQLITE_OK)return 2;
 sqlite3_stmt* info=nullptr; bool hasVersion=false; if(sqlite3_prepare_v2(raw,"PRAGMA table_info(video_fingerprint_cache)",-1,&info,nullptr)!=SQLITE_OK)return 3; while(sqlite3_step(info)==SQLITE_ROW){const unsigned char*n=sqlite3_column_text(info,1);if(n&&std::string(reinterpret_cast<const char*>(n))=="cache_version")hasVersion=true;}sqlite3_finalize(info);if(!hasVersion)return 4;
   if(!insert_seed(raw,media,8))return 5; sqlite3_close(raw);
  msf::VideoFingerprint out; if(!e.build(media,out)||out.hashes.size()!=3||out.mirrorHashes.size()!=3||out.crop4x3!=11||out.crop1x1!=22||out.crop9x16!=33||out.mirrorCrop4x3!=44||out.mirrorCrop1x1!=55||out.mirrorCrop9x16!=66)return 6;
  if(out.thumb48.size()!=3*(std::size_t)48*48||out.thumb48[0]!=0||out.thumb48[48*48]!=64)return 6;
  msf::VideoFingerprint fb; msf::VideoCropFingerprint fc;
  if(!e.buildFull(media,fb,fc,96))return 11;
  if(fc.a4x3.size()!=3||fc.a4x3[1]!=102||fc.mirrorA9x16[2]!=603||fc.timestamps.size()!=3||fc.timestamps[2]!=4.0)return 12;
 // A stale algorithm-version entry must be rejected before a build can reuse it.
 e.clearCache(); e.closePersistentCache();
 if(!e.openPersistentCache(db))return 7;
  raw=nullptr; if(sqlite3_open(db.c_str(),&raw)!=SQLITE_OK)return 8; if(!insert_seed(raw,media,6))return 9; sqlite3_close(raw);
   msf::VideoFingerprint stale; if(e.build(media,stale))return 10; // no valid v6 row remains, and x.mp4 is intentionally not decodable
 e.closePersistentCache(); std::filesystem::remove_all(d); std::cout<<"video_cache_api=ok\n";return 0;
}
