#include "media_analyzer.h"
#include "fingerprint.h"
#include "video_fingerprint.h"
#include "sampling.h"
namespace msf {
AnalysisResult analyze_image_bytes(const std::vector<std::uint8_t>& b){return {average_hash(b),b.empty()?std::size_t(0):std::size_t(1),0};}
AnalysisResult analyze_video_duration(const std::string& path,double d){VideoFingerprint vf; VideoFingerprintEngine e; if(!path.empty() && e.build(path,vf)) return {vf.hashes.empty()?0:vf.hashes.front(),vf.hashes.size(),vf.timestamps.size()>1?(vf.timestamps[1]-vf.timestamps[0]):sampling_interval(d)}; auto p=make_sample_plan(d); return {0,p.timestamps.size(),p.intervalSec};}
}