#include "media_pipeline.h"
#include <filesystem>
#include <fstream>
#include <iostream>
int main(){auto p1=std::filesystem::temp_directory_path()/"msf_batch_a.pgm";auto p2=std::filesystem::temp_directory_path()/"msf_batch_b.pgm";for(auto p:{p1,p2}){std::ofstream f(p,std::ios::binary);f<<"P5\n32 32\n255\n";for(int i=0;i<1024;i++)f.put((char)((i%32)<16?20:220));}msf::MediaPipeline p;auto r=p.imageBatch({p1.string(),p2.string()},true,2);if(r.size()!=2||!r[0].ok||!r[1].ok||r[0].fingerprint!=r[1].fingerprint)return 1; if(r[0].usedGpu && r[0].gpuFallback)return 2;std::filesystem::remove(p1);std::filesystem::remove(p2);std::cout<<"image_batch=ok\n";return 0;}