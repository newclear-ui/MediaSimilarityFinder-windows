#include "incremental_index.h"
namespace msf {
void IncrementalIndex::load(const std::vector<FileState>& states){
 old_.clear(); for(const auto&s:states) old_[s.path]=s;
}
std::vector<Change> IncrementalIndex::diff(const std::vector<FileState>& cur) const{
 std::vector<Change> out; std::unordered_map<std::string,bool> seen;
 for(const auto&s:cur){
  seen[s.path]=true; auto it=old_.find(s.path);
  if(it==old_.end()) out.push_back({ChangeKind::Added,s});
  else if(it->second.size!=s.size || it->second.modified!=s.modified)
       out.push_back({ChangeKind::Modified,s});
  else out.push_back({ChangeKind::Unchanged,s});
 }
 for(const auto&kv:old_) if(!seen.count(kv.first))
   out.push_back({ChangeKind::Removed,kv.second});
 return out;
}
}