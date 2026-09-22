#include "fingerprint.h"
#include "similarity.h"
#include "sampling.h"
#include <iostream>
#include <vector>
int main() {
    std::vector<unsigned char> a(4096), b(4096);
    for(size_t i=0;i<a.size();++i) a[i]=b[i]=static_cast<unsigned char>(i);
    auto ha=msf::average_hash(a), hb=msf::average_hash(b);
    if(msf::hash_similarity(ha,hb)<99.9) return 1;
    if(msf::sampling_interval(10)!=1) return 2;
    if(msf::sampling_interval(60)!=2) return 3;
    if(msf::sampling_interval(300)!=5) return 4;
    if(msf::sampling_interval(301)!=10) return 5;
    if(msf::sampling_interval(1801)!=15) return 6;
    std::cout<<"fingerprint=ok\nsimilarity=ok\nsampling=ok\n";
    return 0;
}
