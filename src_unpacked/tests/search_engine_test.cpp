#include "media_search_engine.h"
#include "fingerprint.h"
#include <filesystem>
#include <fstream>
#include <iostream>
static void img(const std::filesystem::path&p,int high){std::ofstream f(p,std::ios::binary);f<<"P5\n64 64\n255\n";for(int i=0;i<4096;i++)f.put((char)((i%64)<32?high:20));}
int main(){auto d=std::filesystem::temp_directory_path()/"msf_engine_test";std::filesystem::remove_all(d);std::filesystem::create_directories(d);img(d/"a.jpg",220);std::filesystem::copy_file(d/"a.jpg",d/"b.jpg");img(d/"c.jpg",200);msf::MediaSearchEngine e;if(!e.openIndex((d/"index.sqlite").string()))return 1;auto a=e.scan(d.string());if(a.scanned!=3||a.analyzed<3||a.groups<1)return 2;auto b=e.scan(d.string());if(b.analyzed!=0||b.unchanged<3||b.groups<1)return 3;std::filesystem::remove(d/"c.jpg");auto c=e.scan(d.string());if(c.removed!=1)return 4;std::vector<std::uint8_t> px(32*32,10), mx(32*32,10);
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
std::cout<<"search_engine=ok\nfirst_analyzed="<<a.analyzed<<"\nincremental_analyzed="<<b.analyzed<<"\nremoved="<<c.removed<<"\nmirror_query=ok\n";std::filesystem::remove_all(d);return 0;}