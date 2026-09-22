#include "monitor.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
int main(){
    msf::SystemLoadMonitor lm; msf::ResourcePolicy p=msf::make_policy(msf::ResourceMode::Gaming); msf::SystemLoad low{}; assert(lm.allowAnalysis(p,low)); msf::SystemLoad critical{};critical.cpuPercent=95;assert(!lm.allowAnalysis(p,critical));
    auto dir=std::filesystem::temp_directory_path()/"msf_monitor_test";std::filesystem::create_directories(dir);
    msf::MediaSearchEngine engine; auto db=dir/"index.sqlite"; assert(engine.openIndex(db.string()));
    msf::FileState fs; fs.path=(dir/"existing.jpg").string(); fs.size=1; fs.modified=1; fs.fingerprint=0x123456789abcdef0ULL; fs.kind=1; assert(engine.upsertFingerprint(fs.path,fs.fingerprint,fs.kind,fs.size,fs.modified)); auto matches=engine.compareFingerprint(fs.fingerprint,1,100.0,fs.path); assert(matches.empty());
    auto matches2=engine.compareFingerprint(fs.fingerprint,1,90.0); assert(matches2.size()==1); assert(matches2[0].rightPath==fs.path);
    for(int i=0;i<1000;++i){ auto q=dir/("bulk"+std::to_string(i)+".jpg"); assert(engine.upsertFingerprint(q.string(),0x8000000000000000ULL ^ static_cast<std::uint64_t>(i),1,10+i,i)); }
    auto bulkMatches=engine.compareFingerprint(0x8000000000000000ULL,1,98.0);
    assert(!bulkMatches.empty());
    for(const auto& m:bulkMatches) assert(m.percent>=98.0);
    auto updated=dir/"updated.jpg"; assert(engine.upsertFingerprint(updated.string(),0x0f0f0f0f0f0f0f0fULL,1,42,77)); auto um=engine.compareFingerprint(0x0f0f0f0f0f0f0f0fULL,1,100.0); assert(um.size()==1&&um[0].rightPath==updated.string()); assert(engine.removePath(updated.string())); assert(engine.compareFingerprint(0x0f0f0f0f0f0f0f0fULL,1,100.0).empty());auto f=dir/"a.txt";{std::ofstream o(f);o<<"x";} msf::StableFileDetector d;assert(!d.isStable(f.string(),1));std::this_thread::sleep_for(std::chrono::milliseconds(1100));assert(d.isStable(f.string(),1));assert(!d.isStable(f.string(),1));std::filesystem::remove_all(dir);std::cout<<"monitor test ok\n";
}
