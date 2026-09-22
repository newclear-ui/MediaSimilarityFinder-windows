#include "media_search_engine.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
int main(){
 auto d=std::filesystem::temp_directory_path()/"msf_report_test";
 std::filesystem::remove_all(d); std::filesystem::create_directories(d);
 std::ofstream(d/"a.jpg")<<"not image";
 std::ofstream(d/"b.jpg")<<"not image";
 msf::MediaSearchEngine e; if(!e.openIndex((d/"index.sqlite").string())) return 1;
 auto r=e.scan(d.string()); if(r.matches.size()>r.candidates) return 2;
 std::size_t streamed=0, compact=0;
 msf::ScanControl c;
 c.retainMatches=false;
 c.onMatch=[&](const msf::SearchMatch& m){ if(m.leftPath.empty()||m.rightPath.empty()) std::exit(3); ++streamed; };
 c.onMatchRef=[&](const msf::SearchMatchRef& m){ if(m.leftIndex>=e.files().size()||m.rightIndex>=e.files().size()) std::exit(8); ++compact; };
 auto r2=e.scan(d.string(),8,&c);
 if(!r2.matches.empty()) return 4;
 if(streamed!=r2.candidates && streamed>0) return 5;
 if(compact!=r2.candidates && compact>0) return 9;
 msf::ScanControl bounded;
 bounded.maxRetainedMatches=1;
 std::size_t all=0; bounded.onMatch=[&](const msf::SearchMatch&){++all;};
 auto r3=e.scan(d.string(),8,&bounded);
 if(r3.matches.size()>1) return 6;
 if(all!=r3.candidates && all>0) return 7;
 std::cout<<"search_report=ok\nstreamed="<<streamed<<"\ncompact="<<compact<<"\nall_matches="<<all<<"\n";
 std::filesystem::remove_all(d); return 0;
}
