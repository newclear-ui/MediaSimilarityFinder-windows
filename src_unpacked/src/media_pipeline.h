#pragma once
#include "image_decoder.h"
#include "fingerprint.h"
#include "gpu_backend.h"
#include "crop_fingerprint.h"
#include "benchmark.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
namespace msf {
struct ImageFingerprintResult { std::string path; std::uint64_t fingerprint=0, mirrorFingerprint=0; bool ok=false; bool usedGpu=false; bool gpuFallback=false; CropFingerprints crops{}; ColorImage colorThumb; bool hasColorThumb=false; };
class MediaPipeline {
public:
 bool image(const std::string& path,std::uint64_t& fingerprint, std::uint64_t* mirrorFingerprint=nullptr) const;
 std::vector<ImageFingerprintResult> imageBatch(const std::vector<std::string>& paths,bool preferGpu=true,std::size_t gpuBatchSize=256,std::atomic<bool>* activity=nullptr,BenchmarkRecorder* bench=nullptr) const;
  bool gpuAvailable() const { return gpu_.available(); }
  std::string gpuBackendName() const { return gpu_.backendName(); }
  double gpuComputeUnits() const { return gpu_.computeUnits(); }
private:
 mutable GpuBackend gpu_;
};
}
