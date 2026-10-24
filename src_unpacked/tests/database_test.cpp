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
    msf::FileState replaced{"a.jpg",100,10,"changed"};
    if(db.containsUnchanged(replaced)) return 4;
    auto sameMeta=cur; sameMeta[0].quickHash="changed";
    auto freshness=s.classify(sameMeta,old);
    if(freshness.modified.size()!=2||freshness.unchanged.size()!=0) return 5;
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
    if(db.samplingGeneration()!="0") return 14;
    if(!db.setSamplingGeneration("2")) return 15;
    if(db.samplingGeneration()!="2") return 16;
    // 0.9.4.81 B-slice resume frontier round-trip (empty default, store, clear).
    if(!db.analyzeFrontier().empty()) return 17;
    if(!db.setAnalyzeFrontier("eng|8|3|abcd#5")) return 18;
    if(db.analyzeFrontier()!="eng|8|3|abcd#5") return 19;
    if(!db.setAnalyzeFrontier("")) return 20;
    if(!db.analyzeFrontier().empty()) return 21;
    // 0.9.4.105 (F): forget() drops the files row, match pairs touching the
    // path on either side, and the stored thumbnail — in one transaction,
    // leaving unrelated rows intact.
    if(!db.upsert(msf::FileState{"gone.jpg",100,10,"x"})) return 22;
    if(!db.upsert(msf::FileState{"keep.png",300,30,"q"})) return 23;
    {
      std::vector<msf::StoredMatch> ms{
          {"gone.jpg","keep.png",91.0},
          {"keep.png","other.png",88.0}};
      if(!db.saveMatches(ms)) return 24;
    }
    {
      const std::vector<unsigned char> jpeg{0xFF,0xD8,0xFF,0xD9};
      if(!db.putThumb("gone.jpg",10,100,jpeg)) return 25;
    }
    if(!db.forget({"gone.jpg"})) return 26;
    for(const auto& x : db.all()) if(x.path=="gone.jpg") return 27;
    {
      const auto ms=db.loadMatches();
      for(const auto& m : ms) if(m.left=="gone.jpg"||m.right=="gone.jpg") return 28;
      bool keepPair=false; for(const auto& m : ms) if(m.left=="keep.png"||m.right=="keep.png") keepPair=true;
      if(!keepPair) return 29;
    }
    if(!db.forget({})) return 30; // empty input: no-op success
    // 0.9.4.152: saveMatches() inside an open caller transaction uses a
    // SAVEPOINT (mid-scan checkpoints run inside the scan's transaction; a
    // nested BEGIN would fail and the persist would be silently lost).
    if(!db.saveMatches({{"n1.jpg","n2.png",90.0}})) return 31;
    if(!db.beginTransaction()) return 32;
    if(!db.saveMatches({{"s1.jpg","s2.png",80.0}})) return 33; // nested: must succeed
    {
      bool seen = false;
      for (const auto& m : db.loadMatches()) if (m.left == "s1.jpg") seen = true;
      if (!seen) return 34; // in-tx visibility: the savepoint wrote into the tx
    }
    if(!db.rollbackTransaction()) return 35;
    for (const auto& m : db.loadMatches()) if (m.left == "s1.jpg") return 36;
    {
      bool kept = false;
      for (const auto& m : db.loadMatches()) if (m.left == "n1.jpg") kept = true;
      if (!kept) return 37; // pre-tx data intact after the rollback
    }
    std::cout<<"database=ok\nincremental=ok\ntransaction=ok\nfrontier=ok\nforget=ok\nsavepoint=ok\n";
    db.close(); // Windows cannot remove an open database file; close first.
    std::filesystem::remove(p);
    return 0;
}
