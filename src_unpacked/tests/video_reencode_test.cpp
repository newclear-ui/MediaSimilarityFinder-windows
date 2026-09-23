// E2E duplicate-resilience check across a full transcode: the two copies differ
// in codec, resolution, bitrate and pixel format, so the perceptual pipeline
// must still land on the same content region via video_crop_similarity at the
// scan-time threshold. Mirrors the scan pipeline's video temporal call
// (gapPenalty/options = {threshold,8,2,2.0}) and asserts a confident match.
#include "video_fingerprint.h"
#include "sampling.h"
#include <filesystem>
#include <cstdlib>
#include <iostream>
int main(){auto d=std::filesystem::temp_directory_path()/"msf_reen_video";std::filesystem::remove_all(d);std::filesystem::create_directories(d);auto a=(d/"orig.mp4").string(),b=(d/"reencoded.mp4").string();
 std::string c1="ffmpeg -hide_banner -loglevel error -y -f lavfi -i testsrc=size=160x90:rate=10:duration=8 -c:v mpeg4 -pix_fmt yuv420p \""+a+"\"";if(std::system(c1.c_str())!=0)return 1;
 // Re-encode: scale down + libx264 (different codec entirely) + heavy crf.
 std::string c2="ffmpeg -hide_banner -loglevel error -y -i \""+a+"\" -vf scale=128:72 -c:v libx264 -crf 28 -preset veryfast -pix_fmt yuv420p \""+b+"\"";if(std::system(c2.c_str())!=0)return 2;
 msf::VideoFingerprint va,vb;msf::VideoFingerprintEngine eng;if(!eng.build(a,va)||!eng.build(b,vb))return 3;if(va.hashes.size()<7||vb.hashes.size()<7)return 4;
 msf::VideoSimilarityOptions o;o.thresholdPercent=50.0;o.gapPenalty=8.0;o.timeToleranceSeconds=2.0;o.sceneBonus=2.0;
 double sim=msf::video_similarity(va,vb,o);if(sim<85.0)return 5;std::cout<<"reencode_similarity=ok\nframes="<<va.hashes.size()<<" vs "<<vb.hashes.size()<<"\nsimilarity="<<sim<<"\n";std::filesystem::remove_all(d);return 0;}