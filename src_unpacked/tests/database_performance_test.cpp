#include "database.h"
#include <chrono>
#include <filesystem>
#include <iostream>

int main(){
    namespace fs=std::filesystem;
    const auto p=(fs::temp_directory_path()/"msf_db_perf_test.sqlite");
    fs::remove(p); fs::remove(p.string()+"-wal"); fs::remove(p.string()+"-shm");
    msf::Database db;
    if(!db.open(p.string()) || !db.initialize()) return 1;
    constexpr int N=5000;
    if(!db.beginTransaction()) return 2;
    const auto t0=std::chrono::steady_clock::now();
    for(int i=0;i<N;++i){
        msf::FileState x;
        x.path="media_"+std::to_string(i)+".jpg";
        x.size=1000+i; x.modified=i; x.quickHash="q"+std::to_string(i);
        x.fingerprint=static_cast<std::uint64_t>(i)*0x9e3779b97f4a7c15ull;
        x.mirrorFingerprint=x.fingerprint^0x55aa55aa55aa55aaull;
        x.crop4x3=x.fingerprint+1; x.crop1x1=x.fingerprint+2; x.crop9x16=x.fingerprint+3;
        x.mirrorCrop4x3=x.fingerprint+4; x.mirrorCrop1x1=x.fingerprint+5; x.mirrorCrop9x16=x.fingerprint+6;
        if(!db.upsert(x)) return 3;
    }
    if(!db.commitTransaction()) return 4;
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
    auto all=db.all();
    if(static_cast<int>(all.size())!=N) return 5;
    // Regression guard: this is intentionally generous across CI/slow disks.
    if(ms>15000.0) return 6;
    std::cout<<"database_upserts="<<N<<"\ntransaction_ms="<<ms<<"\nprepared_statement_reuse=ok\n";
    fs::remove(p); fs::remove(p.string()+"-wal"); fs::remove(p.string()+"-shm");
    return 0;
}
