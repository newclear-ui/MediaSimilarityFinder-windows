#include "media_pipeline.h"
#include <filesystem>
#include <fstream>
#include <iostream>
int main(){auto p=std::filesystem::temp_directory_path()/"msf_img_test.pgm";std::ofstream f(p,std::ios::binary);f<<"P5\n64 64\n255\n";for(int i=0;i<4096;i++)f.put((char)((i%64)<32?20:220));f.close();msf::MediaPipeline x;std::uint64_t fp=0;if(!x.image(p.string(),fp)||fp==0)return 1;std::cout<<"image_pipeline=ok\n";std::filesystem::remove(p);return 0;}