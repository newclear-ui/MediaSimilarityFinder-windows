#pragma once
#include <algorithm>
namespace msf {
enum class ResourceMode { Maximum=1, High=2, Balanced=3, Gaming=4, Light=4, Custom=5 };
// Node A: cpuPercent keeps its preset semantics (policy upper bound; Custom
// is the only user-editable CPU limit). gpuPercent is a deprecated internal
// cap kept for compatibility — the UI no longer exposes it and Node B
// replaces it with the Adaptive Scheduler (GPU ON = AUTO). CPU fallback and
// preset CPU values are unchanged.
struct ResourcePolicy { ResourceMode mode=ResourceMode::Balanced; int cpuPercent=55; int gpuPercent=60; bool gpuEnabled=true; };
ResourcePolicy make_policy(ResourceMode mode,int customCpu=55,int customGpu=60);
int recommended_worker_count(const ResourcePolicy&, int hardwareThreads);
std::size_t recommended_gpu_batch_size(const ResourcePolicy&, std::size_t base=64);
}
