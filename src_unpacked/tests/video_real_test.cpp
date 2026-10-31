#include "video_fingerprint.h"
#include "gpu_backend.h"
#include "sampling.h"
#include <filesystem>
#include <cstdlib>
#include <iostream>
 int main(){auto d=std::filesystem::temp_directory_path()/"msf_real_video";std::filesystem::remove_all(d);std::filesystem::create_directories(d);auto a=(d/"a.mp4").string(),b=(d/"b.mp4").string();
 std::string cmd="ffmpeg -hide_banner -loglevel error -y -f lavfi -i testsrc=size=128x96:rate=10:duration=8 -c:v mpeg4 -pix_fmt yuv420p \""+a+"\"";if(std::system(cmd.c_str())!=0)return 1;std::filesystem::copy_file(a,b);msf::GpuBackend gpu;const bool gpuAvailable=gpu.available();msf::VideoFingerprintEngine eng;msf::VideoFingerprint va,vb;msf::VideoBuildStats stats;if(!eng.build(a,va,gpuAvailable?&gpu:nullptr,nullptr,&stats)||!eng.build(b,vb,gpuAvailable?&gpu:nullptr,nullptr,nullptr))return 2;if(gpuAvailable&&!stats.gpuUsed)return 5;
 // 0.9.4.156 (V workstream): Auto short-form 5 s on the 8 s clip -> 3 probe
 // timestamps ([0,5,8]); the 8.0 target has no decoded frame at/after it, so 2
 // kept. The sweep still decodes the whole range (decoded 80).
 if(va.hashes.size()!=2||vb.hashes.size()!=2)return 3;
 if(stats.decodedFrames!=80||stats.sampledFrames!=3)return 3;
 // Custom 1 s interval on the same engine: the stored Auto row must NOT match
 // (policy-separated cache), so this re-decodes; 9 probes trim to G1 target 5.
 { msf::VideoPolicy custom; custom.mode=msf::VideoMode::Custom; custom.shortIntervalSec=1.0;
   eng.setVideoPolicy(custom);
   msf::VideoFingerprint vc; msf::VideoBuildStats st2;
   if(!eng.build(a,vc,nullptr,nullptr,&st2))return 6;
   if(vc.hashes.size()!=5||st2.decodedFrames!=80||st2.sampledFrames!=9)return 6; }
 // Same custom policy with a G1 budget of 2: trim 9 probes down to 2.
 { msf::VideoPolicy custom2; custom2.mode=msf::VideoMode::Custom; custom2.shortIntervalSec=1.0; custom2.keptTarget[0]=2;
   eng.setVideoPolicy(custom2);
   msf::VideoFingerprint vd; msf::VideoBuildStats st3;
   if(!eng.build(a,vd,nullptr,nullptr,&st3))return 7;
   if(vd.hashes.size()!=2||st3.decodedFrames!=80)return 7; }
 // Back to Auto on the same engine: the default row must still hit (decoded 0),
 // proving custom rows never polluted the default entry.
 { eng.setVideoPolicy(msf::VideoPolicy());
   msf::VideoFingerprint ve; msf::VideoBuildStats st4;
   if(!eng.build(a,ve,nullptr,nullptr,&st4))return 8;
   if(st4.decodedFrames!=0||ve.hashes.size()!=2)return 8; }
 double sim=msf::video_similarity(va,vb);if(sim<99.9)return 4;std::cout<<"real_video_decode=ok\nframes="<<va.hashes.size()<<" decoded="<<stats.decodedFrames<<" sampled="<<stats.sampledFrames<<"\nsimilarity="<<sim<<"\ninterval="<<msf::sampling_interval(8)<<"\ngpu_video_hash="<<(stats.gpuUsed?"yes":"no")<<"\ngpu_hash_ms="<<stats.gpuMs<<"\n";std::filesystem::remove_all(d);return 0;}

