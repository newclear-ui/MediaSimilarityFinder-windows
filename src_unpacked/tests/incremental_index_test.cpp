#include "incremental_index.h"
#include <iostream>
int main(){
 msf::IncrementalIndex db;
 db.load({{"same.jpg",100,10,1},{"old.jpg",200,20,2}});
 auto d=db.diff({{"same.jpg",100,10,1},{"mod.jpg",300,30,3},{"old.jpg",201,20,4}});
 int u=0,a=0,m=0,r=0;
 for(auto&x:d){u+=x.kind==msf::ChangeKind::Unchanged;a+=x.kind==msf::ChangeKind::Added;m+=x.kind==msf::ChangeKind::Modified;r+=x.kind==msf::ChangeKind::Removed;}
 if(u!=1||a!=1||m!=1||r!=0)return 1;
 auto d2=db.diff({{"same.jpg",100,10,1}});
 int rem=0;for(auto&x:d2)rem+=x.kind==msf::ChangeKind::Removed;
 if(rem!=1)return 2;
 std::cout<<"incremental_index=ok\n";return 0;
}