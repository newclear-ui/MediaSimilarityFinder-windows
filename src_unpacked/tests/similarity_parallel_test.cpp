#include "similarity_engine.h"
#include <iostream>
int main(){
 const std::uint64_t a[4]={0xFFFFULL,0x12345678ULL,0xAAAAAAAAULL,0x1111ULL};
 const std::uint64_t b[5]={0ULL,0x12345678ULL,0xBBBBBBBBULL,0xFFFFULL,0x2222ULL};
 auto x=msf::best_match(a,4,b,5); auto y=msf::best_match_parallel(a,4,b,5,{},2);
 if(x.percent!=y.percent||x.left!=y.left||x.right!=y.right||x.percent<99.9)return 1;
 std::cout<<"similarity_parallel=ok\n";return 0;
}
