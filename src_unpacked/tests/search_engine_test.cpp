#include "media_search_engine.h"
#include "fingerprint.h"
#include <filesystem>
#include <fstream>
#include <iostream>
static void img(const std::filesystem::path&p,int high){std::ofstream f(p,std::ios::binary);f<<"P5\n64 64\n255\n";for(int i=0;i<4096;i++)f.put((char)((i%64)<32?high:20));}
int main(){auto d=std::filesystem::temp_directory_path()/"msf_engine_test";std::filesystem::remove_all(d);std::filesystem::create_directories(d);img(d/"a.jpg",220);std::filesystem::copy_file(d/"a.jpg",d/"b.jpg");img(d/"c.jpg",200);msf::MediaSearchEngine e;if(!e.openIndex((d/"index.sqlite").string()))return 1;auto a=e.scan(d.string());if(a.scanned!=3||a.analyzed<3||a.groups<1)return 2; if(e.gpuActive())return 17;auto b=e.scan(d.string());if(b.analyzed!=0||b.unchanged<3||b.groups<1)return 3;std::filesystem::remove(d/"c.jpg");auto c=e.scan(d.string());if(c.removed!=1)return 4;msf::ScanControl nc;nc.telemetryEnabled=false;auto d2=e.scan(d.string(),8,&nc);if(d2.groups<1||!e.hasTelemetry())return 18;{const std::string bj=e.telemetryJson();if(bj.find("\"detail\":false")==std::string::npos||bj.find("\"completed\":true")==std::string::npos)return 19;}std::vector<std::uint8_t> px(32*32,10), mx(32*32,10);
for(int y=5;y<27;++y) for(int x=4;x<14;++x){ px[y*32+x]=240; mx[y*32+(31-x)]=240; }
const auto hf=msf::perceptual_hash(px,32,32), hm=msf::perceptual_hash_mirrored(px,32,32);
const auto hmf=msf::perceptual_hash(mx,32,32), hmm=msf::perceptual_hash_mirrored(mx,32,32);
if(hf!=hmm || hm!=hmf) return 5;
auto mirrorPath=(d/"mirror.jpg").string(); auto normalPath=(d/"normal.jpg").string();
if(!e.upsertFingerprint(normalPath,hf,(int)msf::MediaKind::Image,1,1,hm)) return 6;
if(!e.upsertFingerprint(mirrorPath,hmf,(int)msf::MediaKind::Image,1,2,hmm)) return 7;
auto mm=e.compareFingerprint(hf,(int)msf::MediaKind::Image,100.0,normalPath,hm);
if(mm.empty() || mm[0].rightPath!=mirrorPath || mm[0].percent<99.9) return 8;
// Crop-aware query: a 16:9 source crop must retrieve a record carrying the same-ratio crop fingerprint.
std::vector<std::uint8_t> cropPx(32*32,30); for(int y=6;y<26;++y) for(int x=8;x<24;++x) cropPx[y*32+x]=230;
const auto cropH=msf::perceptual_hash(cropPx,32,32);
const auto otherH=msf::perceptual_hash_mirrored(cropPx,32,32);
auto cropPath=(d/"crop-target.jpg").string(); if(!e.upsertFingerprint(cropPath,123456789ULL,(int)msf::MediaKind::Image,1,3,otherH,cropH,0,0,msf::perceptual_hash_mirrored(cropPx,32,32),0,0)) return 9;
auto cm=e.compareFingerprint(987654321ULL,(int)msf::MediaKind::Image,100.0,"",0,cropH); if(cm.empty()||cm[0].rightPath!=cropPath||cm[0].percent<99.9) return 10;
// Cross-ratio crop hashes alone must not be treated as equivalent.
auto crossPath=(d/"cross-target.jpg").string(); if(!e.upsertFingerprint(crossPath,111111111ULL,(int)msf::MediaKind::Image,1,4,0,0,0,0,0,0,0)) return 11;
// Use a 4:3-only fingerprint equal to the query 1:1 hash; query is 1:1 and target has only 4:3.
if(!e.upsertFingerprint(crossPath,111111111ULL,(int)msf::MediaKind::Image,1,4,0,cropH,0,0,0,0,0)) return 12;
auto cross=e.compareFingerprint(222222222ULL,(int)msf::MediaKind::Image,100.0,"",0,0,cropH); for(const auto&m:cross) if(m.rightPath==crossPath) return 13;
// Video crop candidate index regression: crop signature can retrieve a video even when full hashes differ.
auto vf1=(d/"v1.mp4").string(); auto vf2=(d/"v2.mp4").string();
if(!e.upsertFingerprint(vf1,0x1111000011110000ULL,(int)msf::MediaKind::Video,10,5,0,0xABCDEF01ULL,0,0,0x10ULL,0,0)) return 14;
if(!e.upsertFingerprint(vf2,0x2222000022220000ULL,(int)msf::MediaKind::Video,10,6,0,0xABCDEF01ULL,0,0,0x20ULL,0,0)) return 15;
auto vm=e.compareFingerprint(0x9999000099990000ULL,(int)msf::MediaKind::Video,100.0,"",0,0xABCDEF01ULL); if(vm.empty()||vm[0].rightPath!=vf1) return 16;
// 0.9.4.116 regression: deleting a file externally and rescanning must remove
// its match pairs too, not only its files row. The old walk reconciliation
// called Database::remove() (files row only), so the deleted file survived as
// a phantom group member forever (0-byte size, blank detail) while its match
// rows kept listing it.
{
  std::filesystem::copy_file(d/"a.jpg", d/"del_a.jpg", std::filesystem::copy_options::overwrite_existing);
  std::filesystem::copy_file(d/"a.jpg", d/"del_b.jpg", std::filesystem::copy_options::overwrite_existing);
  const auto pre=e.scan(d.string());
  if(pre.analyzed<2) return 25;
  if(!e.saveMatches(pre.matches)) return 26;
  bool pairSeen=false;
  for(const auto& m:e.loadMatches())
    if(m.leftPath.find("del_")!=std::string::npos||m.rightPath.find("del_")!=std::string::npos) pairSeen=true;
  if(!pairSeen) return 27;
  std::filesystem::remove(d/"del_b.jpg");
  const auto post=e.scan(d.string());
  if(post.removed<1) return 28;
  for(const auto& m:e.loadMatches())
    if(m.leftPath.find("del_b.jpg")!=std::string::npos||m.rightPath.find("del_b.jpg")!=std::string::npos)
      return 29; // pre-fix: the pair rows survive the rescan
}
// 0.9.4.116 Load-path auto-repair: cleanupMissingOnDisk drops vanished match
// endpoints so a Load never lists phantom members; an unreachable root must
// be a no-op (an offline drive must not wipe the index).
{
  std::filesystem::copy_file(d/"a.jpg", d/"rep_a.jpg", std::filesystem::copy_options::overwrite_existing);
  std::filesystem::copy_file(d/"a.jpg", d/"rep_b.jpg", std::filesystem::copy_options::overwrite_existing);
  const auto rp=e.scan(d.string());
  if(!e.saveMatches(rp.matches)) return 30;
  std::filesystem::remove(d/"rep_b.jpg");
  const std::size_t cleaned=e.cleanupMissingOnDisk(d.string());
  if(cleaned<1) return 31;
  for(const auto& m:e.loadMatches())
    if(m.leftPath.find("rep_b.jpg")!=std::string::npos||m.rightPath.find("rep_b.jpg")!=std::string::npos)
      return 32;
  if(e.cleanupMissingOnDisk((d/"no_such_root").string())!=0) return 33;
}
// 0.9.4.114 regression: loadMatches (Load button + scan quick-load) must
// survive large candidate sets. The 0.9.4.113 memo returned a reference into
// an unordered_map; the second lookup's insert/rehash invalidated the first
// reference (use-after-free) and crashed the backend on real datasets
// (field: crash exit code 62097 on Load and Start Scan). 300 distinct
// byte-identical pairs force many memo inserts/rehashes.
{
  auto big=d/"big"; std::filesystem::create_directories(big);
  auto noise=[&](const std::filesystem::path&p,unsigned seed){
    std::ofstream f(p,std::ios::binary);
    const int w=8,h=8,row=w*3,im=row*h,fs=54+im; unsigned char hd[54]={0};
    hd[0]='B'; hd[1]='M'; hd[2]=(unsigned char)(fs&0xFF); hd[3]=(unsigned char)((fs>>8)&0xFF);
    hd[10]=54; hd[14]=40; hd[18]=(unsigned char)w; hd[22]=(unsigned char)h; hd[26]=1; hd[28]=24;
    hd[34]=(unsigned char)(im&0xFF); hd[35]=(unsigned char)((im>>8)&0xFF);
    f.write((const char*)hd,54);
    unsigned s=seed*2654435761u+1;
    for(int y=0;y<h;++y) for(int x=0;x<w;++x){ s=s*1103515245u+12345u; const unsigned char v=(unsigned char)(((s>>16)&0xFF)>127?255:0); f.put((char)v); f.put((char)v); f.put((char)v); }
  };
  for(int i=0;i<3000;++i){
    noise(big/("p"+std::to_string(i)+"_a.bmp"),(unsigned)(i+1));
    std::filesystem::copy_file(big/("p"+std::to_string(i)+"_a.bmp"),big/("p"+std::to_string(i)+"_b.bmp"));
  }
  msf::MediaSearchEngine e2; if(!e2.openIndex((d/"big.sqlite").string())) return 20;
  const auto rb=e2.scan(big.string());
  if(rb.scanned!=6000||rb.groups<3000) return 21;
  // The engine-level scan() keeps matches in memory; persistence is the
  // worker's job. Store the found pairs so loadMatches (the quick-load path)
  // actually processes a large candidate set.
  if(!e2.saveMatches(rb.matches)) return 22;
  const auto lm=e2.loadMatches();
  if(lm.size()<3000) return 23;
  std::size_t identical=0; for(const auto&m:lm) if(m.contentIdentical) ++identical;
  if(identical!=lm.size()) return 24; // every stored pair is a byte-identical copy
  e2.close();
  std::cout<<"load_matches_big=ok pairs="<<lm.size()<<"\n";
}
std::cout<<"search_engine=ok\nfirst_analyzed="<<a.analyzed<<"\nincremental_analyzed="<<b.analyzed<<"\nremoved="<<c.removed<<"\nmirror_query=ok\n";e.close();std::filesystem::remove_all(d);return 0;}