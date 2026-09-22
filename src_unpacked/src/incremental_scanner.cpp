#include "incremental_scanner.h"
#include <unordered_map>
namespace msf {
ChangeSet IncrementalScanner::classify(const std::vector<FileState>& cur,
                                        const std::vector<FileState>& prev) const{
    ChangeSet c;
    std::unordered_map<std::string,FileState> old;
    for(auto&x:prev)old[x.path]=x;
    std::unordered_map<std::string,bool> seen;
    for(auto&x:cur){
        seen[x.path]=true;
        auto it=old.find(x.path);
        if(it==old.end()) c.added.push_back(x);
        else if(it->second.size==x.size && it->second.modified==x.modified)
            c.unchanged.push_back(x);
        else c.modified.push_back(x);
    }
    for(auto&x:prev) if(!seen[x.path]) c.deleted.push_back(x);
    return c;
}
}
