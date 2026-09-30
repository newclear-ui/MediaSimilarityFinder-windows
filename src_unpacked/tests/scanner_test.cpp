#include "scanner.h"
#include "scan_pipeline.h"  // MediaKind
#include <filesystem>
#include <fstream>
#include <iostream>
int main(){
  auto d=std::filesystem::temp_directory_path()/"msf_scan_test";std::filesystem::remove_all(d);std::filesystem::create_directories(d/"sub");
  std::ofstream(d/"a.jpg")<<"image";std::ofstream(d/"sub"/"b.mp4")<<"video";std::ofstream(d/"ignore.txt")<<"no";
  // Uppercase and mixed-case extensions: isVideoPath() lowercases before comparing,
  // so these must classify the same way rather than becoming Unknown.
  std::ofstream(d/"c.PNG")<<"image";std::ofstream(d/"sub"/"d.MP4")<<"video";
  msf::Scanner s;auto v=s.scan(d.string());
  if(v.size()!=4)return 1;
  for(auto&x:v)if(x.quickHash.empty()||x.path.empty())return 2;

  // Media kind must be the kind the walk already decided, not Unknown. The
  // benchmark --media scope, and anything else reading FileState from a scan,
  // cannot tell images from videos without this.
  int images=0,videos=0,unknown=0;
  for(const auto&x:v){
    if(x.kind==(int)msf::MediaKind::Image)++images;
    else if(x.kind==(int)msf::MediaKind::Video)++videos;
    else ++unknown;
  }
  if(unknown!=0)return 3;
  if(images!=2)return 4;   // a.jpg, c.PNG
  if(videos!=2)return 5;   // b.mp4, d.MP4
  for(const auto&x:v){
    const bool isVid=msf::Scanner::isVideoPath(x.path);
    if(isVid&&x.kind!=(int)msf::MediaKind::Video)return 6;
    if(!isVid&&x.kind!=(int)msf::MediaKind::Image)return 7;
  }

  // A non-media file is not walked at all, so its kind is never invented.
  if(msf::Scanner::isMediaPath(d/"ignore.txt"))return 8;
  if(msf::Scanner::isMediaPath(d/"a.jpg")!=true)return 9;
  if(msf::Scanner::isVideoPath(d/"a.jpg")!=false)return 10;
  if(msf::Scanner::isVideoPath(d/"b.mp4")!=true)return 11;

  std::cout<<"scanner=ok\nmedia_files="<<v.size()<<"\nimages="<<images<<"\nvideos="<<videos<<"\n";
  std::filesystem::remove_all(d);return 0;}
