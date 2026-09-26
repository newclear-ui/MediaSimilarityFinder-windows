#pragma once
#include <cstdint>
#include <string>
#include <cstddef>
#include <mutex>

namespace msf {
// Node A (0.9.4 line): GPU is the generic accelerator term; CUDA is one
// concrete backend, not the generic name. Future backends (Vulkan,
// HIP/ROCm, Level Zero) register here without touching the engine — until
// then only CUDA and CPU-fallback resolve. Unimplemented backends are never
// reported as available.
enum class GpuBackendKind { Auto, Cuda, Cpu };
inline const char* gpuBackendKindName(GpuBackendKind k) {
  switch (k) {
    case GpuBackendKind::Cuda: return "CUDA";
    case GpuBackendKind::Cpu: return "CPU";
    default: return "AUTO";
  }
}
struct GpuInfo {
    bool available=false;
    std::string name;
    int major=0, minor=0;
    std::size_t globalMemoryBytes=0;
    std::size_t freeMemoryBytes=0;
};

class GpuBackend {
public:
    GpuBackend();
    ~GpuBackend();
    GpuBackend(GpuBackend&&) noexcept;
    GpuBackend& operator=(GpuBackend&&) noexcept;
    GpuBackend(const GpuBackend&) = delete;
    GpuBackend& operator=(const GpuBackend&) = delete;
    GpuInfo detect() const;
    bool available() const;
    // Backend abstraction: Auto resolves to CUDA when a device is present,
    // otherwise CPU fallback. Explicit Cuda/Cpu pins the resolution for
    // diagnostics (MSF_GPU_BACKEND=CUDA); no other backend exists yet.
    void setKind(GpuBackendKind k) { kind_ = k; }
    GpuBackendKind kind() const { return kind_; }
    std::string backendName() const;
    // B1 baseline capacity proxy: CUDA SM count (relative units; only the
    // CPU/GPU ratio feeds shares). 0 when no device. B2/C replace this with
    // measured throughput and calibration.
    double computeUnits() const;
    std::size_t recommendedBatchSize(std::size_t requested=256) const;
    bool hashBatch(const std::uint8_t* grayscale,std::uint64_t count,
                   std::uint64_t* hashes) const;
    bool ssimBatch(const std::uint8_t* a,const std::uint8_t* b,
                   std::uint64_t count,double* scores) const;
private:
    struct Impl;
    Impl* impl_=nullptr;
    GpuBackendKind kind_=GpuBackendKind::Auto;
    mutable std::mutex hashMutex_;
};
}
