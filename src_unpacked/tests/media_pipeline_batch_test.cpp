#include "media_pipeline.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(){
  auto p1=std::filesystem::temp_directory_path()/"msf_batch_a.pgm";
  auto p2=std::filesystem::temp_directory_path()/"msf_batch_b.pgm";
  for(auto p:{p1,p2}){
    std::ofstream f(p,std::ios::binary);
    f<<"P5\n32 32\n255\n";
    for(int i=0;i<1024;i++)f.put((char)((i%32)<16?20:220));
  }

  msf::MediaPipeline p;
  msf::TelemetryRecorder bench;
  msf::TelemetryConfig cfg;
  cfg.root=std::filesystem::temp_directory_path().string();
  cfg.build="0.9.4.15"; cfg.engine="1.5.0"; cfg.db="1.0.3";
  bench.start(cfg);

  std::atomic<bool> act{true};
  auto r=p.imageBatch({p1.string(),p2.string()},true,2,&act,&bench);
  if(r.size()!=2||!r[0].ok||!r[1].ok||r[0].fingerprint!=r[1].fingerprint)return 1;
  if(r[0].usedGpu && r[0].gpuFallback)return 2;
  if(act.load())return 3;

  bench.finalize(true,2,2,0,0,0,0,0.0,0,0);
  const auto js=bench.toJson();
  if(js.find("\"batchCount\":1")==std::string::npos)return 4;
  if(js.find("\"batchItems\":2")==std::string::npos)return 5;
  if(js.find("\"batchState\":\"measured\"")==std::string::npos)return 6;
  if(js.find("\"packMs\":") == std::string::npos)return 7;
  if(js.find("\"cpuHashMs\":") == std::string::npos)return 8;

  std::atomic<bool> act2{true};
  auto r2=p.imageBatch({},true,2,&act2);
  if(!r2.empty()||act2.load())return 9;

  std::filesystem::remove(p1);std::filesystem::remove(p2);
  std::cout<<"image_batch=d1a_observability_ok\n";
  return 0;
}
