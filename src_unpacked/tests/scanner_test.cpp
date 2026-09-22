#include "scanner.h"
#include <filesystem>
#include <fstream>
#include <iostream>
int main(){auto d=std::filesystem::temp_directory_path()/"msf_scan_test";std::filesystem::remove_all(d);std::filesystem::create_directories(d/"sub");std::ofstream(d/"a.jpg")<<"image";std::ofstream(d/"sub"/"b.mp4")<<"video";std::ofstream(d/"ignore.txt")<<"no";msf::Scanner s;auto v=s.scan(d.string());if(v.size()!=2)return 1;for(auto&x:v)if(x.quickHash.empty()||x.path.empty())return 2;std::cout<<"scanner=ok\nmedia_files="<<v.size()<<"\n";std::filesystem::remove_all(d);return 0;}
