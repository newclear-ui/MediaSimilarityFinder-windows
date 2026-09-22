#pragma once
#include <cstdint>
#include <string>
#include <cstddef>

namespace msf {
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
    std::size_t recommendedBatchSize(std::size_t requested=256) const;
    bool hashBatch(const std::uint8_t* grayscale,std::uint64_t count,
                   std::uint64_t* hashes) const;
private:
    struct Impl;
    Impl* impl_=nullptr;
};
}
