#include "database.h"
#include <filesystem>
#include <iostream>
#include <algorithm>
int main(){
    const auto p=(std::filesystem::temp_directory_path()/"msf_match_store_test.sqlite").string();
    std::filesystem::remove(p);
    msf::Database db; if(!db.open(p)||!db.initialize()) return 1;
    // Round-trip on a fresh database.
    std::vector<msf::StoredMatch> in{
        {"b.jpg","a.jpg",100.0},   // deliberately unordered: pairKey must sort
        {"d.png","c.png",87.5},
    };
    if(!db.saveMatches(in)) return 2;
    auto out=db.loadMatches();
    if(out.size()!=2) return 3;
    auto find=[&](const char* l,const char* r)->const msf::StoredMatch*{
        for(const auto& m:out) if(m.left==l&&m.right==r) return &m;
        return nullptr;
    };
    const auto* p1=find("a.jpg","b.jpg");
    if(!p1||p1->percent!=100.0) return 4;
    const auto* p2=find("c.png","d.png");
    if(!p2||p2->percent!=87.5) return 5;
    // Replacement drops pairs that are no longer present.
    std::vector<msf::StoredMatch> replace{{"a.jpg","b.jpg",99.0}};
    if(!db.saveMatches(replace)) return 6;
    out=db.loadMatches();
    if(out.size()!=1) return 7;
    if(!find("a.jpg","b.jpg")||find("c.png","d.png")!=nullptr) return 8;
    if(out[0].percent!=99.0) return 9;
    // Empty set clears the table.
    if(!db.saveMatches({})) return 10;
    if(!db.loadMatches().empty()) return 11;
    std::cout<<"match_store=ok\n";
    db.close();
    std::filesystem::remove(p);
    return 0;
}