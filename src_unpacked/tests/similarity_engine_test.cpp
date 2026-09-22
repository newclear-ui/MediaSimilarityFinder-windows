#include "similarity_engine.h"
#include "similarity.h"
#include <cmath>
#include <iostream>
int main(){
 const std::uint64_t a[]={0xFFFFULL,0ULL},b[]={0xFFFFULL,0xFF00ULL};
 auto r=msf::best_match(a,2,b,2);
 if(std::abs(r.percent-100.0)>1e-9||r.left!=0||r.right!=0)return 1;
 if(std::abs(msf::hash_similarity(0,~0ULL))>1e-9)return 2;
 std::cout<<"similarity_engine=ok\n"; return 0;
}