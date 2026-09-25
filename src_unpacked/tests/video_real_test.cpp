#include "video_fingerprint.h"
#include "gpu_backend.h"
#include "sampling.h"
#include <filesystem>
#include <cstdlib>
#include <iostream>
 int main(){auto d=std::filesystem::temp_directory_path()/"msf_real_video";std::filesystem::remove_all(d);std::filesystem::create_directories(d);auto a=(d/"a.mp4").string(),b=(d/"b.mp4").string();
  std::string cmd="ffmpeg -hide_banner -loglevel error -y -f lavfi -i testsrc=size=128x96:rate=10:duration=8 -c:v mpeg4 -pix_fmt yuv420p \""+a+"\"";if(std::system(cmd.c_str())!=0)return 1;std::filesystem::copy_file(a,b);msf::GpuBackend gpu;const bool gpuAvailable=gpu.available();msf::VideoFingerprint va,vb;msf::VideoBuildStats stats;if(!msf::VideoFingerprintEngine().build(a,va,gpuAvailable?&gpu:nullptr,nullptr,&stats)||!msf::VideoFingerprintEngine().build(b,vb,gpuAvailable?&gpu:nullptr,nullptr,nullptr))return 2;if(gpuAvailable&&!stats.gpuUsed)return 5;if(va.hashes.size()<7||vb.hashes.size()<7)return 3;double sim=msf::video_similarity(va,vb);if(sim<99.9)return 4;std::cout<<"real_video_decode=ok\nframes="<<va.hashes.size()<<"\nsimilarity="<<sim<<"\ninterval="<<msf::sampling_interval(8)<<"\ngpu_video_hash="<<(stats.gpuUsed?"yes":"no")<<"\ngpu_hash_ms="<<stats.gpuMs<<"\n";std::filesystem::remove_all(d);return 0;}
