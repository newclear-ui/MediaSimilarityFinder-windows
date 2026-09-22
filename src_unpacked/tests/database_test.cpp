#include "database.h"
#include "incremental_scanner.h"
#include <iostream>
#include <filesystem>
int main(){
    auto p=(std::filesystem::temp_directory_path()/"msf_db_test.tsv").string();
    std::filesystem::remove(p);
    msf::Database db; if(!db.open(p)||!db.initialize()) return 1;
    msf::FileState a{"a.jpg",100,10,"x"}, b{"b.mp4",200,20,"y"};
    if(!db.upsert(a)||!db.upsert(b)) return 2;
    auto old=db.all();
    std::vector<msf::FileState> cur{
        {"a.jpg",100,10,"x"},
        {"b.mp4",250,21,"z"},
        {"c.png",300,30,"q"}};
    msf::IncrementalScanner s; auto d=s.classify(cur,old);
    if(d.unchanged.size()!=1||d.modified.size()!=1||d.added.size()!=1||d.deleted.size()!=0) return 3;
    if(!db.remove("a.jpg")) return 6;
    if(!db.beginTransaction()) return 7;
    msf::FileState transient{"transient.jpg",10,40,"t",123,1,0};
    if(!db.upsert(transient)) return 8;
    if(!db.rollbackTransaction()) return 9;
    for(const auto& x : db.all()) if(x.path == "transient.jpg") return 10;
    if(!db.beginTransaction()) return 11;
    if(!db.upsert(transient) || !db.commitTransaction()) return 12;
    bool committed=false; for(const auto& x : db.all()) if(x.path == "transient.jpg") committed=true;
    if(!committed) return 13;
    std::cout<<"database=ok\nincremental=ok\ntransaction=ok\n";
    std::filesystem::remove(p);
    return 0;
}
