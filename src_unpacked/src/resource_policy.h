#pragma once
#include <algorithm>
namespace msf {
enum class ResourceMode { Maximum=1, Balanced=2, Gaming=3, Light=3, Custom=4 };
struct ResourcePolicy { ResourceMode mode=ResourceMode::Balanced; int cpuPercent=55; int gpuPercent=60; bool gpuEnabled=true; };
ResourcePolicy make_policy(ResourceMode mode,int customCpu=55,int customGpu=60);
int recommended_worker_count(const ResourcePolicy&, int hardwareThreads);
std::size_t recommended_gpu_batch_size(const ResourcePolicy&, std::size_t base=64);
}
