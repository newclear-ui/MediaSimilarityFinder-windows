#pragma once
#include "media_pipeline.h"
#include "media_search_engine.h"
#include "video_fingerprint.h"
#include "resource_policy.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <memory>
#include <condition_variable>

namespace msf {
struct MonitorRoot { std::string path; bool watch=true; bool compare=true; };
struct MonitorConfig {
    std::vector<std::string> watchRoots;
    std::vector<std::string> compareRoots;
    std::string applicationDirectory;
    double thresholdPercent=90.0;
    int pollSeconds=2; // fallback/non-Windows polling interval
    int stableSeconds=3;
    unsigned maxRetries=8;
    bool gpuEnabled=true;
};
enum class LoadState { Idle, Light, Busy, Heavy, Critical };
struct SystemLoad { double cpuPercent=0; double memoryPercent=0; double gpuPercent=-1; bool critical=false; LoadState state=LoadState::Idle; };
struct MonitorMatch { std::string newPath, existingPath; double percent=0; };
struct MonitorEvent { enum class Type { Detected, Deferred, Match, Error, Started, Stopped }; Type type=Type::Detected; std::string path, detail; std::vector<MonitorMatch> matches; };
struct MonitorStatus {
    bool running=false;
    LoadState loadState=LoadState::Idle;
    double cpuPercent=0;
    double memoryPercent=0;
    double gpuPercent=-1;
    std::size_t pending=0;
    std::size_t deferred=0;
    std::size_t analyzed=0;
    std::size_t matches=0;
    std::size_t errors=0;
    bool paused=false;
    double lastAnalysisMs=0;
    double averageAnalysisMs=0;
    std::string lastAnalysisKind;
    std::string lastErrorPath;
    std::string lastError;
};

class SystemLoadMonitor {
public:
    SystemLoad sample();
    bool allowAnalysis(const ResourcePolicy& policy, const SystemLoad& load) const;
    static LoadState classify(const SystemLoad& load);
private:
#ifdef _WIN32
    ULARGE_INTEGER prevIdle_{}, prevKernel_{}, prevUser_{};
    bool haveCpuSample_=false;
    std::chrono::steady_clock::time_point lastGpuSample_{};
    double cachedGpuPercent_=-1.0;
#endif
};

class StableFileDetector {
public:
    bool isStable(const std::string& path, int stableSeconds);
private:
    struct State { std::uintmax_t size=0; std::filesystem::file_time_type modified{}; std::chrono::steady_clock::time_point firstSeen{}; };
    std::mutex mutex_; std::unordered_map<std::string, State> states_;
};

class MediaMonitor {
public:
    using Callback=std::function<void(const MonitorEvent&)>;
    MediaMonitor(); ~MediaMonitor();
    MediaMonitor(const MediaMonitor&)=delete; MediaMonitor& operator=(const MediaMonitor&)=delete;
    bool start(const MonitorConfig& config, const ResourcePolicy& policy, Callback callback);
    void stop(); bool running() const { return running_.load(); }
    void setPolicy(const ResourcePolicy& policy);
    void setPaused(bool paused);
    bool paused() const;
    MonitorStatus status() const;
private:
    void loop();
    // Named emitEvent (not emit): Qt defines `emit` as an empty macro, so a
    // member named `emit` breaks any translation unit that includes Qt headers.
    void emitEvent(MonitorEvent e);
    void enqueuePath(const std::string& path, bool notify=true);
    void enqueueRemoval(const std::string& path, bool notify=false);
#ifdef _WIN32
    void windowsWatchLoop(const std::string& root, bool notify);
    void stopWindowsWatchers();
    std::vector<std::thread> watcherThreads_;
    std::vector<void*> watcherHandles_;
    std::mutex watcherMutex_;
#endif
    MonitorConfig config_; ResourcePolicy policy_{}; Callback callback_;
    std::atomic_bool running_{false}; std::atomic_bool paused_{false}; std::thread thread_; mutable std::mutex mutex_;
    StableFileDetector stable_; SystemLoadMonitor load_; MediaPipeline imagePipeline_; VideoFingerprintEngine videoEngine_;
    struct PendingItem {
        std::string path;
        bool notify=true;
        bool removal=false;
        std::chrono::steady_clock::time_point due{};
        unsigned retries=0;
    };
    std::unordered_map<std::string,std::uint64_t> seen_;
    std::queue<PendingItem> pending_;
    struct PendingFlags { bool notify=false; bool removal=false; };
    std::unordered_map<std::string,PendingFlags> pendingSet_;
    std::condition_variable queueCv_;
    std::unordered_map<std::string,unsigned> retryCounts_;
    MonitorStatus status_{};
    struct CompareEngine { std::string root; std::unique_ptr<MediaSearchEngine> engine; };
    std::vector<CompareEngine> compareEngines_;
};
}
