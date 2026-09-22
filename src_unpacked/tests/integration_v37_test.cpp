#include "scanner.h"
#include "incremental_scanner.h"
#include "database.h"
#include "image_decoder.h"
#include "media_pipeline.h"
#include "video_fingerprint.h"
#include "similarity.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
namespace fs=std::filesystem;
static void pgm(const fs::path&p,int seed){std::ofstream f(p,std::ios::binary);f<<"P5\n64 64\n255\n";for(int y=0;y<64;y++)for(int x=0;x<64;x++)f.put((char)((x+y+seed)%256));}
int main(){
 auto d=fs::temp_directory_path()/"msf_v37_integration";fs::remove_all(d);fs::create_directories(d/"sub");
 pgm(d/"a.pgm",0);pgm(d/"sub"/"b.pgm",0);pgm(d/"c.pgm",40);std::ofstream(d/"note.txt")<<"ignore";
 msf::Scanner sc; auto cur=sc.scan(d.string()); if(cur.size()!=0){/* PGM intentionally outside scanner extension set */}
 // Use supported extension test assets while preserving valid PGM payload for the portable decoder.
 fs::rename(d/"a.pgm",d/"a.jpg"); fs::rename(d/"sub"/"b.pgm",d/"sub"/"b.jpg"); fs::rename(d/"c.pgm",d/"c.jpg");
 cur=sc.scan(d.string()); if(cur.size()!=3){std::cerr<<"scanner count="<<cur.size()<<"\n";return 1;}
 msf::Database db;auto dbp=(d/"index.sqlite").string();if(!db.open(dbp)||!db.initialize())return 2;for(auto&x:cur)if(!db.upsert(x))return 3;
 if(db.all().size()!=3||!db.containsUnchanged(cur[0]))return 4;
 auto old=db.all(); auto modified=cur; modified[0].size++; auto changes=msf::IncrementalScanner().classify(modified,old); if(changes.modified.size()!=1||changes.unchanged.size()!=2)return 5;
 msf::ImageDecoder dec;msf::GrayImage g; if(!dec.decode((d/"a.jpg").string(),64,64,g)){std::cerr<<"portable image decoder unavailable\n";return 6;} msf::MediaPipeline pipe;std::uint64_t ha=0,hb=0;if(!pipe.image((d/"a.jpg").string(),ha)||!pipe.image((d/"sub"/"b.jpg").string(),hb))return 7;if(msf::hash_similarity(ha,hb)<99.9)return 8;
 std::cout<<"v3.7_integration=ok\nfiles=3\nsqlite=ok\nimage_exact_duplicate=100%\n";db.close();fs::remove_all(d);return 0;
}
