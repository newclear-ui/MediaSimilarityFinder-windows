#include "video_fingerprint.h"
#include <iostream>
int main(){
 msf::VideoFingerprint longv, shortv, shifted; longv.timestamps={0,1,2,3,4,5,6,7,8,9}; longv.hashes={10,20,30,40,50,60,70,80,90,100};
 shortv.timestamps={0,1,2,3}; shortv.hashes={40,50,60,70};
 shifted.timestamps={10,11,12,13}; shifted.hashes={40,50,60,70};
 auto p=msf::video_similarity(longv,shortv,{50,8,2}); auto q=msf::video_similarity(longv,shifted,{50,8,2});
 if(p<95.0||q<95.0)return 1;
 std::cout<<"partial_alignment="<<p<<"\nshifted_alignment="<<q<<"\n";return 0;
}
